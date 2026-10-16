#include "sgre/script/squirrel_vm.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

using sgre::script::BytecodeProgram;
using sgre::script::SquirrelInstruction;
using sgre::script::SquirrelNativeResult;
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

static BytecodeProgram Program(std::vector<SquirrelInstruction> instructions, bool parent = false) {
	BytecodeProgram program;
	program.MainFunction.SourceName = std::string("independent-delegated-table-default-fixture");
	program.MainFunction.Name = std::string("main");
	program.MainFunction.Parameters = {std::string("this"), std::string("target")};
	if (parent) {
		program.MainFunction.Parameters.push_back(std::string("parent"));
	}

	program.MainFunction.StackSize = 4;
	program.MainFunction.Instructions = std::move(instructions);

	return program;
}

static void TestDefaultPrecedesDelegatedGetter(bool userdata, bool bytecode) {
	std::int32_t getter_calls = 0;
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	const auto first = vm.CreateTable();
	const auto second = vm.CreateTable();
	const std::array<SquirrelValue, 3> delegate_arguments{vm.RootTable(), first, second};
	static_cast<void>(vm.InvokeWithoutResultInContext(vm.RootContext(), vm.CreateClosure(Program({Instruction(53, 3, 1, 2), Instruction(19, 255)}, true)), delegate_arguments));
	const auto getter = vm.CreateNativeClosure("delegated_getter_must_follow_default", [&](SquirrelVm&, std::span<const SquirrelValue>) {
		++getter_calls;

		return SquirrelNativeResult::ReturnValue(std::int32_t{77});
	});
	vm.NewSlot(second, std::string("_get"), getter);
	vm.NewSlot(first, std::string("_get"), getter);
	SquirrelValue receiver = first;
	if (userdata) {
		const std::array<std::uint8_t, 1> bytes{37};
		receiver = vm.CreateUserData(bytes);
		vm.SetUserDataDelegate(receiver, first);
	}

	const auto default_method = vm.ReadSlot(vm.CreateTable(), std::string("weakref"));
	SquirrelValue selected;
	if (bytecode) {
		auto program = Program({Instruction(9, 3, 0, 1), Instruction(19, 1, 3)});
		program.MainFunction.Literals.push_back(std::string("weakref"));
		const std::array<SquirrelValue, 2> arguments{vm.RootTable(), receiver};
		selected = vm.Invoke(vm.CreateClosure(std::move(program)), arguments).Value;
	} else {
		selected = vm.ReadSlot(receiver, std::string("weakref"));
	}

	Require(selected == default_method && getter_calls == 0, "actual delegated Table type default must win before any enclosing Table/userdata getter");
	const std::array<SquirrelValue, 1> arguments{receiver};
	const auto weak = vm.Invoke(selected, arguments).Value;
	Require(vm.Type(weak) == SquirrelValueType::WeakReference && vm.ResolveWeakReference(weak) == receiver, "selected default method must remain the real registered callback and receive the original Table/userdata receiver");
	vm.NewSlot(first, std::string("weakref"), std::int32_t{73});
	Require(std::get<std::int32_t>(vm.ReadSlot(receiver, std::string("weakref"))) == 73 && getter_calls == 0, "direct delegate slot must continue winning over its shared Table default");
	if (userdata) {
		Require(std::get<std::int32_t>(vm.ReadSlot(receiver, std::string("missing"))) == 77 && getter_calls == 1, "recovered recursive userdata getter must execute the first delegated Table getter when its default is absent");
	} else {
		Require(std::get<std::int32_t>(vm.ReadSlot(receiver, std::string("missing"))) == 77 && getter_calls == 1, "ordinary delegated Table getter must still run when its requested default is absent");
	}

	vm.Close();
	vm.RethrowReleaseFailure();
}

int main() {
	try {
		for (const auto userdata : {false, true}) {
			for (const auto bytecode : {false, true}) {
				TestDefaultPrecedesDelegatedGetter(userdata, bytecode);
			}
		}

		std::cout << "four independent delegated Table default fixture groups pass\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
