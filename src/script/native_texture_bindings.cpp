#include "sgre/script/native_texture_bindings.hpp"

#include "sgre/scene/raw_layer_owner.hpp"
#include "sgre/scene/screens.hpp"
#include "sgre/scene/texture_cache.hpp"
#include "sgre/script/image_bindings.hpp"
#include "sgre/script/raw_texture_bindings.hpp"
#include "sgre/script/screen_bindings.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

namespace sgre::script {

	class ScriptNativeTexturePayload {
	public:
		SquirrelHostRoot SourceOwner;
		std::unique_ptr<scene::NativeTextureReference> Reference;
		bool Releasing = false;
	};

	struct NativeTextureRecord {
		SquirrelObjectIdentity Instance;
		std::shared_ptr<ScriptNativeTexturePayload> Payload;
	};

	static bool NativeTextureBoolean(std::span<const SquirrelValue> arguments) {
		if (arguments.size() < 2 || std::holds_alternative<std::monostate>(arguments[1])) {
			return false;
		}
		if (const auto* number = std::get_if<float>(&arguments[1])) {
			return *number != 0;
		}
		if (const auto* number = std::get_if<std::int32_t>(&arguments[1])) {
			return *number != 0;
		}
		if (const auto* boolean = std::get_if<bool>(&arguments[1])) {
			return *boolean;
		}

		return true;
	}

	static std::uint32_t NativeImageFormat(resource::PixelFormat format) {
		switch (format) {
		case resource::PixelFormat::Alpha8:
			return 65;
		case resource::PixelFormat::Luminance8:
			return 61;
		case resource::PixelFormat::Bc7:
			return 98;
		default:
			throw std::runtime_error("NativeTextureRef Image has an unknown native texture interpretation");
		}
	}

	static std::uint32_t NativeAddressWord(std::uint32_t index) {
		if (index > 2) {
			throw std::runtime_error("NativeTextureRef Image address is outside the recovered index table");
		}

		return std::array<std::uint32_t, 3>{3, 1, 2}[index];
	}

	class ScriptNativeTextureBindings::Implementation {
	public:
		Implementation(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptScreenBindings& screen_bindings, scene::ScreenRegistry& screens, ScriptRawTextureBindings& raw, ScriptImageBindings& images, scene::TextureCache& cache, render::SpritePassRenderer& renderer) : Vm(vm), Objects(objects), ScreenBindings(screen_bindings), Screens(screens), Raw(raw), Images(images), Cache(cache), Renderer(renderer) {
		}

		SquirrelVm& Vm;
		ScriptObjectBindings& Objects;
		ScriptScreenBindings& ScreenBindings;
		scene::ScreenRegistry& Screens;
		ScriptRawTextureBindings& Raw;
		ScriptImageBindings& Images;
		scene::TextureCache& Cache;
		render::SpritePassRenderer& Renderer;
		SquirrelValue Class;
		SquirrelHostRoot ClassRoot;
		std::map<std::uint32_t, NativeTextureRecord> Instances;

		std::shared_ptr<ScriptNativeTexturePayload> Payload(const SquirrelValue& instance) const {
			if (Vm.Type(instance) != SquirrelValueType::Instance) {
				throw std::runtime_error("NativeTextureRef requires its genuine native instance");
			}
			const auto found = Instances.find(std::get<SquirrelObjectHandle>(instance).Index);
			if (found == Instances.end() || found->second.Instance != Vm.BorrowObjectIdentity(instance) || !found->second.Payload || found->second.Payload->Releasing || !found->second.Payload->Reference) {
				throw std::runtime_error("NativeTextureRef receiver has retired or has another native identity");
			}

			return found->second.Payload;
		}

		std::unique_ptr<scene::NativeTextureReference> FromScreen(const SquirrelValue& source) {
			const auto screen = ScreenBindings.Handle(source);
			const auto texture = Screens.Renderer(screen).SampledTarget();
			return std::make_unique<scene::NativeTextureReference>([this, screen, texture] {
				const auto state = Screens.State(screen);
				const auto physical = Screens.Renderer(screen).Description();
				const auto filtering = Renderer.SampledTargetFiltering(texture);
				scene::NativeTextureSnapshot snapshot;
				snapshot.Texture = texture;
				snapshot.Size = {static_cast<float>(physical.Width), static_cast<float>(physical.Height)};
				snapshot.ActiveBounds = {0, 0, static_cast<float>(state.Description.Width), static_cast<float>(state.Description.Height)};
				snapshot.Minification = filtering[0];
				snapshot.Magnification = filtering[1];

				return snapshot;
			},
			    [this, texture](auto minimum, auto maximum) {
				    Renderer.SetSampledTargetFiltering(texture, minimum, maximum);
			    });
		}

