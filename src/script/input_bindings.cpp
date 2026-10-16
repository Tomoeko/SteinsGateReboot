#include "sgre/script/input_bindings.hpp"

#include "sgre/core/looper_frame.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <exception>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace sgre::script {

	static constexpr std::size_t kMaximumInputHubs = 64;
	static constexpr std::size_t kMaximumInputViews = 65536;

	struct InputHubRecord {
		SquirrelObjectIdentity Instance;
		std::shared_ptr<input::InputHub> Hub;
		std::optional<std::uint64_t> PhysicalOwner;
	};

	struct InputHubFrameOwner {
		std::shared_ptr<input::InputHub> Hub;
		std::uint32_t VmIndex = 0;
		std::optional<scene::ParallelTaskHandle> Clock;
		std::optional<scene::ParallelTaskHandle> Task;
	};

	struct InputDeviceRecord {
		SquirrelObjectIdentity Instance;
		// Native SQInput borrows the hub's MInput pointer. Expired hubs fail before access here.
		std::weak_ptr<input::InputHub> Hub;
		std::int32_t Index = 0;
		input::MouseInput Mouse;
		std::optional<scene::ParallelTaskHandle> Clock;
	};

	static bool InputBoolean(const SquirrelValue& value) {
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

	static std::int32_t InputInteger(std::span<const SquirrelValue> arguments, std::size_t index) {
		return arguments.size() > index ? TryNativeInteger(arguments[index]).value_or(0) : 0;
	}

	static float InputFloat(std::span<const SquirrelValue> arguments, std::size_t index) {
		if (index >= arguments.size()) {
			return 0;
		}

		if (const auto* number = std::get_if<float>(&arguments[index])) {
			return *number;
		}

		if (const auto integer = TryNativeInteger(arguments[index])) {
			return static_cast<float>(*integer);
		}

		return 0;
	}

	class ScriptInputBindings::Implementation {
	public:
		Implementation(SquirrelVm& vm, ScriptObjectBindings& objects, input::KeyMappings& mappings) : Vm(vm), Objects(objects), Mappings(mappings) {
		}

		SquirrelVm& Vm;
		ScriptObjectBindings& Objects;
		input::KeyMappings& Mappings;
		SquirrelValue HubClass;
		SquirrelValue DeviceClass;
		SquirrelHostRoot HubClassRoot;
		SquirrelHostRoot DeviceClassRoot;
		std::map<std::uint32_t, InputHubRecord> Hubs;
		std::map<std::uint32_t, InputDeviceRecord> Devices;
		std::map<std::uint64_t, InputHubFrameOwner> PhysicalHubs;
		std::uint64_t NextPhysicalHub = 0;
		ClientReader ReadClient;
		CursorRequest RequestCursor;
		FunctionKeyReader ReadFunctionKeys;
		KeyboardTextReader ReadKeyboardText;
		bool ReadingFunctionKeys = false;
		core::LooperFrame* FrameLooper = nullptr;
		scene::ParallelTaskSet* DriverParallel = nullptr;
		HubFrameReader ReadHubFrame;
		MouseFrameReader ReadMouseFrame;
		const std::thread::id FrameThread = std::this_thread::get_id();
		bool SamplingFrame = false;
		bool RetiringFrameClock = false;

		void RequireInputMutation() const {
			if (std::this_thread::get_id() != FrameThread || SamplingFrame) {
				throw std::runtime_error("input instance mutation requires its owner thread outside actual sampling");
			}
		}

		std::optional<scene::ParallelTaskHandle>& ClockFor(std::uint64_t index, bool mouse) {
			return mouse ? Devices.at(static_cast<std::uint32_t>(index)).Clock : PhysicalHubs.at(index).Clock;
		}

		void AdvanceInstance(std::uint64_t index, bool mouse) {
			RequireInputMutation();
			if (FrameLooper == nullptr || RetiringFrameClock) {
				throw std::runtime_error("input VSync requires its configured actual Looper and platform readers");
			}
			SamplingFrame = true;
			try {
				if (mouse) {
					Devices.at(static_cast<std::uint32_t>(index)).Mouse.Advance(ReadMouseFrame());
				} else {
					const auto base = FrameLooper->FixedBaseCount();
					const auto& clock = FrameLooper->Clock();
					const float step = clock.VariableFrame() ? std::max(clock.VariableStep(), 1.0f) : static_cast<float>(base);
					if (!std::isfinite(step) || step < 0 || step >= 2147483648.0f) {
						throw std::runtime_error("input frame exceeds native nonnegative integer elapsed domain");
					}
					auto& hub = *PhysicalHubs.at(index).Hub;
					const auto sample = ReadHubFrame(hub, Mappings);
					hub.Advance(sample.Devices, static_cast<std::int32_t>(step), sample.CursorExtent);
				}
				SamplingFrame = false;
			} catch (...) {
				SamplingFrame = false;
				throw;
			}
		}

		void RegisterFrameClock(std::uint64_t index, bool mouse, const std::weak_ptr<Implementation>& weak) {
			if (FrameLooper == nullptr) {
				return;
			}
			auto& group = FrameLooper->Group(core::LooperFrameGroup::VSync);
			group.RequireIdleTraversal();
			scene::ParallelTaskCallbacks callbacks;
			callbacks.Update = [weak, index, mouse](auto handle) {
				const auto owner = weak.lock();
				if (!owner || owner->ClockFor(index, mouse) != handle) {
					throw std::runtime_error("input VSync clock lost its actual instance owner");
				}
				owner->AdvanceInstance(index, mouse);
			};
			callbacks.CanRelease = [weak, index, mouse](auto handle) {
				const auto owner = weak.lock();
				return owner && owner->RetiringFrameClock && !owner->SamplingFrame && owner->ClockFor(index, mouse) == handle;
			};
			callbacks.Release = [weak, index, mouse](auto handle) {
				const auto owner = weak.lock();
				if (!owner || !owner->RetiringFrameClock || owner->SamplingFrame || owner->ClockFor(index, mouse) != handle) {
					throw std::runtime_error("input frame-clock retirement requires its exact live registry owner");
				}
				owner->ClockFor(index, mouse).reset();
			};
			ClockFor(index, mouse) = group.Append(std::move(callbacks));
		}

		void RetireFrameClock(std::uint64_t index, bool mouse) {
			RequireInputMutation();
			if (mouse ? !Devices.contains(static_cast<std::uint32_t>(index)) : !PhysicalHubs.contains(index)) {
				return;
			}
			const auto clock = ClockFor(index, mouse);
			if (!clock) {
				return;
			}
			RetiringFrameClock = true;
			try {
				auto& group = FrameLooper->Group(core::LooperFrameGroup::VSync);
				if (!group.CanExtract(*clock)) {
					throw std::runtime_error("input frame clock remains borrowed by actual traversal");
				}
				auto owned = group.Extract(*clock);
				owned.Release();
				RetiringFrameClock = false;
			} catch (...) {
				RetiringFrameClock = false;
				throw;
			}
		}

		bool CanRetireHub(std::uint64_t index, scene::ParallelTaskHandle task) const noexcept {
			try {
				if (std::this_thread::get_id() != FrameThread || SamplingFrame || RetiringFrameClock || FrameLooper == nullptr || DriverParallel == nullptr) {
					return false;
				}
				const auto found = PhysicalHubs.find(index);
				if (found == PhysicalHubs.end() || found->second.Task != task) {
					return false;
				}
				auto& group = FrameLooper->Group(core::LooperFrameGroup::VSync);
				group.RequireIdleTraversal();
				return found->second.Clock && group.Contains(*found->second.Clock);
			} catch (...) {
				return false;
			}
		}

		void RetireHub(std::uint64_t index, scene::ParallelTaskHandle task) {
			if (!CanRetireHub(index, task)) {
				throw std::runtime_error("InputHub task retirement requires its exact idle VSync owner");
			}
			RetireFrameClock(index, false);
			const auto vm_index = PhysicalHubs.at(index).VmIndex;
			const auto wrapper = Hubs.find(vm_index);
			if (wrapper != Hubs.end() && wrapper->second.PhysicalOwner == index) {
				wrapper->second.Hub.reset();
			}
			PhysicalHubs.erase(index);
		}

		void RegisterHubTask(std::uint32_t vm_index, const std::weak_ptr<Implementation>& weak) {
			if (FrameLooper == nullptr) {
				return;
			}
			if (PhysicalHubs.size() == kMaximumInputHubs || NextPhysicalHub == std::numeric_limits<std::uint64_t>::max()) {
				throw std::runtime_error("InputHub actual task capacity is exhausted");
			}
			const auto index = ++NextPhysicalHub;
			auto& wrapper = Hubs.at(vm_index);
			InputHubFrameOwner physical;
			physical.Hub = wrapper.Hub;
			physical.VmIndex = vm_index;
			PhysicalHubs.emplace(index, std::move(physical));
			wrapper.PhysicalOwner = index;
			try {
				RegisterFrameClock(index, false, weak);
				scene::ParallelTaskCallbacks callbacks;
				callbacks.CanRelease = [weak, index](auto handle) {
					const auto owner = weak.lock();
					return owner && owner->CanRetireHub(index, handle);
				};
				callbacks.Release = [weak, index](auto handle) {
					const auto owner = weak.lock();
					if (!owner) {
						throw std::runtime_error("InputHub task lost its actual containing registry");
					}
					owner->RetireHub(index, handle);
				};
				// Native MSingleInputHub Update/Draw are RET; its separate VSync owner samples.
				PhysicalHubs.at(index).Task = DriverParallel->Append(std::move(callbacks));
			} catch (...) {
				RetireFrameClock(index, false);
				PhysicalHubs.erase(index);
				wrapper.PhysicalOwner.reset();
				throw;
			}
		}

		void RetireAllFrameOwners() {
			for (const auto& [index, record] : Devices) {
				static_cast<void>(record);
				RetireFrameClock(index, true);
			}
			while (!PhysicalHubs.empty()) {
				const auto task = *PhysicalHubs.begin()->second.Task;
				if (!DriverParallel->CanExtract(task)) {
					throw std::runtime_error("input adapter retirement requires stable actual Driver tasks");
				}
				auto owned = DriverParallel->Extract(task);
				owned.Release();
			}
		}

		InputHubRecord& HubRecord(const SquirrelValue& instance) {
			if (Vm.Type(instance) != SquirrelValueType::Instance) {
				throw std::runtime_error("InputHub callback requires its native instance receiver");
			}

			const auto found = Hubs.find(std::get<SquirrelObjectHandle>(instance).Index);
			if (found == Hubs.end() || found->second.Instance != Vm.BorrowObjectIdentity(instance) || !found->second.Hub) {
				throw std::runtime_error("InputHub instance has no live native state");
			}

			return found->second;
		}

		InputDeviceRecord& DeviceRecord(const SquirrelValue& instance) {
			if (Vm.Type(instance) != SquirrelValueType::Instance) {
				throw std::runtime_error("Input callback requires its native instance receiver");
			}

			const auto found = Devices.find(std::get<SquirrelObjectHandle>(instance).Index);
			if (found == Devices.end() || found->second.Instance != Vm.BorrowObjectIdentity(instance)) {
				throw std::runtime_error("Input instance has no live native state");
			}

			return found->second;
		}

		input::InputDevice& Device(const SquirrelValue& instance) {
			auto& record = DeviceRecord(instance);
			const auto hub = record.Hub.lock();
			if (!hub) {
				throw std::runtime_error("Input instance's native hub has expired");
			}

			return hub->DeviceAt(record.Index);
		}

		void Construct(const SquirrelValue& instance, const std::weak_ptr<Implementation>& owner) {
			RequireInputMutation();
			if (!Vm.IsInstanceOf(instance, HubClass)) {
				throw std::runtime_error("InputHub constructor requires its native instance receiver");
			}

			if (Hubs.size() == kMaximumInputHubs) {
				throw std::runtime_error("InputHub native instance capacity is exhausted");
			}

			const auto index = std::get<SquirrelObjectHandle>(instance).Index;
			if (Hubs.contains(index)) {
				throw std::runtime_error("InputHub native state is already initialized");
			}

			InputHubRecord record;
			record.Instance = Vm.BorrowObjectIdentity(instance);
			record.Hub = std::make_shared<input::InputHub>();
			Objects.AttachInstance(instance);
			try {
				Hubs.emplace(index, std::move(record));
				ConfigureRelease(instance, owner);
				RegisterHubTask(index, owner);
			} catch (...) {
				Hubs.erase(index);
				Objects.ReleaseInstance(instance);

				throw;
			}
		}

		SquirrelValue InputAt(const SquirrelValue& hub_instance, std::int32_t index, const std::weak_ptr<Implementation>& owner) {
			RequireInputMutation();
			auto& hub_record = HubRecord(hub_instance);
			static_cast<void>(hub_record.Hub->DeviceAt(index));
			if (Devices.size() == kMaximumInputViews) {
				throw std::runtime_error("Input native instance capacity is exhausted");
			}

			const auto instance = Vm.CreateInstance(DeviceClass);
			InputDeviceRecord record;
			record.Instance = Vm.BorrowObjectIdentity(instance);
			record.Hub = hub_record.Hub;
			record.Index = std::min(index, static_cast<std::int32_t>(input::kInputDeviceCount - 1));
			Objects.AttachInstance(instance);
			try {
				Devices.emplace(std::get<SquirrelObjectHandle>(instance).Index, std::move(record));
				ConfigureRelease(instance, owner);
				RegisterFrameClock(std::get<SquirrelObjectHandle>(instance).Index, true, owner);
			} catch (...) {
				RetireFrameClock(std::get<SquirrelObjectHandle>(instance).Index, true);
				Devices.erase(std::get<SquirrelObjectHandle>(instance).Index);
				Objects.ReleaseInstance(instance);

				throw;
			}

			return instance;
		}

		void ConfigureRelease(const SquirrelValue& instance, const std::weak_ptr<Implementation>& owner) {
			Objects.ConfigureNativeRelease(instance, [owner](const SquirrelValue& receiver) {
				if (const auto retained = owner.lock()) {
					retained->Release(receiver);
				}
			});
		}

		void Release(const SquirrelValue& instance) {
			RequireInputMutation();
			Objects.RequireCanRelease(instance);
			const auto identity = Vm.BorrowObjectIdentity(instance);
			const auto hub = Hubs.find(identity.Index);
			const bool is_hub = hub != Hubs.end() && hub->second.Instance == identity;
			if (is_hub) {
				if (hub->second.PhysicalOwner) {
					const auto physical = PhysicalHubs.find(*hub->second.PhysicalOwner);
					if (physical != PhysicalHubs.end()) {
						DriverParallel->SetStatus(*physical->second.Task, scene::TaskStatus::Retire);
					}
				}
			} else {
				static_cast<void>(DeviceRecord(instance));
				RetireFrameClock(identity.Index, true);
			}
			std::exception_ptr failure;
			try {
				Objects.ReleaseInstance(instance);
			} catch (...) {
				failure = std::current_exception();
			}

			const auto index = std::get<SquirrelObjectHandle>(instance).Index;
			if (is_hub) {
				Hubs.erase(index);
			} else {
				Devices.erase(index);
			}

			if (failure) {
				std::rethrow_exception(failure);
			}
		}
	};

	ScriptInputBindings::ScriptInputBindings(SquirrelVm& vm, ScriptObjectBindings& objects, input::KeyMappings& mappings) : m_Implementation(std::make_shared<Implementation>(vm, objects, mappings)) {
		auto& state = *m_Implementation;
		state.HubClass = vm.CreateClass(objects.ObjectClass());
		state.DeviceClass = vm.CreateClass(objects.ObjectClass());
		state.HubClassRoot = vm.Retain(state.HubClass);
		state.DeviceClassRoot = vm.Retain(state.DeviceClass);
		const auto weak = std::weak_ptr<Implementation>(m_Implementation);
		vm.NewSlot(vm.RootTable(), std::string("funcKeyPressed"), vm.CreateNativeClosure("funcKeyPressed", [weak](SquirrelVm& active, std::span<const SquirrelValue> arguments) {
			const auto owner = weak.lock();
			if (!owner || &active != &owner->Vm || !owner->ReadFunctionKeys || owner->ReadingFunctionKeys) {
				throw std::runtime_error("funcKeyPressed requires its live nonrecursive actual input snapshot provider");
			}
			const auto mask = arguments.size() > 1 ? TryNativeInteger(arguments[1]).value_or(0) : 0;
			owner->ReadingFunctionKeys = true;
			try {
				const auto sample = owner->ReadFunctionKeys();
				owner->ReadingFunctionKeys = false;

				return SquirrelNativeResult::ReturnValue(std::bit_cast<std::int32_t>(sample.Pressed(std::bit_cast<std::uint32_t>(mask))));
			} catch (...) {
				owner->ReadingFunctionKeys = false;
				throw;
			}
		}));

		vm.NewSlot(vm.RootTable(), std::string("altEnterPressed"), vm.CreateNativeClosure("altEnterPressed", [weak](SquirrelVm& active, std::span<const SquirrelValue>) {
			const auto owner = weak.lock();
			if (!owner || &active != &owner->Vm || !owner->ReadFunctionKeys || owner->ReadingFunctionKeys) {
				throw std::runtime_error("altEnterPressed requires its live nonrecursive actual input snapshot provider");
			}
			owner->ReadingFunctionKeys = true;
			try {
				const auto sample = owner->ReadFunctionKeys();
				owner->ReadingFunctionKeys = false;
				return SquirrelNativeResult::ReturnValue(sample.AltEnterPressed());
			} catch (...) {
				owner->ReadingFunctionKeys = false;
				throw;
			}
		}));

		for (const auto name : {"getWMChar", "getDownKey"}) {
			const bool characters = std::string_view(name) == "getWMChar";
			vm.NewSlot(vm.RootTable(), std::string(name), vm.CreateNativeClosure(name, [weak, characters](SquirrelVm& active, std::span<const SquirrelValue>) {
				const auto owner = weak.lock();
				if (!owner || &active != &owner->Vm || !owner->ReadKeyboardText || owner->ReadingFunctionKeys) {
					throw std::runtime_error("keyboard text getter requires its live nonrecursive actual input snapshot provider");
				}
				owner->ReadingFunctionKeys = true;
				try {
					const auto sample = owner->ReadKeyboardText();
					SquirrelValue value = sample.DownKey;
					if (characters) {
						// Native045180 passes length -1; the VM observes the first NUL, not the buffer extent.
						value = std::string(sample.Characters.substr(0, sample.Characters.find(char{0})));
					}
					owner->ReadingFunctionKeys = false;
					return SquirrelNativeResult::ReturnValue(std::move(value));
				} catch (...) {
					owner->ReadingFunctionKeys = false;
					throw;
				}
			}));
		}

		const auto method = [&](const SquirrelValue& target_class, std::string name, auto operation, bool is_static = false) {
			const auto closure = vm.CreateNativeClosure("Input." + name, [weak, operation](SquirrelVm&, std::span<const SquirrelValue> arguments) {
				const auto owner = weak.lock();
				if (!owner || arguments.empty()) {
					throw std::runtime_error("Input callback owner has expired or lacks its receiver");
				}

				SquirrelNativeResult result;
				result.Value = operation(*owner, arguments);

				return result;
			});
			vm.NewSlot(target_class, name, closure, is_static);
		};

		method(state.HubClass, "constructor", [weak](Implementation& owner, const auto arguments) -> SquirrelValue {
			owner.Construct(arguments[0], weak);

			return {};
		});
		method(state.HubClass, "getInputNum", [](Implementation& owner, const auto arguments) -> SquirrelValue {
			return static_cast<std::int32_t>(owner.HubRecord(arguments[0]).Hub->DeviceCount());
		});
		method(state.HubClass, "inputAt", [weak](Implementation& owner, const auto arguments) {
			const auto index = arguments.size() > 1 ? TryNativeInteger(arguments[1]) : std::optional<std::int32_t>();
			if (!index) {
				throw std::runtime_error("InputHub.inputAt requires an integer-convertible device index");
			}

			return owner.InputAt(arguments[0], *index, weak);
		});
		for (const bool right : {false, true}) {
			method(state.HubClass, right ? "setTransferRightAnalogToDigital" : "setTransferAnalogToDigital", [right](Implementation& owner, const auto arguments) -> SquirrelValue {
				auto& hub = *owner.HubRecord(arguments[0]).Hub;
				const auto enabled = arguments.size() > 1 && InputBoolean(arguments[1]);
				if (right) {
					hub.SetTransferRightAnalogToDigital(enabled);
				} else {
					hub.SetTransferLeftAnalogToDigital(enabled);
				}

				return {};
			});
			method(state.HubClass, right ? "getTransferRightAnalogToDigital" : "getTransferAnalogToDigital", [right](Implementation& owner, const auto arguments) -> SquirrelValue {
				const auto& hub = *owner.HubRecord(arguments[0]).Hub;

				return right ? hub.TransferRightAnalogToDigital() : hub.TransferLeftAnalogToDigital();
			});
		}

		// These setters are literal RET bodies in this desktop target; their getters remain fixed.
		for (const auto name : {"setAnalogPressSensitivity", "setAnalogToDigitalDeadZone", "enableFrontTouchPad"}) {
			method(state.HubClass, name, [](Implementation& owner, const auto arguments) -> SquirrelValue {
				static_cast<void>(owner.HubRecord(arguments[0]));

				return {};
			});
		}

		method(state.HubClass, "getAnalogPressSensitivity", [](Implementation& owner, const auto arguments) -> SquirrelValue {
			static_cast<void>(owner.HubRecord(arguments[0]));

			return std::int32_t(0);
		});
		method(state.HubClass, "getAnalogToDigitalDeadZone", [](Implementation& owner, const auto arguments) -> SquirrelValue {
			static_cast<void>(owner.HubRecord(arguments[0]));

			return 0.0f;
		});

		method(state.HubClass, "ClearKeyConv", [](Implementation& owner, const auto) -> SquirrelValue {
			owner.Mappings.Clear();

			return {};
		},
		    true);
		method(state.HubClass, "DefaultKeyConv", [](Implementation& owner, const auto) -> SquirrelValue {
			owner.Mappings.SetDefault();

			return {};
		},
		    true);
		method(state.HubClass, "AddKeyConv", [](Implementation& owner, const auto arguments) -> SquirrelValue {
			if (arguments.size() != 4) {
				throw std::runtime_error("InputHub.AddKeyConv requires source, logical target, and controller target");
			}

			return owner.Mappings.Add(owner.Vm.ToString(arguments[1]), owner.Vm.ToString(arguments[2]), owner.Vm.ToString(arguments[3]));
		},
		    true);
		method(state.DeviceClass, "constructor", [](Implementation&, const auto) -> SquirrelValue {
			throw std::runtime_error("can't create instance of Input; use InputHub.inputAt");
		});
		for (std::uint32_t query = 0; query < 3; ++query) {
			method(state.DeviceClass, query == 0 ? "key" : query == 1 ? "keyPressed"
			                                                          : "keyReleased",
			    [query](Implementation& owner, const auto arguments) -> SquirrelValue {
				    const auto& device = owner.Device(arguments[0]);
				    const auto mask = std::bit_cast<std::uint32_t>(InputInteger(arguments, 1));
				    const auto value = query == 0 ? device.Buttons() & mask : query == 1 ? device.Pressed(mask)
				                                                                         : device.Released(mask);

				    return std::bit_cast<std::int32_t>(value);
			    });
		}

		method(state.DeviceClass, "isComboKeyPressed", [](Implementation& owner, const auto arguments) -> SquirrelValue {
			return owner.Device(arguments[0]).ComboPressed(std::bit_cast<std::uint32_t>(InputInteger(arguments, 1)));
		});
		method(state.DeviceClass, "getConnected", [](Implementation& owner, const auto arguments) -> SquirrelValue {
			return owner.Device(arguments[0]).Connected();
		});
		method(state.DeviceClass, "getControllerType", [](Implementation& owner, const auto arguments) -> SquirrelValue {
			return static_cast<std::int32_t>(owner.Device(arguments[0]).Type());
		});
		for (const auto name : {"disconnect", "setTouchPadEnabled", "setEnableSoftKeyPad", "setAccelerometerEnabled"}) {
			method(state.DeviceClass, name, [](Implementation& owner, const auto arguments) -> SquirrelValue {
				static_cast<void>(owner.DeviceRecord(arguments[0]));

				return {};
			});
		}

		method(state.DeviceClass, "getTouchPadEnabled", [](Implementation& owner, const auto arguments) -> SquirrelValue {
			static_cast<void>(owner.DeviceRecord(arguments[0]));

			return false;
		});
		method(state.DeviceClass, "getAccelerometerEnabled", [](Implementation& owner, const auto arguments) -> SquirrelValue {
			static_cast<void>(owner.DeviceRecord(arguments[0]));

			return true;
		});
		for (const auto name : {"getHorizonRad", "getHorizonDeg", "getAccelerometerX", "getAccelerometerY", "getAccelerometerZ"}) {
			const auto vertical = std::string_view(name) == "getAccelerometerZ";
			method(state.DeviceClass, name, [vertical](Implementation& owner, const auto arguments) -> SquirrelValue {
				static_cast<void>(owner.DeviceRecord(arguments[0]));

				return vertical ? std::bit_cast<float>(std::uint32_t(0x411ce80a)) : 0.0f;
			});
		}
		method(state.DeviceClass, "getStick", [](Implementation& owner, const auto arguments) -> SquirrelValue {
			return owner.Device(arguments[0]).StickDirection();
		});
		for (const bool right : {false, true}) {
			for (const bool vertical : {false, true}) {
				const auto name = std::string(right ? "getRightAnalogStick" : "getAnalogStick") + (vertical ? "Y" : "X");
				method(state.DeviceClass, name, [right, vertical](Implementation& owner, const auto arguments) -> SquirrelValue {
					const auto& device = owner.Device(arguments[0]);
					const auto point = right ? device.RightStick() : device.LeftStick();

					return vertical ? point.Y : point.X;
				});
			}
		}

		for (const bool vertical : {false, true}) {
			method(state.DeviceClass, vertical ? "getTouchPointY" : "getTouchPointX", [vertical](Implementation& owner, const auto arguments) -> SquirrelValue {
				const auto point = owner.Device(arguments[0]).Cursor();

				return vertical ? point.Y : point.X;
			});
		}

		method(state.DeviceClass, "setKeyRepeat", [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto mask = std::bit_cast<std::uint32_t>(InputInteger(arguments, 1));
			const auto initial = static_cast<std::uint64_t>(static_cast<std::int64_t>(InputInteger(arguments, 2)));
			const auto interval = static_cast<std::uint64_t>(static_cast<std::int64_t>(InputInteger(arguments, 3)));
			owner.Device(arguments[0]).SetButtonRepeat(mask, initial, interval);

			return {};
		});
		for (const bool movement : {false, true}) {
			method(state.DeviceClass, movement ? "getMouseMove" : "getMousePos", [movement](Implementation& owner, const auto arguments) {
				const auto& mouse = owner.DeviceRecord(arguments[0]).Mouse;
				const auto point = movement ? mouse.Movement() : mouse.Position();
				const auto result = owner.Vm.CreateTable();
				owner.Vm.NewSlot(result, std::string("x"), point.X);
				owner.Vm.NewSlot(result, std::string("y"), point.Y);

				return result;
			});
		}

		const std::array<std::string_view, 3> mouse_buttons{"getMouseButton", "getMouseButtonPressed", "getMouseButtonReleased"};
		const std::array<std::string_view, 3> mouse_enter{"isMouseEnter", "getMouseEntered", "getMouseLeaved"};
		for (std::uint32_t query = 0; query < 3; ++query) {
			method(state.DeviceClass, std::string(mouse_buttons[query]), [query](Implementation& owner, const auto arguments) -> SquirrelValue {
				const auto& mouse = owner.DeviceRecord(arguments[0]).Mouse;
				const auto value = query == 0 ? mouse.Buttons() : query == 1 ? mouse.Pressed()
				                                                             : mouse.Released();

				return std::bit_cast<std::int32_t>(value);
			});
			method(state.DeviceClass, std::string(mouse_enter[query]), [query](Implementation& owner, const auto arguments) -> SquirrelValue {
				const auto& mouse = owner.DeviceRecord(arguments[0]).Mouse;

				return query == 0 ? mouse.Inside() : query == 1 ? mouse.Entered()
				                                                : mouse.Left();
			});
		}

		method(state.DeviceClass, "getMouseWheel", [](Implementation& owner, const auto arguments) -> SquirrelValue {
			return owner.DeviceRecord(arguments[0]).Mouse.WheelDirection();
		});
		method(state.DeviceClass, "setMousePos", [](Implementation& owner, const auto arguments) -> SquirrelValue {
			static_cast<void>(owner.DeviceRecord(arguments[0]));
			if (!owner.ReadClient || !owner.RequestCursor) {
				throw std::runtime_error("native mouse position requests require a configured platform boundary");
			}

			const input::InputPoint point{InputFloat(arguments, 1), InputFloat(arguments, 2)};
			owner.RequestCursor(input::PlanMousePosition(owner.ReadClient(), point));

			return {};
		});

		const auto keys = vm.CreateTable();
		const std::array<std::pair<std::string_view, std::uint32_t>, 28> constants{{
		    {"UP", 0x40},
		    {"DOWN", 0x80},
		    {"LEFT", 0x20},
		    {"RIGHT", 0x10},
		    {"A", 0x1},
		    {"B", 0x2},
		    {"C", 0x4000},
		    {"R", 0x100},
		    {"L", 0x200},
		    {"R2", 0x20000},
		    {"L2", 0x10000},
		    {"R3", 0x80000},
		    {"L3", 0x40000},
		    {"X", 0x400},
		    {"Y", 0x800},
		    {"Z", 0x8000},
		    {"SELECT", 0x4},
		    {"START", 0x8},
		    {"BACK", 0x100000},
		    {"TOUCH", 0x1000},
		    {"HOME", 0x2000},
		    {"R_UP", 0x800000},
		    {"R_DOWN", 0x1000000},
		    {"R_LEFT", 0x400000},
		    {"R_RIGHT", 0x200000},
		    {"DIRECTION", 0xf0},
		    {"R_DIRECTION", 0x1e00000},
		    {"BUTTON", 0x1e1fff0f},
		}};
		for (const auto& [name, value] : constants) {
			vm.NewSlot(keys, std::string(name), std::bit_cast<std::int32_t>(value));
		}

		vm.NewSlot(keys, std::string("ALL"), std::int32_t(0x1fffffff));
		const auto types = vm.CreateTable();
		const std::array<std::string_view, 7> type_names{"XBOX", "STEAM", "PS4", "PS5", "SWITCH", "GENERIC", "UNKNOWN"};
		for (std::size_t index = 0; index < type_names.size(); ++index) {
			vm.NewSlot(types, std::string(type_names[index]), static_cast<std::int32_t>(index));
		}

		vm.NewSlot(vm.ConstantTable(), std::string("KEY"), keys);
		vm.NewSlot(vm.ConstantTable(), std::string("ControllerType"), types);
		vm.NewSlot(vm.RootTable(), std::string("InputHub"), state.HubClass);
		vm.NewSlot(vm.RootTable(), std::string("Input"), state.DeviceClass);
	}

	ScriptInputBindings::~ScriptInputBindings() {
		try {
			m_Implementation->RetireAllFrameOwners();
		} catch (...) {
			std::terminate();
		}
	}

	SquirrelValue ScriptInputBindings::InputHubClass() const {
		return m_Implementation->HubClass;
	}

	SquirrelValue ScriptInputBindings::InputClass() const {
		return m_Implementation->DeviceClass;
	}

	input::InputHub& ScriptInputBindings::Hub(const SquirrelValue& instance) {
		return *m_Implementation->HubRecord(instance).Hub;
	}

	input::InputDevice& ScriptInputBindings::Device(const SquirrelValue& instance) {
		return m_Implementation->Device(instance);
	}

	input::MouseInput& ScriptInputBindings::Mouse(const SquirrelValue& instance) {
		return m_Implementation->DeviceRecord(instance).Mouse;
	}

	void ScriptInputBindings::ConfigureCursorRequests(ClientReader client, CursorRequest request) {
		if (!client || !request) {
			throw std::runtime_error("mouse position boundary requires a client reader and request sink");
		}

		m_Implementation->ReadClient = std::move(client);
		m_Implementation->RequestCursor = std::move(request);
	}

	void ScriptInputBindings::ReleaseInstance(const SquirrelValue& instance) {
		m_Implementation->Release(instance);
	}

	void ScriptInputBindings::ConfigureFunctionKeys(FunctionKeyReader actual_snapshot) {
		auto& state = *m_Implementation;
		if (!actual_snapshot || state.ReadFunctionKeys || state.ReadingFunctionKeys) {
			throw std::runtime_error("function-key provider requires one actual immutable input owner binding");
		}
		state.ReadFunctionKeys = std::move(actual_snapshot);
	}

	void ScriptInputBindings::ConfigureKeyboardText(KeyboardTextReader actual_snapshot) {
		auto& state = *m_Implementation;
		state.RequireInputMutation();
		if (!actual_snapshot || state.ReadKeyboardText || state.ReadingFunctionKeys) {
			throw std::runtime_error("keyboard text requires one actual immutable snapshot provider");
		}
		state.ReadKeyboardText = std::move(actual_snapshot);
	}

	void ScriptInputBindings::ConfigureFrameOwners(core::LooperFrame& actual_looper, scene::ParallelTaskSet& actual_driver_parallel, HubFrameReader actual_hub, MouseFrameReader actual_mouse) {
		auto& state = *m_Implementation;
		state.RequireInputMutation();
		if (state.FrameLooper != nullptr || !state.Hubs.empty() || !state.Devices.empty() || !actual_hub || !actual_mouse) {
			throw std::runtime_error("input frame services require one actual Looper binding before instance construction");
		}
		actual_looper.Group(core::LooperFrameGroup::VSync).RequireIdleTraversal();
		actual_driver_parallel.RequireIdleTraversal();
		state.DriverParallel = &actual_driver_parallel;
		state.ReadHubFrame = std::move(actual_hub);
		state.ReadMouseFrame = std::move(actual_mouse);
		state.FrameLooper = &actual_looper;
	}

} // namespace sgre::script
