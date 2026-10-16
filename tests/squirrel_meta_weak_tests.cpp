#include "sgre/script/squirrel_vm.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

using sgre::script::BytecodeProgram;
using sgre::script::SquirrelInstruction;
using sgre::script::SquirrelNativeResult;
using sgre::script::SquirrelValue;
using sgre::script::SquirrelValueType;
using sgre::script::SquirrelVm;

enum class WeakState {
	Live,
	Expired,
	Nested
};

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
	program.MainFunction.SourceName = std::string("independent-metamethod-weak-slot-fixture");
	program.MainFunction.Name = std::string("main");
	program.MainFunction.Parameters = {std::string("this"), std::string("target")};
	if (parent) {
		program.MainFunction.Parameters.push_back(std::string("parent"));
	}

	program.MainFunction.StackSize = 4;
	program.MainFunction.Instructions = std::move(instructions);

	return program;
}

static void TestMetamethodResolvesOneWeakSlot(bool userdata, WeakState state, bool bytecode) {
	std::int32_t calls = 0;
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	const auto delegate = vm.CreateTable();
	SquirrelValue receiver;
	if (userdata) {
		const std::array<std::uint8_t, 1> bytes{37};
		receiver = vm.CreateUserData(bytes);
		vm.SetUserDataDelegate(receiver, delegate);
	} else {
		receiver = vm.CreateTable();
		const std::array<SquirrelValue, 3> arguments{vm.RootTable(), receiver, delegate};
		static_cast<void>(vm.InvokeWithoutResultInContext(vm.RootContext(), vm.CreateClosure(Program({Instruction(53, 3, 1, 2), Instruction(19, 255)}, true)), arguments));
	}

	const auto identity = vm.BorrowObjectIdentity(receiver);
	SquirrelValue method = vm.CreateNativeClosure("weak_slot_getter", [&](SquirrelVm& active, std::span<const SquirrelValue> arguments) {
		++calls;
		Require(active.BorrowObjectIdentity(arguments[0]) == identity && std::get<std::int32_t>(arguments[1]) == 8, "resolved weak getter must receive its actual original receiver and key");

		return SquirrelNativeResult::ReturnValue(std::int32_t{89});
	});
	const auto inner = vm.CreateWeakReference(method);
	const auto stored = state == WeakState::Nested ? vm.CreateWeakReference(inner) : inner;
	vm.NewSlot(delegate, std::string("_get"), stored);
	if (state == WeakState::Expired) {
		method = {};
		Require(vm.Type(vm.ResolveWeakReference(inner)) == SquirrelValueType::Null, "expired weak getter control must have an actually retired native closure");
	}

	const auto stored_references = vm.RetainedReferenceCount(stored);
	SquirrelValue result;
	bool failed = false;
	try {
		if (bytecode) {
			auto program = Program({Instruction(9, 3, 0, 1), Instruction(19, 1, 3)});
			program.MainFunction.Literals.push_back(std::int32_t{8});
			const std::array<SquirrelValue, 2> arguments{vm.RootTable(), receiver};
			result = vm.Invoke(vm.CreateClosure(std::move(program)), arguments).Value;
		} else {
			result = vm.ReadSlot(receiver, std::int32_t{8});
		}
	} catch (const sgre::script::SquirrelVmException& error) {
		failed = std::string(error.what()).find("index") != std::string::npos;
	}

	if (state == WeakState::Live) {
		Require(!failed && std::get<std::int32_t>(result) == 89 && calls == 1, "Table/userdata metamethod lookup must resolve a live weak field once before generic call");
	} else {
		Require(failed && calls == 0, "expired or nested weak metamethod controls must remain noncallable rather than recursively resolving wrappers");
	}

	Require(vm.FindRawSlot(delegate, std::string("_get")) == std::optional<SquirrelValue>{vm.ResolveWeakReference(stored)}, "native raw table lookup must still resolve exactly one weak wrapper after the call");
	Require(vm.RetainedReferenceCount(stored) == stored_references, "getter lookup must retain the actual delegate slot's weak wrapper instead of replacing it with the resolved target");
	vm.Close();
	vm.RethrowReleaseFailure();
}

int main() {
	try {
		for (const auto userdata : {false, true}) {
			for (const auto state : {WeakState::Live, WeakState::Expired, WeakState::Nested}) {
				for (const auto bytecode : {false, true}) {
					TestMetamethodResolvesOneWeakSlot(userdata, state, bytecode);
				}
			}
		}

		std::cout << "twelve independent weak metamethod slot fixture groups pass\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
