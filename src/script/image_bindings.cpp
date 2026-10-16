#include "sgre/script/image_bindings.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <exception>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>

namespace sgre::script {

	class ScriptImagePayload {
	public:
		SquirrelHostRoot PsbOwner;
		std::shared_ptr<const scene::LoadedImageAsset> Asset;
		std::unique_ptr<scene::ImageTextureOwner> Sources;
		bool Closed = false;
		bool Closing = false;

		void RequireLive() const {
			if (Closed || Closing || !Sources) {
				throw std::runtime_error("Image native owner has expired or is retiring");
			}
		}

		void Close() {
			if (Closed) {
				return;
			}
			if (Closing) {
				throw std::runtime_error("Image native owner cleanup cannot reenter");
			}

			Closing = true;
			std::exception_ptr failure;
			try {
				Sources->Close();
			} catch (...) {
				failure = std::current_exception();
			}
			Sources.reset();
			Asset.reset();
			PsbOwner = {};
			Closed = true;
			Closing = false;
			if (failure) {
				std::rethrow_exception(failure);
			}
		}
	};

	struct ImageBindingRecord {
		SquirrelObjectIdentity Instance;
		std::shared_ptr<ScriptImagePayload> Payload;
	};

	enum class ImageMethod : std::uint32_t {
		GetValid = 1,
		GetWidth,
		GetHeight,
		Reset,
		Grayscale,
		Negate,
		AdjustLevel,
		AdjustGamma
	};

	static std::array<std::uint8_t, 8> ImageMethodMetadata(ImageMethod method) {
		const auto word = static_cast<std::uint32_t>(method);

		return {static_cast<std::uint8_t>(word), static_cast<std::uint8_t>(word >> 8), static_cast<std::uint8_t>(word >> 16), static_cast<std::uint8_t>(word >> 24), 0, 0, 0, 0};
	}

	static void RequireImageMethod(SquirrelVm& vm, ImageMethod method) {
		const auto captured = vm.NativeStackValue(-1);
		if (vm.Type(captured) != SquirrelValueType::UserData) {
			throw std::runtime_error("Image callback requires its actual captured userdata metadata");
		}

		const auto bytes = vm.UserDataBytes(captured);
		const auto expected = ImageMethodMetadata(method);
		if (bytes.size() != expected.size() || !std::equal(bytes.begin(), bytes.end(), expected.begin())) {
			throw std::runtime_error("Image captured method identity is invalid");
		}
	}

	class ScriptImageBindings::Implementation {
	public:
		Implementation(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptPsbBindings& psb, scene::ImageAssetCatalog& assets, scene::TextureCache& cache, std::size_t maximum)
		    : Vm(vm), Objects(objects), Psb(psb), Assets(assets), Cache(cache), Maximum(maximum) {
			if (!assets.UsesCache(cache) || maximum == 0 || maximum > 65536) {
				throw std::runtime_error("Image binding requires actual catalog/cache services and bounded wrapper capacity");
			}
		}

		SquirrelVm& Vm;
		ScriptObjectBindings& Objects;
		ScriptPsbBindings& Psb;
		scene::ImageAssetCatalog& Assets;
		scene::TextureCache& Cache;
		std::size_t Maximum;
		SquirrelValue Class;
		SquirrelHostRoot ClassRoot;
		std::map<std::uint32_t, ImageBindingRecord> Instances;

		ImageBindingRecord& Record(const SquirrelValue& instance) {
			if (Vm.Type(instance) != SquirrelValueType::Instance) {
				throw std::runtime_error("Image callback requires its genuine native instance");
			}

			const auto found = Instances.find(std::get<SquirrelObjectHandle>(instance).Index);
			if (found == Instances.end() || found->second.Instance != Vm.BorrowObjectIdentity(instance)) {
				throw std::runtime_error("Image callback receiver has no live native identity");
			}

			return found->second;
		}

