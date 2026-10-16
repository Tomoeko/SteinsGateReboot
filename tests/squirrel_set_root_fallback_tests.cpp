#include "sgre/script/object_bindings.hpp"
#include "sgre/script/squirrel_vm.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

using sgre::script::BytecodeProgram;
using sgre::script::ScriptObjectBindings;
using sgre::script::SquirrelContextHandle;
using sgre::script::SquirrelInstruction;
using sgre::script::SquirrelNativeResult;
using sgre::script::SquirrelValue;
using sgre::script::SquirrelVm;

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view expected) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(expected) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("expected failure: " + std::string(expected));
}

template <typename Operation>
static void TestCase(std::string_view name, Operation operation) {
	try {
		operation();
	} catch (const std::exception& error) {
		throw std::runtime_error(std::string(name) + ": " + error.what());
	}
}

static SquirrelInstruction Instruction(std::uint8_t opcode, std::uint8_t destination, std::int32_t argument1 = 0, std::uint8_t argument2 = 0, std::uint8_t argument3 = 0) {
	SquirrelInstruction instruction;
	instruction.Opcode = opcode;
	instruction.Argument0 = destination;
	instruction.Argument1 = argument1;
	instruction.Argument2 = argument2;
	instruction.Argument3 = argument3;

	return instruction;
}

static BytecodeProgram Program(std::vector<SquirrelInstruction> instructions, std::vector<sgre::script::SquirrelLiteral> parameters, std::uint32_t registers = 8) {
	BytecodeProgram program;
	program.MainFunction.Name = std::string("independentSetFixture");
	program.MainFunction.SourceName = std::string("independent-set-root.nut");
	program.MainFunction.Parameters = std::move(parameters);
	program.MainFunction.StackSize = registers;
	program.MainFunction.Instructions = std::move(instructions);

	return program;
}

static SquirrelValue Write(SquirrelVm& vm, SquirrelContextHandle context, const SquirrelValue& receiver, const SquirrelValue& key, const SquirrelValue& value) {
	auto program = Program({Instruction(13, 3, 0, 1, 2), Instruction(19, 1, 3)}, {std::string("this"), std::string("key"), std::string("value")});
	const auto closure = vm.CreateClosureInContext(context, std::move(program));
	const std::array<SquirrelValue, 3> arguments{receiver, key, value};
	const auto result = vm.InvokeInContext(context, closure, arguments);
	Require(result.State == sgre::script::SquirrelRunState::Completed, "SET cannot suspend through its actual metamethod");

	return result.Value;
}

static SquirrelValue ThrowingSetter(SquirrelVm& vm) {
	auto program = Program({Instruction(1, 3, 0), Instruction(58, 3)}, {std::string("this"), std::string("key"), std::string("value")});
	program.MainFunction.Literals = {std::string("independent script setter failure")};

	return vm.CreateClosure(std::move(program));
}

static void TestActualObjectPropertyFailure() {
	SquirrelVm vm;
	ScriptObjectBindings objects(vm);
	const auto derived = vm.CreateClass(objects.ObjectClass());
	const auto application = vm.CreateInstance(derived);
	objects.AttachInstance(application);
	vm.NewSlot(vm.RootTable(), std::string("KEY_OK"), std::int32_t{2});
	Require(std::get<std::int32_t>(Write(vm, vm.RootContext(), application, std::string("KEY_OK"), std::int32_t{1})) == 1, "actual Object._set failure must return the assigned expression value");
	Require(std::get<std::int32_t>(vm.GetSlot(vm.RootTable(), std::string("KEY_OK"))) == 1 && !vm.HasSlot(application, std::string("KEY_OK")), "implicit Object SET must update its existing root slot without creating an Application member");
	RequireFailure([&] {
		vm.AssignSlot(application, std::string("KEY_OK"), std::int32_t{9});
	},
	    "native Object property setter failed");
	Require(std::get<std::int32_t>(vm.GetSlot(vm.RootTable(), std::string("KEY_OK"))) == 1, "context-free native AssignSlot must not borrow the bytecode implicit-this fallback");

	std::int32_t assigned = 0;
	const auto successful_type = vm.CreateClass(objects.ObjectClass());
	vm.NewSlot(successful_type, std::string("setKEY_OK"), vm.CreateNativeClosure("actual_property_setter", [&](SquirrelVm& current, const auto arguments) {
		Require(arguments.size() == 2 && current.IsInstanceOf(arguments[0], successful_type), "actual Object setter receiver changed");
		assigned = std::get<std::int32_t>(arguments[1]);
		SquirrelNativeResult result;
		result.Value = false;

		return result;
	}));
	const auto successful_application = vm.CreateInstance(successful_type);
	objects.AttachInstance(successful_application);
	Require(std::get<std::int32_t>(Write(vm, vm.RootContext(), successful_application, std::string("KEY_OK"), std::int32_t{8})) == 8 && assigned == 8, "successful Object setter must win even when it returns false");
	Require(std::get<std::int32_t>(vm.GetSlot(vm.RootTable(), std::string("KEY_OK"))) == 1, "successful setter must not update a same-named root slot");
}

