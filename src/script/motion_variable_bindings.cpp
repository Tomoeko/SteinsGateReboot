#include "sgre/script/motion_variable_bindings.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace sgre::script {
	enum class MotionVariableMethod {
		Set,
		Get,
		Keys,
		Animate
	};

	static float VariableNumber(const SquirrelValue& value) {
		if (const auto* real = std::get_if<float>(&value)) {
			return *real;
		}
		if (const auto* integer = std::get_if<std::int32_t>(&value)) {
			return static_cast<float>(*integer);
		}
		if (const auto* boolean = std::get_if<bool>(&value)) {
			return *boolean ? 1.0f : 0.0f;
		}

		return 0;
	}

	class ScriptMotionVariables::Implementation {
	public:
		Implementation(SquirrelVm& vm, ScriptMotionNativeOwners& owners)
		    : Vm(vm), Owners(owners) {
		}

		SquirrelVm& Vm;
		ScriptMotionNativeOwners& Owners;

		void RequireReceiver(const SquirrelValue& instance) const {
			if (Vm.Type(instance) != SquirrelValueType::Instance || !Owners.Contains(instance)) {
				throw std::runtime_error("bad instance");
			}
		}

		std::string Name(const SquirrelValue& value) {
			auto name = Vm.ToString(value);
			name.resize(name.find('\0') == std::string::npos ? name.size() : name.find('\0'));

			return name;
		}

		SquirrelNativeResult Dispatch(MotionVariableMethod method, std::span<const SquirrelValue> arguments) {
			if (arguments.empty()) {
				throw std::runtime_error("bad instance");
			}
			const auto instance = arguments[0];
			RequireReceiver(instance);
			if (method == MotionVariableMethod::Keys) {
				const auto keys = Owners.IsDetached(instance) ? std::vector<std::string>{} : Owners.Commands(instance).ParameterValues().RegisteredKeys();
				std::vector<SquirrelValue> values;
				values.reserve(keys.size());
				for (const auto& key : keys) {
					values.push_back(Vm.CreateString(key));
				}
				const auto output = Vm.CreateArray(values);

				return SquirrelNativeResult::ReturnValue(output);
			}
			if (method == MotionVariableMethod::Animate) {
				// Native2727d0 converts fixed5/4/3 first, then stringifies2; the capture remains on the stack.
				const auto curve = TryNativeInteger(Vm.NativeAllocatedStackValue(5)).value_or(0);
				const auto duration = TryNativeInteger(Vm.NativeAllocatedStackValue(4)).value_or(0);
				const auto value = VariableNumber(Vm.NativeAllocatedStackValue(3));
				const auto input = Vm.NativeAllocatedStackValue(2);
				const auto name = Name(input);
				RequireReceiver(instance);
				if (!Owners.IsDetached(instance)) {
					Owners.AnimateVariable(instance, name, value, duration, curve);
				}

				return SquirrelNativeResult::NoValues();
			}
			if (method == MotionVariableMethod::Get) {
				// Native271ed0 converts physical arg2 before the typed target's detached check, without arity admission.
				const auto input = Vm.NativeAllocatedStackValue(2);
				const auto name = Name(input);
				RequireReceiver(instance);

				return SquirrelNativeResult::ReturnValue(Owners.IsDetached(instance) ? 0.0f : Owners.Commands(instance).ParameterValues().ReadLocal(name));
			}

			// Native280af0 returns immediately for a detached target, before either count or tostring evaluation.
			if (Owners.IsDetached(instance)) {
				return SquirrelNativeResult::NoValues();
			}
			if (arguments.size() != 3 && arguments.size() != 4) {
				throw std::runtime_error("invalid argument.");
			}
			// Tostring can grow the VM operand storage or run script. Keep every subsequently read operand by value.
			const auto name_input = arguments[1];
			const auto value_input = arguments[2];
			const auto flags_input = arguments.size() == 4 ? arguments[3] : SquirrelValue{};
			const auto name = Name(name_input);
			const auto value = VariableNumber(value_input);
			const auto flags = static_cast<std::uint32_t>(TryNativeInteger(flags_input).value_or(0));
			RequireReceiver(instance);
			if (!Owners.IsDetached(instance)) {
				Owners.SetVariable(instance, name, value, flags);
			}

			return SquirrelNativeResult::NoValues();
		}
	};

	ScriptMotionVariables::ScriptMotionVariables(SquirrelVm& vm, const SquirrelValue& actual_motion_class, ScriptMotionNativeOwners& actual_owners)
	    : m_Implementation(std::make_shared<Implementation>(vm, actual_owners)) {
		if (vm.Type(actual_motion_class) != SquirrelValueType::Class) {
			throw std::runtime_error("Motion variables require the actual registered class");
		}
		const std::array<std::pair<std::string_view, MotionVariableMethod>, 4> methods{{{"setVariable", MotionVariableMethod::Set}, {"getVariable", MotionVariableMethod::Get}, {"getVariableKeys", MotionVariableMethod::Keys}, {"animateVariable", MotionVariableMethod::Animate}}};
		const std::weak_ptr<Implementation> weak = m_Implementation;
		for (const auto& [name, method] : methods) {
			// Original278fdc/279079/279113 each install a genuine16-byte member descriptor as captured userdata.
			std::array<std::uint8_t, 16> descriptor{};
			descriptor[0] = static_cast<std::uint8_t>(method);
			const std::array<SquirrelValue, 1> captures{vm.CreateUserData(descriptor)};
			const auto callback = vm.CreateNativeClosureWithCaptures(
			    "Motion." + std::string(name), [weak, method, descriptor](SquirrelVm& active, auto arguments) {
				    const auto owner = weak.lock();
				    if (!owner) {
					    throw std::runtime_error("Motion variable owner has expired");
				    }
				    const auto payload = active.UserDataBytes(active.NativeStackValue(-1));
				    if (&active != &owner->Vm || payload.size() != descriptor.size() || !std::equal(payload.begin(), payload.end(), descriptor.begin())) {
					    throw std::runtime_error("Motion variable descriptor does not match its actual typed operation");
				    }

				    if (method == MotionVariableMethod::Set || method == MotionVariableMethod::Keys) {
					    // Native078ed0 removes the descriptor before invoking each raw-VM member target.
					    active.SetNativeStackSize(active.NativeStackSize() - 1);
					    arguments = arguments.first(arguments.size() - 1);
				    }

				    return owner->Dispatch(method, arguments);
			    },
			    captures);
			vm.NewSlot(actual_motion_class, std::string(name), callback);
		}
	}

	ScriptMotionVariables::~ScriptMotionVariables() = default;
} // namespace sgre::script