		std::unique_ptr<scene::NativeTextureReference> FromRaw(const SquirrelValue& source) {
			auto& task = Raw.Owner(source).TextureTask();
			task.Texture().Bind(0, scene::MutableTextureStage::Pixel);
			const auto texture = Renderer.MutableTextureBinding(0, scene::MutableTextureStage::Pixel);

			return std::make_unique<scene::NativeTextureReference>([this, source, texture] {
				auto& current = Raw.Owner(source).TextureTask();
				const auto state = current.Texture().State();
				const auto filter = state.DesiredSampler.FilterWord;
				if (filter != 0 && filter != 0x15) {
					throw std::runtime_error("NativeTextureRef RawTex requires its recovered common sampler filter");
				}
				scene::NativeTextureSnapshot snapshot;
				snapshot.Texture = texture;
				snapshot.MutableSource = &current.Texture();
				snapshot.Size = {static_cast<float>(state.Description.Width), static_cast<float>(state.Description.Height)};
				const auto logical = current.State().LogicalExtent;
				snapshot.ActiveBounds = {0, 0, logical.X, logical.Y};
				snapshot.OriginalFormat = state.Description.OriginalFormat;
				snapshot.Minification = filter == 0 ? 0 : 1;
				snapshot.Magnification = snapshot.Minification;
				snapshot.AddressU = state.DesiredSampler.AddressU;
				snapshot.AddressV = state.DesiredSampler.AddressV;

				return snapshot;
			},
			    [this, source](auto minimum, auto maximum) {
				    Raw.Owner(source).TextureTask().SetFiltering(minimum, maximum);
			    });
		}

		std::unique_ptr<scene::NativeTextureReference> FromImage(const SquirrelValue& source) {
			const auto image = Images.BorrowNativeImage(source);
			const auto asset = image.Asset();
			if (asset->TileBounds.empty() || asset->Model.Tiles.empty()) {
				throw std::runtime_error("NativeTextureRef Image requires its genuine first native texture and active rectangle");
			}
			const auto wrapper = image.Source(0);
			const auto active = asset->TileBounds[0].Bounds;
			return std::make_unique<scene::NativeTextureReference>([this, image, wrapper, active] {
				static_cast<void>(image.Asset());
				const auto texture = Cache.Get(wrapper.Texture());
				const auto address = Cache.Address(wrapper.Texture());
				scene::NativeTextureSnapshot snapshot;
				snapshot.Texture = texture.Backend;
				snapshot.Size = {static_cast<float>(texture.Description.Width), static_cast<float>(texture.Description.Height)};
				snapshot.ActiveBounds = {active[0], active[1], active[2], active[3]};
				snapshot.OriginalFormat = NativeImageFormat(texture.Description.Format);
				snapshot.Minification = static_cast<std::uint32_t>(texture.Minification);
				snapshot.Magnification = static_cast<std::uint32_t>(texture.Magnification);
				snapshot.AddressU = NativeAddressWord(address.Horizontal);
				snapshot.AddressV = NativeAddressWord(address.Vertical);

				return snapshot;
			},
			    [wrapper](auto minimum, auto maximum) {
				    wrapper.SetFiltering(static_cast<std::int32_t>(minimum), static_cast<std::int32_t>(maximum));
			    });
		}

		void Construct(std::span<const SquirrelValue> arguments, const std::weak_ptr<Implementation>& weak) {
			if (arguments.size() != 2 || !Vm.IsInstanceOf(arguments[0], Class)) {
				throw std::runtime_error("invalid argument.");
			}
			const auto index = std::get<SquirrelObjectHandle>(arguments[0]).Index;
			if (Instances.contains(index) || Instances.size() == 65536) {
				throw std::runtime_error("NativeTextureRef receiver is initialized or exceeds its bounded instance capacity");
			}
			auto payload = std::make_shared<ScriptNativeTexturePayload>();
			payload->SourceOwner = Vm.Retain(arguments[1]);
			if (Vm.IsInstanceOf(arguments[1], ScreenBindings.ScreenClass())) {
				payload->Reference = FromScreen(arguments[1]);
			} else if (Vm.IsInstanceOf(arguments[1], Raw.RawTextureClass())) {
				payload->Reference = FromRaw(arguments[1]);
			} else if (Vm.IsInstanceOf(arguments[1], Images.ImageClass())) {
				payload->Reference = FromImage(arguments[1]);
			} else {
				throw std::runtime_error("invalid argument.");
			}
			Objects.AttachInstance(arguments[0]);
			try {
				Instances.emplace(index, NativeTextureRecord{Vm.BorrowObjectIdentity(arguments[0]), payload});
				Objects.ConfigureNativeRelease(arguments[0], [weak](const auto& receiver) {
					if (const auto owner = weak.lock()) {
						owner->Release(receiver);
					}
				});
			} catch (...) {
				Instances.erase(index);
				Objects.ReleaseInstance(arguments[0]);
				throw;
			}
		}