static void TestNativeAndScriptFailureContinuations() {
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	std::size_t notifications = 0;
	const auto handler = vm.CreateNativeClosure("error_observer", [&](SquirrelVm&, const auto) {
		++notifications;

		return SquirrelNativeResult{};
	});
	const std::array<SquirrelValue, 2> install{vm.RootTable(), handler};
	static_cast<void>(vm.Invoke(vm.GetSlot(vm.RootTable(), std::string("seterrorhandler")), install));
	vm.NewSlot(vm.RootTable(), std::string("shared"), std::int32_t{10});
	for (const bool native : {false, true}) {
		const auto type = vm.CreateClass();
		std::size_t calls = 0;
		const auto setter = native ? vm.CreateNativeClosure("throwing_setter", [&](SquirrelVm& current, const auto arguments) -> SquirrelNativeResult {
			const std::array<SquirrelValue, 3> saved{arguments[0], arguments[1], arguments[2]};
			++calls;
			auto nested = Program({Instruction(2, 1, 73), Instruction(19, 1, 1)}, {std::string("this")}, 32768);
			const auto closure = current.CreateClosure(std::move(nested));
			const std::array<SquirrelValue, 1> root{current.RootTable()};
			Require(std::get<std::int32_t>(current.Invoke(closure, root).Value) == 73, "nested native setter call failed before its intended exception");
			Require(sgre::script::StringText(saved[1]) == "shared" && std::get<std::int32_t>(saved[2]) == 29, "setter inputs must survive real operand-stack growth");
			throw std::runtime_error("independent native setter failure");
		})
		                           : ThrowingSetter(vm);
		vm.NewSlot(type, std::string("_set"), setter);
		const auto receiver = vm.CreateInstance(type);
		Require(std::get<std::int32_t>(Write(vm, vm.RootContext(), receiver, std::string("shared"), std::int32_t{29})) == 29, "failed native/script _set must resume the suspended SET with its assigned value");
		Require(std::get<std::int32_t>(vm.GetSlot(vm.RootTable(), std::string("shared"))) == 29 && notifications == 0, "recovered setter failure must update the root without an unhandled-error notification");
		Require(!native || calls == 1, "native setter must execute exactly once");
	}

	const auto missing_type = vm.CreateClass();
	vm.NewSlot(missing_type, std::string("_set"), ThrowingSetter(vm));
	const auto missing_receiver = vm.CreateInstance(missing_type);
	RequireFailure([&] {
		static_cast<void>(Write(vm, vm.RootContext(), missing_receiver, std::string("absent"), std::int32_t{7}));
	},
	    "the index 'absent' does not exist");
	Require(!vm.HasSlot(vm.RootTable(), std::string("absent")) && notifications == 1, "unrecovered SET must report exactly one outer error and never create a root slot");
}

static void TestTrapAndSuccessfulScriptSetter() {
	SquirrelVm vm;
	const auto type = vm.CreateClass();
	vm.NewSlot(type, std::string("_set"), ThrowingSetter(vm));
	const auto receiver = vm.CreateInstance(type);
	auto trapped = Program({Instruction(56, 4, 3), Instruction(13, 3, 0, 1, 2), Instruction(57, 1), Instruction(19, 1, 3), Instruction(19, 1, 4)}, {std::string("this"), std::string("key"), std::string("value")});
	const auto closure = vm.CreateClosure(std::move(trapped));
	const std::array<SquirrelValue, 3> arguments{receiver, std::string("absent"), std::int32_t{57}};
	Require(sgre::script::StringText(vm.Invoke(closure, arguments).Value) == "the index 'absent' does not exist", "a failed SET must deliver the native missing-index error to its actual caller trap");

	auto successful = Program({Instruction(2, 3, 0), Instruction(19, 1, 3)}, {std::string("this"), std::string("key"), std::string("value")});
	const auto successful_type = vm.CreateClass();
	vm.NewSlot(successful_type, std::string("_set"), vm.CreateClosure(std::move(successful)));
	const auto successful_receiver = vm.CreateInstance(successful_type);
	vm.NewSlot(vm.RootTable(), std::string("absent"), std::int32_t{13});
	Require(std::get<std::int32_t>(Write(vm, vm.RootContext(), successful_receiver, std::string("absent"), std::int32_t{57})) == 57, "successful script _set must return the assigned value rather than its own zero result");
	Require(std::get<std::int32_t>(vm.GetSlot(vm.RootTable(), std::string("absent"))) == 13, "successful script _set cannot fall through to the root");
}

