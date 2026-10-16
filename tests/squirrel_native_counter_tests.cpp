#include "sgre/script/squirrel_vm.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <variant>

using sgre::script::BytecodeProgram;
using sgre::script::SquirrelInstruction;
using sgre::script::SquirrelNativeResult;
using sgre::script::SquirrelRunState;
using sgre::script::SquirrelValue;
using sgre::script::SquirrelValueType;
using sgre::script::SquirrelVm;

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static SquirrelInstruction Instruction(std::uint8_t opcode, std::uint8_t first, std::int32_t second = 0, std::uint8_t third = 0) {
	SquirrelInstruction instruction;
	instruction.Opcode = opcode;
	instruction.Argument0 = first;
	instruction.Argument1 = second;
	instruction.Argument2 = third;

	return instruction;
}

static BytecodeProgram Program() {
	BytecodeProgram program;
	program.MainFunction.SourceName = std::string("independent-native-counter-retirement-fixture");
	program.MainFunction.Name = std::string("main");
	program.MainFunction.Parameters = {std::string("this"), std::string("target")};
	program.MainFunction.StackSize = 4;
	program.MainFunction.Literals = {std::int32_t{0}, std::int32_t{1}};
	program.MainFunction.Instructions = {Instruction(9, 3, 0, 1), Instruction(20, 3, 1), Instruction(9, 3, 1, 1), Instruction(19, 1, 3)};

	return program;
}

static void TestOutputRetirementUsesReturnedCallbackCounter(std::int32_t status) {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	vm.InstallRecoveredBaseLibrary();
	std::int32_t getter_calls = 0;
	std::int32_t release_calls = 0;
	bool guard_completed = false;
	SquirrelValue weak;
	const auto class_value = vm.CreateClass();
	const auto getter = vm.CreateNativeClosure("counter_output_getter", [&](SquirrelVm& active, std::span<const SquirrelValue> arguments) {
		++getter_calls;
		if (std::get<std::int32_t>(arguments[1]) == 0) {
			const std::array<std::uint8_t, 1> bytes{37};
			const auto owner = active.CreateUserData(bytes);
			weak = active.CreateWeakReference(owner);
			active.SetUserDataReleaseHook(owner, [&](SquirrelVm& releasing, const SquirrelValue&, std::span<std::uint8_t>) {
				++release_calls;
				Require(releasing.Type(releasing.ResolveWeakReference(weak)) == SquirrelValueType::UserData, "output owner's weak target must remain live during its actual last-reference callback");
				const std::array<SquirrelValue, 1> arguments{std::int32_t{17}};
				const auto result = releasing.InvokeInContext(releasing.CurrentContext(), releasing.GetSlot(releasing.RootTable(), std::string("suspend")), arguments, 64, false);
				Require(result.State == SquirrelRunState::Completed && std::get<std::int32_t>(result.Value) == 17 && !releasing.IsSuspended(), "generic base-suspend call from output cleanup must use the returned getter's retired counter while preserving actual Execute depth");
				guard_completed = true;
			});

			return SquirrelNativeResult::ReturnValue(owner);
		}

		if (status != 0) {
			active.PushNativeValue(std::int32_t{77});
		}

		return SquirrelNativeResult::StackStatus(status);
	});
	vm.NewSlot(class_value, std::string("_get"), getter);
	const auto instance = vm.CreateInstance(class_value);
	const std::array<SquirrelValue, 2> arguments{vm.RootTable(), instance};
	const auto result = vm.Invoke(vm.CreateClosure(Program()), arguments);
	vm.RethrowReleaseFailure();
	Require(result.State == SquirrelRunState::Completed && !vm.IsSuspended() && getter_calls == 2 && release_calls == 1 && guard_completed, "getter output replacement must release its prior owner after callback depth retirement and preserve caller execution");
	Require(vm.Type(vm.ResolveWeakReference(weak)) == SquirrelValueType::Null, "retired output owner must expire after its successful callback");
	if (status == 0) {
		Require(vm.Type(result.Value) == SquirrelValueType::Null, "native zero return must replace output with null after counter retirement");
	} else {
		Require(std::get<std::int32_t>(result.Value) == 77, "positive and raw suspension statuses must retain the actual getter output after cleanup reentry");
	}

	vm.Close();
	vm.RethrowReleaseFailure();
}

int main() {
	try {
		for (const auto status : {0, 1, 2, -666}) {
			TestOutputRetirementUsesReturnedCallbackCounter(status);
		}

		std::cout << "four independent native counter retirement fixture groups pass\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
