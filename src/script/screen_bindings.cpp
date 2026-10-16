#include "sgre/script/screen_bindings.hpp"

#include <array>
#include <cstdint>
#include <exception>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

namespace sgre::script {

	struct ScreenBindingRecord {
		SquirrelObjectIdentity Instance;
		scene::ScreenHandle Handle;
	};

	static bool ScreenBoolean(const SquirrelValue& value) {
		if (std::holds_alternative<std::monostate>(value)) {
			return false;
		}

		if (const auto* number = std::get_if<std::int32_t>(&value)) {
			return *number != 0;
		}

		if (const auto* number = std::get_if<float>(&value)) {
			return *number != 0;
		}

		if (const auto* boolean = std::get_if<bool>(&value)) {
			return *boolean;
		}

		return true;
	}

	class ScriptScreenBindings::Implementation {
	public:
		Implementation(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptLayerBindings& layers, scene::ScreenRegistry& screens) : Vm(vm), Objects(objects), Layers(layers), Screens(screens) {
		}

		SquirrelVm& Vm;
		ScriptObjectBindings& Objects;
		ScriptLayerBindings& Layers;
		scene::ScreenRegistry& Screens;
		SquirrelValue Class;
		SquirrelHostRoot ClassRoot;
		std::map<std::uint32_t, ScreenBindingRecord> Instances;

		ScreenBindingRecord& Record(const SquirrelValue& instance) {
			if (Vm.Type(instance) != SquirrelValueType::Instance) {
				throw std::runtime_error("Screen callback requires a native Screen instance");
			}

			const auto found = Instances.find(std::get<SquirrelObjectHandle>(instance).Index);
			if (found == Instances.end() || found->second.Instance != Vm.BorrowObjectIdentity(instance) || !Screens.Contains(found->second.Handle)) {
				throw std::runtime_error("Screen instance has no live render target");
			}

			return found->second;
		}

		void Construct(std::span<const SquirrelValue> arguments, const std::weak_ptr<Implementation>& owner) {
			if (arguments.size() != 3 && arguments.size() != 4) {
				throw std::runtime_error("Screen constructor requires width, height, and optional insertion flag");
			}

			const auto instance = arguments[0];
			if (!Vm.IsInstanceOf(instance, Class)) {
				throw std::runtime_error("Screen constructor requires its native instance receiver");
			}

			const auto index = std::get<SquirrelObjectHandle>(instance).Index;
			if (Instances.contains(index)) {
				throw std::runtime_error("Screen native state is already initialized");
			}

			const auto width = TryNativeInteger(arguments[1]);
			const auto height = TryNativeInteger(arguments[2]);
			const auto insertion = arguments.size() == 4 ? TryNativeInteger(arguments[3]) : std::optional<std::int32_t>(0);
			if (!width || !height || !insertion || *width <= 0 || *height <= 0) {
				throw std::runtime_error("Screen construction requires positive integer-convertible dimensions and a numeric insertion flag");
			}

			scene::ScreenDescription description;
			description.Width = static_cast<std::uint32_t>(*width);
			description.Height = static_cast<std::uint32_t>(*height);
			description.InsertBefore = *insertion != 0;
			const auto handle = Screens.Create(description);
			bool attached = false;
			try {
				Objects.AttachInstance(instance);
				attached = true;
				ScreenBindingRecord record;
				record.Instance = Vm.BorrowObjectIdentity(instance);
				record.Handle = handle;
				Instances.emplace(index, std::move(record));
				Objects.ConfigureNativeRelease(instance, [owner](const SquirrelValue& receiver) {
					if (const auto retained = owner.lock()) {
						retained->Release(receiver);
					}
				});
			} catch (...) {
				Instances.erase(index);
				Screens.Release(handle);
				if (attached) {
					Objects.ReleaseInstance(instance);
				}

				throw;
			}
		}