		void Construct(std::span<const SquirrelValue> arguments, const std::weak_ptr<Implementation>& weak) {
			if ((arguments.size() != 2 && arguments.size() != 3) || !Vm.IsInstanceOf(arguments[0], Class) || !Vm.IsInstanceOf(arguments[1], Psb.ObjectClass())) {
				throw std::runtime_error("invalid argument.");
			}

			const auto instance = arguments[0];
			const auto index = std::get<SquirrelObjectHandle>(instance).Index;
			if (Instances.contains(index) || Instances.size() == Maximum) {
				throw std::runtime_error("Image native wrapper capacity is exhausted or receiver is already initialized");
			}
			std::string_view label;
			if (arguments.size() == 3) {
				const auto* text = TryStringText(arguments[2]);
				if (!text) {
					throw std::runtime_error("Image generated-text label lifetime requires a separately recovered native conversion path");
				}

				// The callback argument's canonical string owner outlives this borrowed label.
				label = *text;
			}

			const auto psb_asset = Psb.DocumentAsset(arguments[1]);
			auto payload = std::make_shared<ScriptImagePayload>();
			payload->PsbOwner = Vm.Retain(arguments[1]);
			payload->Asset = psb_asset.Image ? Assets.Prepare(psb_asset.Image, label) : Assets.Prepare(psb_asset.Document, psb_asset.Backing, label);
			payload->Sources = std::make_unique<scene::ImageTextureOwner>(Cache, payload->Asset);
			ImageBindingRecord record;
			record.Instance = Vm.BorrowObjectIdentity(instance);
			record.Payload = std::move(payload);
			Objects.AttachInstance(instance);
			bool inserted = false;
			try {
				Instances.emplace(index, std::move(record));
				inserted = true;
				Objects.ConfigureNativeRelease(instance, [weak](const SquirrelValue& receiver) {
					if (const auto owner = weak.lock()) {
						owner->Release(receiver);
					}
				});
			} catch (...) {
				if (inserted) {
					Instances.erase(index);
				}
				Objects.ReleaseInstance(instance);

				throw;
			}
		}

		void Release(const SquirrelValue& instance) {
			Objects.RequireCanRelease(instance);
			const auto payload = Record(instance).Payload;
			std::exception_ptr failure;
			try {
				payload->Close();
			} catch (...) {
				failure = std::current_exception();
			}
			Instances.erase(std::get<SquirrelObjectHandle>(instance).Index);
			try {
				Objects.ReleaseInstance(instance);
			} catch (...) {
				if (!failure) {
					failure = std::current_exception();
				}
			}
			if (failure) {
				std::rethrow_exception(failure);
			}
		}
	};

	bool ScriptNativeImageBorrow::IsLive() const {
		const auto payload = m_Payload.lock();

		return payload && !payload->Closed && !payload->Closing;
	}

	std::shared_ptr<const scene::LoadedImageAsset> ScriptNativeImageBorrow::Asset() const {
		const auto payload = m_Payload.lock();
		if (!payload) {
			throw std::runtime_error("borrowed native Image owner has expired");
		}
		payload->RequireLive();

		return payload->Asset;
	}

	scene::MotionTextureWrapper::Borrowed ScriptNativeImageBorrow::Source(std::size_t index) const {
		const auto payload = m_Payload.lock();
		if (!payload) {
			throw std::runtime_error("borrowed native Image owner has expired");
		}
		payload->RequireLive();

		return payload->Sources->Source(index);
	}

	void ScriptNativeImageBorrow::SetFiltering(std::int32_t minification, std::int32_t magnification) const {
		const auto payload = m_Payload.lock();
		if (!payload) {
			throw std::runtime_error("borrowed native Image owner has expired");
		}
		payload->RequireLive();
		payload->Sources->SetFiltering(minification, magnification);
	}

