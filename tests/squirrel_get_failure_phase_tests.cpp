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
	program.MainFunction.SourceName = std::string("independent-get-failure-phase-fixture");
	program.MainFunction.Name = std::string("main");
	program.MainFunction.Parameters = {std::string("this"), std::string("target")};
	program.MainFunction.StackSize = 4;
	program.MainFunction.Literals = {std::int32_t{0}, std::string("weakref")};
	program.MainFunction.Instructions = {Instruction(9, 3, 0, 1), Instruction(20, 3, 1), Instruction(9, 3, 1, 1), Instruction(19, 1, 3)};

	return program;
}

static void TestFailedGetterRetiresFrameBeforeDefault(std::int32_t status) {
	std::int32_t getter_calls = 0;
	std::int32_t release_calls = 0;
	SquirrelValue weak;
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	vm.InstallRecoveredBaseLibrary();
	const auto class_value = vm.CreateClass();
	const auto getter = vm.CreateNativeClosure("failure_phase_getter", [&](SquirrelVm& active, std::span<const SquirrelValue> arguments) {
		++getter_calls;
		if (const auto* integer = std::get_if<std::int32_t>(&arguments[1]); integer && *integer == 0) {
			const std::array<std::uint8_t, 1> bytes{37};
			const auto owner = active.CreateUserData(bytes);
			weak = active.CreateWeakReference(owner);
			active.SetUserDataReleaseHook(owner, [&](SquirrelVm& releasing, const SquirrelValue&, std::span<std::uint8_t> payload) {
				++release_calls;
				Require(payload.size() == 1 && payload[0] == 37 && releasing.Type(releasing.ResolveWeakReference(weak)) == SquirrelValueType::UserData, "getter output cleanup must observe the original initialized live payload");
				bool frame_active = true;
				std::size_t count = 0;
				try {
					count = releasing.NativeStackSize();
				} catch (const std::runtime_error& error) {
					Require(std::string(error.what()).find("active native frame") != std::string::npos, "native stack control failed for an unrelated reason");
					frame_active = false;
				}

				Require(status == 0 ? frame_active && count == 2 : !frame_active, "failed getter must retire its native frame before default output replacement while successful zero-result cleanup keeps its frame");
			});

			return SquirrelNativeResult::ReturnValue(owner);
		}

		Require(sgre::script::StringText(arguments[1]) == "weakref", "second getter must receive the actual instance-default key");
		if (status == -1) {
			active.SetLastError(std::int32_t{-41});
		}

		return SquirrelNativeResult::StackStatus(status);
	});
	vm.NewSlot(class_value, std::string("_get"), getter);
	const auto instance = vm.CreateInstance(class_value);
	const std::array<SquirrelValue, 2> arguments{vm.RootTable(), instance};
	const auto result = vm.Invoke(vm.CreateClosure(Program()), arguments);
	vm.RethrowReleaseFailure();
	Require(getter_calls == 2 && release_calls == 1 && vm.Type(vm.ResolveWeakReference(weak)) == SquirrelValueType::Null, "default recovery must retire the old output once and preserve original bytecode execution");
	if (status == 0) {
		Require(vm.Type(result.Value) == SquirrelValueType::Null, "successful zero-result getter must return null without instance-default retry");
	} else {
		Require(result.Value == vm.GetSlot(instance, std::string("weakref")) && std::get<std::int32_t>(vm.LastError()) == -41, "failed getter must return the real registered instance default while preserving its previous error");
	}

	vm.Close();
	vm.RethrowReleaseFailure();
}

int main() {
	try {
		TestFailedGetterRetiresFrameBeforeDefault(0);
		TestFailedGetterRetiresFrameBeforeDefault(-1);
		std::cout << "two independent getter failure cleanup fixture groups pass\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
