#include "sgre/script/raw_image_bindings.hpp"

#include "sgre/script/binary_bindings.hpp"
#include "sgre/script/struct_bindings.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <exception>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

namespace sgre::script {

	struct RawImageBindingRecord {
		SquirrelObjectIdentity Instance;
		std::unique_ptr<scene::RawImage> Image;
	};

	static std::int32_t RawImageInteger(std::span<const SquirrelValue> arguments, std::size_t index) {
		return index < arguments.size() ? TryNativeInteger(arguments[index]).value_or(0) : 0;
	}

	static bool RawImageTruth(std::span<const SquirrelValue> arguments, std::size_t index) {
		if (index >= arguments.size() || std::holds_alternative<std::monostate>(arguments[index])) {
			return false;
		}
		if (const auto* value = std::get_if<bool>(&arguments[index])) {
			return *value;
		}
		if (const auto* value = std::get_if<std::int32_t>(&arguments[index])) {
			return *value != 0;
		}
		if (const auto* value = std::get_if<float>(&arguments[index])) {
			return *value != 0;
		}

		return true;
	}

	class ScriptRawImageBindings::Implementation {
	public:
		Implementation(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptBinaryBindings& binary, ScriptRawImageLimits limits) : Vm(vm), Objects(objects), Binary(binary), Limits(limits) {
			if (limits.MaxInstances == 0 || limits.MaxInstances > 65536 || limits.MaxPixelBytes == 0 || limits.MaxPixelBytes > 256 * 1024 * 1024) {
				throw std::runtime_error("RawImage services require bounded positive instance/storage limits");
			}
		}

		SquirrelVm& Vm;
		ScriptObjectBindings& Objects;
		ScriptBinaryBindings& Binary;
		ScriptStructBindings* Structs = nullptr;
		ScriptRawImageLimits Limits;
		SquirrelValue Class;
		SquirrelHostRoot ClassRoot;
		SquirrelValue ExtendedClass;
		SquirrelHostRoot ExtendedClassRoot;
		std::map<std::uint32_t, RawImageBindingRecord> Instances;
		std::size_t PixelBytes = 0;

		RawImageBindingRecord& Record(const SquirrelValue& instance) {
			if (Vm.Type(instance) != SquirrelValueType::Instance) {
				throw std::runtime_error("RawImage callback requires its native receiver");
			}

			const auto found = Instances.find(std::get<SquirrelObjectHandle>(instance).Index);
			if (found == Instances.end() || found->second.Instance != Vm.BorrowObjectIdentity(instance)) {
				throw std::runtime_error("RawImage receiver has no live pixel owner");
			}

			return found->second;
		}

		void Construct(std::span<const SquirrelValue> arguments, const std::weak_ptr<Implementation>& weak) {
			if (arguments.size() != 3 || !Vm.IsInstanceOf(arguments[0], Class)) {
				throw std::runtime_error("invalid argument.");
			}

			const auto width = RawImageInteger(arguments, 1);
			const auto height = RawImageInteger(arguments, 2);
			const auto available = Limits.MaxPixelBytes - PixelBytes;
			if (width <= 0 || height <= 0 || std::size_t(width) > available / 4 / std::size_t(height) || Instances.size() >= Limits.MaxInstances) {
				throw std::runtime_error("RawImage construction exceeds its positive image/aggregate storage domain");
			}
			const auto instance = arguments[0];
			const auto index = std::get<SquirrelObjectHandle>(instance).Index;
			if (Instances.contains(index)) {
				throw std::runtime_error("RawImage receiver is already initialized");
			}

			RawImageBindingRecord record;
			record.Instance = Vm.BorrowObjectIdentity(instance);
			record.Image = std::make_unique<scene::RawImage>(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height));
			const auto bytes = record.Image->BgraPixels().size();
			Objects.AttachInstance(instance);
			bool inserted = false;
			try {
				Instances.emplace(index, std::move(record));
				inserted = true;
				PixelBytes += bytes;
				Objects.ConfigureNativeRelease(instance, [weak](const SquirrelValue& receiver) {
					if (const auto owner = weak.lock()) {
						owner->Release(receiver);
					}
				});
			} catch (...) {
				if (inserted) {
					Instances.erase(index);
					PixelBytes -= bytes;
				}
				Objects.ReleaseInstance(instance);

				throw;
			}
		}