		void Release(const SquirrelValue& instance) {
			Objects.RequireCanRelease(instance);
			const auto handle = Record(instance).Handle;
			Screens.RequireCanRelease(handle);
			std::exception_ptr failure;
			try {
				Layers.DetachScreenOwners(instance);
			} catch (...) {
				failure = std::current_exception();
			}

			try {
				Objects.ReleaseInstance(instance);
			} catch (...) {
				if (!failure) {
					failure = std::current_exception();
				}
			}

			try {
				Screens.Release(handle);
			} catch (...) {
				if (!failure) {
					failure = std::current_exception();
				}
			}

			Instances.erase(std::get<SquirrelObjectHandle>(instance).Index);
			if (failure) {
				std::rethrow_exception(failure);
			}
		}
	};

	ScriptScreenBindings::ScriptScreenBindings(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptLayerBindings& layers, scene::ScreenRegistry& screens) : m_Implementation(std::make_shared<Implementation>(vm, objects, layers, screens)) {
		auto& state = *m_Implementation;
		state.Class = vm.CreateClass(objects.ObjectClass());
		state.ClassRoot = vm.Retain(state.Class);
		const auto weak = std::weak_ptr<Implementation>(m_Implementation);
		const auto method = [&](std::string name, auto operation) {
			const auto closure = vm.CreateNativeClosure("Screen." + name, [weak, operation](SquirrelVm&, std::span<const SquirrelValue> arguments) {
				const auto owner = weak.lock();
				if (!owner || arguments.empty()) {
					throw std::runtime_error("Screen callback owner has expired or lacks its receiver");
				}

				SquirrelNativeResult result;
				result.Value = operation(*owner, arguments);

				return result;
			});
			vm.NewSlot(state.Class, name, closure);
		};

		method("constructor", [weak](Implementation& owner, const auto arguments) -> SquirrelValue {
			owner.Construct(arguments, weak);

			return {};
		});
		method("getBounds", [](Implementation& owner, const auto arguments) {
			const auto bounds = owner.Screens.Bounds(owner.Record(arguments[0]).Handle);
			const auto result = owner.Vm.CreateTable();
			owner.Vm.NewSlot(result, std::string("left"), bounds.Left);
			owner.Vm.NewSlot(result, std::string("top"), bounds.Top);
			owner.Vm.NewSlot(result, std::string("right"), bounds.Right);
			owner.Vm.NewSlot(result, std::string("bottom"), bounds.Bottom);
			owner.Vm.NewSlot(result, std::string("width"), bounds.Width);
			owner.Vm.NewSlot(result, std::string("height"), bounds.Height);

			return result;
		});
		method("setVisible", [](Implementation& owner, const auto arguments) -> SquirrelValue {
			owner.Screens.SetVisible(owner.Record(arguments[0]).Handle, arguments.size() >= 2 && ScreenBoolean(arguments[1]));

			return {};
		});
		method("getVisible", [](Implementation& owner, const auto arguments) -> SquirrelValue {
			return owner.Screens.State(owner.Record(arguments[0]).Handle).Visible;
		});
		method("setBackgroundColor", [](Implementation& owner, const auto arguments) -> SquirrelValue {
			std::array<std::int32_t, 4> color{};
			for (std::size_t index = 0; index < color.size(); ++index) {
				color[index] = arguments.size() > index + 1 ? TryNativeInteger(arguments[index + 1]).value_or(0) : 0;
			}

			owner.Screens.SetBackgroundColor(owner.Record(arguments[0]).Handle, color);

			return {};
		});
		vm.NewSlot(vm.RootTable(), std::string("Screen"), state.Class);
		layers.ConfigureScreenOwners(state.Class, [weak](const SquirrelValue& instance) {
			const auto owner = weak.lock();
			if (!owner) {
				throw std::runtime_error("Screen owner resolver has expired");
			}

			return owner->Screens.State(owner->Record(instance).Handle).LayerRoot;
		});
	}

	ScriptScreenBindings::~ScriptScreenBindings() = default;

	SquirrelValue ScriptScreenBindings::ScreenClass() const {
		return m_Implementation->Class;
	}

	scene::ScreenHandle ScriptScreenBindings::Handle(const SquirrelValue& instance) const {
		return m_Implementation->Record(instance).Handle;
	}

	void ScriptScreenBindings::ReleaseInstance(const SquirrelValue& instance) {
		m_Implementation->Release(instance);
	}

} // namespace sgre::script