	ScriptImageBindings::ScriptImageBindings(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptPsbBindings& psb, scene::ImageAssetCatalog& assets, scene::TextureCache& cache, std::size_t max_instances)
	    : m_Implementation(std::make_shared<Implementation>(vm, objects, psb, assets, cache, max_instances)) {
		auto& state = *m_Implementation;
		state.Class = vm.CreateClass(objects.ObjectClass());
		state.ClassRoot = vm.Retain(state.Class);
		const auto weak = std::weak_ptr<Implementation>(m_Implementation);
		vm.NewSlot(state.Class, std::string("constructor"), vm.CreateNativeClosure("Image.constructor", [weak](SquirrelVm&, auto arguments) {
			const auto owner = weak.lock();
			if (!owner) {
				throw std::runtime_error("Image constructor requires its live binding services");
			}
			owner->Construct(arguments, weak);

			return SquirrelNativeResult::NoValues();
		}));
		vm.NewSlot(state.Class, std::string("weakref"), vm.CreateNativeClosure("Image.weakref", [weak](SquirrelVm&, auto arguments) {
			const auto owner = weak.lock();
			if (!owner || arguments.empty()) {
				throw std::runtime_error("Image weakref requires a live binding and its original final argument");
			}

			return SquirrelNativeResult::ReturnValue(owner->Vm.CreateWeakReference(arguments.back()));
		}));
		const auto method = [&](std::string name, ImageMethod identity) {
			const auto metadata = ImageMethodMetadata(identity);
			const std::array<SquirrelValue, 1> captures{vm.CreateUserData(metadata)};
			const auto callback = vm.CreateNativeClosureWithCaptures(
			    "Image." + name, [weak, identity](SquirrelVm&, auto arguments) {
				    const auto owner = weak.lock();
				    if (!owner || arguments.empty()) {
					    throw std::runtime_error("Image method requires a live binding and native receiver");
				    }
				    RequireImageMethod(owner->Vm, identity);
				    const auto payload = owner->Record(arguments[0]).Payload;
				    payload->RequireLive();
				    if (identity == ImageMethod::GetValid) {
					    return SquirrelNativeResult::ReturnValue(payload->Asset->Valid);
				    }
				    if (identity == ImageMethod::GetWidth) {
					    return SquirrelNativeResult::ReturnValue(std::bit_cast<std::int32_t>(payload->Asset->Model.Width));
				    }
				    if (identity == ImageMethod::GetHeight) {
					    return SquirrelNativeResult::ReturnValue(std::bit_cast<std::int32_t>(payload->Asset->Model.Height));
				    }

				    throw std::runtime_error("Image color mutation requires its recovered exclusive source-copy/pixel-operator pipeline");
			    },
			    captures);
			vm.NewSlot(state.Class, name, callback);
		};
		method("getValid", ImageMethod::GetValid);
		method("getWidth", ImageMethod::GetWidth);
		method("getHeight", ImageMethod::GetHeight);
		method("reset", ImageMethod::Reset);
		method("grayscale", ImageMethod::Grayscale);
		method("negate", ImageMethod::Negate);
		method("adjustLevel", ImageMethod::AdjustLevel);
		method("adjustGamma", ImageMethod::AdjustGamma);
		const auto image_constants = vm.CreateTable();
		vm.NewSlot(image_constants, std::string("GRAYSCALE_METHOD_INT_NTSC"), std::int32_t(0));
		vm.NewSlot(image_constants, std::string("GRAYSCALE_METHOD_NTSC"), std::int32_t(1));
		vm.NewSlot(image_constants, std::string("GRAYSCALE_METHOD_ITU"), std::int32_t(2));
		vm.NewSlot(vm.ConstantTable(), std::string("IMAGE"), image_constants);
		vm.NewSlot(vm.RootTable(), std::string("Image"), state.Class);
	}

	ScriptImageBindings::~ScriptImageBindings() = default;

	SquirrelValue ScriptImageBindings::ImageClass() const {
		return m_Implementation->Class;
	}

	ScriptNativeImageBorrow ScriptImageBindings::BorrowNativeImage(const SquirrelValue& instance) const {
		ScriptNativeImageBorrow borrowed;
		borrowed.m_Payload = m_Implementation->Record(instance).Payload;

		return borrowed;
	}

	void ScriptImageBindings::ReleaseInstance(const SquirrelValue& instance) {
		m_Implementation->Release(instance);
	}

} // namespace sgre::script