		void Release(const SquirrelValue& instance) {
			Objects.RequireCanRelease(instance);
			const auto payload = Payload(instance);
			payload->Reference->RequireCanRelease();
			payload->Releasing = true;
			Instances.erase(std::get<SquirrelObjectHandle>(instance).Index);
			payload->Reference.reset();
			payload->SourceOwner = {};
			Objects.ReleaseInstance(instance);
		}
	};

	const scene::NativeTextureReference& ScriptNativeTextureBorrow::Reference() const {
		const auto payload = m_Payload.lock();
		if (!payload || payload->Releasing || !payload->Reference) {
			throw std::runtime_error("borrowed NativeTextureRef has no live actual descriptor owner");
		}

		return *payload->Reference;
	}

	ScriptNativeTextureBindings::ScriptNativeTextureBindings(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptScreenBindings& screen_bindings, scene::ScreenRegistry& screens, ScriptRawTextureBindings& raw, ScriptImageBindings& images, scene::TextureCache& cache, render::SpritePassRenderer& renderer) : m_Implementation(std::make_shared<Implementation>(vm, objects, screen_bindings, screens, raw, images, cache, renderer)) {
		auto& state = *m_Implementation;
		state.Class = vm.CreateClass(objects.ObjectClass());
		state.ClassRoot = vm.Retain(state.Class);
		const auto weak = std::weak_ptr<Implementation>(m_Implementation);
		const auto method = [&](const std::string& name, auto operation) {
			vm.NewSlot(state.Class, name, vm.CreateNativeClosure("NativeTextureRef." + name, [weak, operation](SquirrelVm&, std::span<const SquirrelValue> arguments) {
				const auto owner = weak.lock();
				if (!owner || arguments.empty()) {
					throw std::runtime_error("NativeTextureRef callback owner has retired or lacks its receiver");
				}
				SquirrelNativeResult result;
				result.Value = operation(*owner, arguments);

				return result;
			}));
		};
		method("constructor", [weak](Implementation& owner, const auto arguments) -> SquirrelValue {
			owner.Construct(arguments, weak);

			return {};
		});
		method("setSmoothing", [](Implementation& owner, const auto arguments) -> SquirrelValue {
			owner.Payload(arguments[0])->Reference->SetSmoothing(NativeTextureBoolean(arguments));

			return {};
		});
		method("getSize", [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto size = owner.Payload(arguments[0])->Reference->Read().Size;
			const auto table = owner.Vm.CreateTable();
			owner.Vm.NewSlot(table, std::string("width"), size.X);
			owner.Vm.NewSlot(table, std::string("height"), size.Y);

			return table;
		});
		method("getActiveBounds", [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto bounds = owner.Payload(arguments[0])->Reference->Read().ActiveBounds;
			const auto table = owner.Vm.CreateTable();
			owner.Vm.NewSlot(table, std::string("left"), bounds.MinimumX);
			owner.Vm.NewSlot(table, std::string("top"), bounds.MinimumY);
			owner.Vm.NewSlot(table, std::string("right"), bounds.MaximumX);
			owner.Vm.NewSlot(table, std::string("bottom"), bounds.MaximumY);
			owner.Vm.NewSlot(table, std::string("width"), bounds.MaximumX - bounds.MinimumX);
			owner.Vm.NewSlot(table, std::string("height"), bounds.MaximumY - bounds.MinimumY);

			return table;
		});
		vm.NewSlot(vm.RootTable(), std::string("NativeTextureRef"), state.Class);
	}

	ScriptNativeTextureBindings::~ScriptNativeTextureBindings() = default;

	SquirrelValue ScriptNativeTextureBindings::NativeTextureClass() const {
		return m_Implementation->Class;
	}

	ScriptNativeTextureBorrow ScriptNativeTextureBindings::BorrowNativeTexture(const SquirrelValue& instance) const {
		ScriptNativeTextureBorrow result;
		result.m_Payload = m_Implementation->Payload(instance);

		return result;
	}

	void ScriptNativeTextureBindings::ReleaseInstance(const SquirrelValue& instance) {
		m_Implementation->Release(instance);
	}

} // namespace sgre::script