static void TestIdentityRawSlotAndContext() {
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	const auto type = vm.CreateClass();
	vm.NewSlot(type, std::string("_set"), ThrowingSetter(vm));
	const auto receiver = vm.CreateInstance(type);
	const auto distinct = vm.CreateInstance(type);
	vm.NewSlot(vm.RootTable(), std::string("shared"), std::int32_t{11});
	auto explicit_target = Program({Instruction(13, 4, 3, 1, 2), Instruction(19, 1, 4)}, {std::string("this"), std::string("key"), std::string("value"), std::string("target")});
	const auto closure = vm.CreateClosure(std::move(explicit_target));
	const std::array<SquirrelValue, 4> distinct_arguments{receiver, std::string("shared"), std::int32_t{42}, distinct};
	RequireFailure([&] {
		static_cast<void>(vm.Invoke(closure, distinct_arguments));
	},
	    "the index 'shared' does not exist");
	Require(std::get<std::int32_t>(vm.GetSlot(vm.RootTable(), std::string("shared"))) == 11, "a different receiver of the same class cannot use implicit-this root fallback");
	const std::array<SquirrelValue, 4> alias_arguments{receiver, std::string("shared"), std::int32_t{42}, receiver};
	Require(std::get<std::int32_t>(vm.Invoke(closure, alias_arguments).Value) == 42, "an explicit operand aliasing frame this must use native identity-based fallback");

	const auto child_context = vm.CreateContext();
	const auto child_root = vm.CreateTable();
	vm.NewSlot(child_root, std::string("shared"), std::int32_t{100});
	vm.SetRootTableInContext(child_context, child_root);
	Require(std::get<std::int32_t>(Write(vm, child_context, receiver, std::string("shared"), std::int32_t{61})) == 61, "SET must complete in its actual receiving execution context");
	Require(std::get<std::int32_t>(vm.GetSlot(child_root, std::string("shared"))) == 61 && std::get<std::int32_t>(vm.GetSlot(vm.RootTable(), std::string("shared"))) == 42, "SET fallback must update the active context root rather than the base VM root");
	vm.ReleaseContext(child_context);

	const auto delegated = vm.CreateTable();
	vm.NewSlot(delegated, std::string("delegateOnly"), std::int32_t{5});
	auto delegation = Program({Instruction(53, 1, 1, 2), Instruction(19, 1, 1)}, {std::string("this"), std::string("table"), std::string("delegate")});
	const auto attach_closure = vm.CreateClosure(std::move(delegation));
	const std::array<SquirrelValue, 3> attach{vm.RootTable(), vm.RootTable(), delegated};
	TestCase("opcode53 root delegate attachment", [&] {
		Require(vm.Invoke(attach_closure, attach).Value == vm.RootTable(), "delegate expression must return its actual table");
	});
	RequireFailure([&] {
		static_cast<void>(Write(vm, vm.RootContext(), receiver, std::string("delegateOnly"), std::int32_t{9}));
	},
	    "the index 'delegateOnly' does not exist");
	Require(std::get<std::int32_t>(vm.GetSlot(delegated, std::string("delegateOnly"))) == 5, "native root retry is an own-slot write and cannot follow its delegate");
}

static void TestExistingMemberAndNewSlotPrecedence() {
	SquirrelVm vm;
	vm.NewSlot(vm.RootTable(), std::string("shared"), std::int32_t{11});
	const auto type = vm.CreateClass();
	vm.NewSlot(type, std::string("shared"), std::int32_t{3});
	vm.NewSlot(type, std::string("_set"), ThrowingSetter(vm));
	const auto receiver = vm.CreateInstance(type);
	Require(std::get<std::int32_t>(Write(vm, vm.RootContext(), receiver, std::string("shared"), std::int32_t{32})) == 32 && std::get<std::int32_t>(vm.GetSlot(receiver, std::string("shared"))) == 32, "existing instance field must be assigned before attempting _set or root fallback");
	Require(std::get<std::int32_t>(vm.GetSlot(vm.RootTable(), std::string("shared"))) == 11, "own member assignment must leave the same-named root untouched");

	const auto new_type = vm.CreateClass();
	vm.NewSlot(new_type, std::string("_newslot"), ThrowingSetter(vm));
	const auto new_receiver = vm.CreateInstance(new_type);
	auto program = Program({Instruction(11, 3, 0, 1, 2), Instruction(19, 1, 3)}, {std::string("this"), std::string("key"), std::string("value")});
	const auto closure = vm.CreateClosure(std::move(program));
	const std::array<SquirrelValue, 3> arguments{new_receiver, std::string("shared"), std::int32_t{48}};
	RequireFailure([&] {
		static_cast<void>(vm.Invoke(closure, arguments));
	},
	    "independent script setter failure");
	Require(std::get<std::int32_t>(vm.GetSlot(vm.RootTable(), std::string("shared"))) == 11, "NEW_SLOT must not inherit SET's root fallback");
}

int main() {
	try {
		TestCase("TestActualObjectPropertyFailure", TestActualObjectPropertyFailure);
		TestCase("TestNativeAndScriptFailureContinuations", TestNativeAndScriptFailureContinuations);
		TestCase("TestTrapAndSuccessfulScriptSetter", TestTrapAndSuccessfulScriptSetter);
		TestCase("TestIdentityRawSlotAndContext", TestIdentityRawSlotAndContext);
		TestCase("TestExistingMemberAndNewSlotPrecedence", TestExistingMemberAndNewSlotPrecedence);
		std::cout << "Squirrel implicit-this SET root fallback checks passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