		void Release(const SquirrelValue& instance) {
			Objects.RequireCanRelease(instance);
			const auto bytes = Record(instance).Image->BgraPixels().size();
			// Native SQRawImage retires its owned pixel vector before Object notifications.
			Instances.erase(std::get<SquirrelObjectHandle>(instance).Index);
			PixelBytes -= bytes;
			Objects.ReleaseInstance(instance);
		}
	};

	ScriptRawImageBindings::ScriptRawImageBindings(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptBinaryBindings& binary, ScriptRawImageLimits limits) : m_Implementation(std::make_shared<Implementation>(vm, objects, binary, limits)) {
		auto& state = *m_Implementation;
		state.Class = vm.CreateClass(objects.ObjectClass());
		state.ClassRoot = vm.Retain(state.Class);
		const auto weak = std::weak_ptr<Implementation>(m_Implementation);
		const auto method = [&](std::string name, auto operation) {
			const auto callback = vm.CreateNativeClosure("RawImage." + name, [weak, operation](SquirrelVm&, std::span<const SquirrelValue> arguments) {
				const auto owner = weak.lock();
				if (!owner || arguments.empty()) {
					throw std::runtime_error("RawImage callbacks require a live service and receiver");
				}

				return operation(*owner, arguments);
			});
			vm.NewSlot(state.Class, name, callback);
		};
		method("constructor", [weak](Implementation& owner, auto arguments) {
			owner.Construct(arguments, weak);

			return SquirrelNativeResult::NoValues();
		});
		method("weakref", [](Implementation& owner, auto arguments) {
			return SquirrelNativeResult::ReturnValue(owner.Vm.CreateWeakReference(arguments.back()));
		});
		method("getWidth", [](Implementation& owner, auto arguments) {
			return SquirrelNativeResult::ReturnValue(std::bit_cast<std::int32_t>(owner.Record(arguments[0]).Image->Width()));
		});
		method("getHeight", [](Implementation& owner, auto arguments) {
			return SquirrelNativeResult::ReturnValue(std::bit_cast<std::int32_t>(owner.Record(arguments[0]).Image->Height()));
		});
		method("getUpdate", [](Implementation& owner, auto arguments) {
			return SquirrelNativeResult::ReturnValue(owner.Record(arguments[0]).Image->IsUpdated());
		});
		method("clearUpdate", [](Implementation& owner, auto arguments) {
			owner.Record(arguments[0]).Image->ClearUpdated();

			return SquirrelNativeResult::NoValues();
		});
		method("fill", [](Implementation& owner, auto arguments) {
			if (arguments.size() < 6) {
				throw std::runtime_error("invalid argument.");
			}

			scene::RawImageRegion region;
			region.X = std::bit_cast<std::uint32_t>(RawImageInteger(arguments, 1));
			region.Y = std::bit_cast<std::uint32_t>(RawImageInteger(arguments, 2));
			region.Width = std::bit_cast<std::uint32_t>(RawImageInteger(arguments, 3));
			region.Height = std::bit_cast<std::uint32_t>(RawImageInteger(arguments, 4));
			const auto color = std::bit_cast<std::uint32_t>(RawImageInteger(arguments, 5));
			owner.Record(arguments[0]).Image->Fill(region, color, RawImageTruth(arguments, 6));

			return SquirrelNativeResult::NoValues();
		});
		method("fillAlpha", [](Implementation& owner, auto arguments) {
			if (arguments.size() != 1) {
				throw std::runtime_error("invalid argument.");
			}
			owner.Record(arguments[0]).Image->FillAlpha();

			return SquirrelNativeResult::NoValues();
		});
		method("boxBlur", [](Implementation& owner, auto arguments) {
			if (arguments.size() != 3 && arguments.size() != 4) {
				throw std::runtime_error("invalid argument.");
			}
			const auto horizontal = RawImageInteger(arguments, 1);
			const auto vertical = RawImageInteger(arguments, 2);
			const auto iterations = arguments.size() == 4 ? RawImageInteger(arguments, 3) : 1;
			if (horizontal < 0 || vertical < 0 || iterations <= 0) {
				throw std::runtime_error("raw image blur requires nonnegative radii and a positive iteration count");
			}
			owner.Record(arguments[0]).Image->BoxBlur(static_cast<std::uint32_t>(horizontal), static_cast<std::uint32_t>(vertical), static_cast<std::uint32_t>(iterations));

			return SquirrelNativeResult::NoValues();
		});
		method("adjustLevel", [](Implementation& owner, auto arguments) {
			if (arguments.size() != 7) {
				throw std::runtime_error("invalid argument.");
			}
			std::array<std::int32_t, 6> endpoints{};
			for (std::size_t index = 0; index < endpoints.size(); ++index) {
				endpoints[index] = RawImageInteger(arguments, index + 1);
			}
			owner.Record(arguments[0]).Image->AdjustLevel(endpoints);

			return SquirrelNativeResult::NoValues();
		});
		method("store", [](Implementation& owner, auto arguments) {
			if (arguments.size() != 2) {
				throw std::runtime_error("invalid argument.");
			}
			if (owner.Structs == nullptr) {
				throw std::runtime_error("RawImage.store requires its actual mutable Struct character-array service");
			}
			const auto size = owner.Structs->CharacterArraySize(arguments[1]);
			const auto& image = *owner.Record(arguments[0]).Image;
			const auto pixels = std::size_t(image.Width()) * image.Height();
			if (size != pixels * 3 && size != pixels * 4) {
				throw std::runtime_error("char array size mismatch.");
			}
			const auto bytes = image.StorePixels(size == pixels * 4);
			owner.Structs->StoreCharacterArray(arguments[1], bytes);

			return SquirrelNativeResult::NoValues();
		});
		method("restore", [](Implementation& owner, auto arguments) {
			if (arguments.size() != 2) {
				throw std::runtime_error("invalid argument.");
			}

			if (owner.Structs == nullptr) {
				throw std::runtime_error("RawImage.restore requires its actual Struct character-array service");
			}
			const auto size = owner.Structs->CharacterArraySize(arguments[1]);
			auto& image = *owner.Record(arguments[0]).Image;
			const auto pixels = std::size_t(image.Width()) * image.Height();
			if (size != pixels * 3 && size != pixels * 4) {
				throw std::runtime_error("char array size mismatch.");
			}
			image.RestorePixels(owner.Structs->ReadCharacterArray(arguments[1]));

			return SquirrelNativeResult::NoValues();
		});
		vm.NewSlot(vm.RootTable(), std::string("RawImage"), state.Class);
		// Native0609e0 derives SQRawImageEx from SQRawImage. Save thumbnails use the inherited pixel owner.
		state.ExtendedClass = vm.CreateClass(state.Class);
		state.ExtendedClassRoot = vm.Retain(state.ExtendedClass);
		vm.NewSlot(vm.RootTable(), std::string("RawImageEx"), state.ExtendedClass);
	}

	ScriptRawImageBindings::~ScriptRawImageBindings() = default;

	SquirrelValue ScriptRawImageBindings::RawImageClass() const {
		return m_Implementation->Class;
	}

	void ScriptRawImageBindings::ConfigureStorageOwners(ScriptStructBindings& actual_structs) {
		auto& state = *m_Implementation;
		if (state.Structs != nullptr || !state.Instances.empty()) {
			throw std::runtime_error("RawImage storage service must be configured once before image construction");
		}
		state.Structs = &actual_structs;
	}

	const scene::RawImage& ScriptRawImageBindings::Image(const SquirrelValue& instance) const {
		return *m_Implementation->Record(instance).Image;
	}

	void ScriptRawImageBindings::ReleaseInstance(const SquirrelValue& instance) {
		m_Implementation->Release(instance);
	}

	void ScriptRawImageBindings::StoreThumbnail(const SquirrelValue& instance, std::span<const std::uint8_t> bgra) {
		m_Implementation->Record(instance).Image->RestoreThumbnail(bgra);
	}

} // namespace sgre::script
