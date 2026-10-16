#include "sgre/script/squirrel_vm.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

using sgre::script::BytecodeProgram;
using sgre::script::SquirrelInstruction;
using sgre::script::SquirrelNativeResult;
using sgre::script::SquirrelRunState;
using sgre::script::SquirrelValue;
using sgre::script::SquirrelValueType;
using sgre::script::SquirrelVm;
using sgre::script::StringText;

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static SquirrelInstruction Instruction(std::uint8_t opcode, std::uint8_t first, std::int32_t second = 0, std::uint8_t third = 0, std::uint8_t fourth = 0) {
	SquirrelInstruction instruction;
	instruction.Opcode = opcode;
	instruction.Argument0 = first;
	instruction.Argument1 = second;
	instruction.Argument2 = third;
	instruction.Argument3 = fourth;

	return instruction;
}

static BytecodeProgram CallProgram(bool extra_argument = true) {
	BytecodeProgram program;
	program.MainFunction.SourceName = std::string("independent-base-suspend-CALL");
	program.MainFunction.Parameters = {std::string("this"), std::string("callable"), std::string("value")};
	program.MainFunction.StackSize = 5;
	program.MainFunction.Instructions = {Instruction(10, 3, 0), Instruction(10, 4, 2), Instruction(6, 2, 1, 3, extra_argument ? 2 : 1), Instruction(19, 1, 2)};

	return program;
}

static void TestScriptCallSuspendsAndResumes(bool extra_argument) {
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	const auto callback = vm.GetSlot(vm.RootTable(), std::string("suspend"));
	const std::array<SquirrelValue, 3> arguments{vm.RootTable(), callback, std::int32_t{23}};
	const auto result = vm.Invoke(vm.CreateClosure(CallProgram(extra_argument)), arguments);
	Require(result.State == SquirrelRunState::Suspended && result.Value == (extra_argument ? SquirrelValue(std::int32_t{23}) : vm.RootTable()), "base suspend must transfer its actual last stack argument and suspend only the script CALL dispatcher");
	Require(vm.IsSuspended(), "the actual suspended script context must retain its continuation");
	const auto resumed = vm.Resume(std::int32_t{53});
	Require(resumed.State == SquirrelRunState::Completed && std::get<std::int32_t>(resumed.Value) == 53 && !vm.IsSuspended(), "resumption must write the saved CALL destination and continue the real script return");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestNativeMetaGetReturnsValueWithoutSuspending() {
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	const auto class_value = vm.CreateClass();
	vm.NewSlot(class_value, std::string("_get"), vm.GetSlot(vm.RootTable(), std::string("suspend")));
	const auto instance = vm.CreateInstance(class_value);
	BytecodeProgram program;
	program.MainFunction.SourceName = std::string("independent-base-suspend-get");
	program.MainFunction.Parameters = {std::string("this"), std::string("instance")};
	program.MainFunction.StackSize = 3;
	program.MainFunction.Literals = {std::int32_t{7}};
	program.MainFunction.Instructions = {Instruction(9, 2, 0, 1), Instruction(19, 1, 2)};
	const std::array<SquirrelValue, 2> arguments{vm.RootTable(), instance};
	const auto result = vm.Invoke(vm.CreateClosure(std::move(program)), arguments);
	Require(result.State == SquirrelRunState::Completed && std::get<std::int32_t>(result.Value) == 7 && !vm.IsSuspended(), "base suspend at native counter two must return the actual getter key through GenericCall without suspending GET");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestDirectBaseCallRejectsAtCounterOne() {
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	const auto callback = vm.GetSlot(vm.RootTable(), std::string("suspend"));
	const std::array<SquirrelValue, 2> arguments{vm.RootTable(), std::int32_t{23}};
	bool failed = false;
	try {
		static_cast<void>(vm.Invoke(callback, arguments));
	} catch (const sgre::script::SquirrelVmException& error) {
		failed = StringText(error.ThrownValue()) == "cannot suspend through native calls/metamethods";
	}

	Require(failed && StringText(vm.LastError()) == "cannot suspend through native calls/metamethods" && !vm.IsSuspended(), "direct GenericCall counter one must preserve the exact guarded error without setting suspended state");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestBaseConstructorRequest(bool bytecode_call) {
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	const auto class_value = vm.CreateClass();
	vm.NewSlot(class_value, std::string("constructor"), vm.GetSlot(vm.RootTable(), std::string("suspend")));
	SquirrelValue instance;
	bool failed = false;
	try {
		if (bytecode_call) {
			auto program = CallProgram(false);
			const std::array<SquirrelValue, 3> arguments{vm.RootTable(), class_value, std::int32_t{23}};
			instance = vm.Invoke(vm.CreateClosure(std::move(program)), arguments).Value;
		} else {
			const std::array<SquirrelValue, 1> arguments{vm.RootTable()};
			instance = vm.Invoke(class_value, arguments).Value;
		}
	} catch (const sgre::script::SquirrelVmException& error) {
		failed = StringText(error.ThrownValue()) == "cannot suspend through native calls/metamethods";
	}

	Require(failed != bytecode_call && !vm.IsSuspended(), "native base constructor request must obey the actual current VM counter and discard its local suspension flag");
	if (bytecode_call) {
		Require(vm.Type(instance) == SquirrelValueType::Instance && vm.IsInstanceOf(instance, class_value), "allowed constructor request must return the actual newly created instance");
	}

	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestNestedChildCanSuspend(bool script_parent, bool two_children) {
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	const auto first_child = vm.CreateContext();
	const auto second_child = two_children ? vm.CreateContext() : first_child;
	const auto child_function = vm.CreateClosure(CallProgram());
	std::size_t calls = 0;
	const auto parent_callback = vm.CreateNativeClosure("independent-parent-active-child-suspension", [&](SquirrelVm& current, std::span<const SquirrelValue>) {
		const auto parent = current.CurrentContext();
		const auto invoke_child = [&](sgre::script::SquirrelContextHandle child, std::int32_t value) {
			const auto root = current.RootTableInContext(child);
			const auto suspend = current.GetSlot(root, std::string("suspend"));
			const std::array<SquirrelValue, 3> arguments{root, suspend, value};
			const auto result = current.InvokeInContext(child, child_function, arguments);
			Require(result.State == SquirrelRunState::Suspended && std::get<std::int32_t>(result.Value) == value, "a child's own Execute/native counter two must permit suspension during a still-running parent native callback");
			Require(current.ContextState(child) == sgre::script::SquirrelContextState::Suspended && current.CurrentContext() == parent && !current.IsSuspended(), "child suspension must preserve the running parent context and restore its active identity");
			++calls;
		};
		invoke_child(first_child, 23);
		if (two_children) {
			invoke_child(second_child, 29);
		}

		return SquirrelNativeResult::ReturnValue(std::int32_t{41});
	});
	if (script_parent) {
		const std::array<SquirrelValue, 3> arguments{vm.RootTable(), parent_callback, std::int32_t{17}};
		const auto result = vm.Invoke(vm.CreateClosure(CallProgram()), arguments);
		Require(result.State == SquirrelRunState::Completed && std::get<std::int32_t>(result.Value) == 41, "parent script must complete its own CALL and RETURN after a separate child suspends");
	} else {
		const std::array<SquirrelValue, 1> arguments{vm.RootTable()};
		const auto result = vm.Invoke(parent_callback, arguments);
		Require(result.State == SquirrelRunState::Completed && std::get<std::int32_t>(result.Value) == 41, "direct native parent must complete while the child's independent continuation remains suspended");
	}

	Require(calls == (two_children ? 2U : 1U), "each independent child must suspend exactly once");
	Require(std::get<std::int32_t>(vm.ResumeInContext(first_child, std::int32_t{53}).Value) == 53, "first child must resume its own saved destination after the parent returns");
	if (two_children) {
		Require(std::get<std::int32_t>(vm.ResumeInContext(second_child, std::int32_t{59}).Value) == 59, "second child must retain a distinct saved destination and resume value");
		vm.ReleaseContext(second_child);
	}

	vm.ReleaseContext(first_child);
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestNestedSameContextRejects() {
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	const auto function = vm.CreateClosure(CallProgram());
	const auto callback = vm.CreateNativeClosure("independent-same-VM-suspend-guard", [&](SquirrelVm& current, std::span<const SquirrelValue>) {
		const auto suspend = current.GetSlot(current.RootTable(), std::string("suspend"));
		const std::array<SquirrelValue, 3> arguments{current.RootTable(), suspend, std::int32_t{23}};
		bool failed = false;
		try {
			static_cast<void>(current.InvokeInContext(current.CurrentContext(), function, arguments));
		} catch (const sgre::script::SquirrelVmException& error) {
			failed = StringText(error.ThrownValue()) == "cannot suspend through native calls/metamethods";
		}

		Require(failed && !current.IsSuspended(), "reentrant Execute on the same VM must reject its actual native counter above two without suspending the parent");

		return SquirrelNativeResult::ReturnValue(std::int32_t{41});
	});
	const std::array<SquirrelValue, 3> arguments{vm.RootTable(), callback, std::int32_t{17}};
	const auto result = vm.Invoke(vm.CreateClosure(CallProgram()), arguments);
	Require(result.State == SquirrelRunState::Completed && std::get<std::int32_t>(result.Value) == 41, "a caught same-VM suspension error must leave the original parent execution usable");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestScriptMetamethodCounterRejects() {
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	BytecodeProgram getter;
	getter.MainFunction.SourceName = std::string("independent-script-getter-suspension");
	getter.MainFunction.Parameters = {std::string("this"), std::string("key")};
	getter.MainFunction.StackSize = 5;
	getter.MainFunction.Literals = {std::string("suspend")};
	getter.MainFunction.Instructions = {Instruction(21, 2), Instruction(9, 2, 0, 2), Instruction(10, 3, 0), Instruction(56, 4, 3), Instruction(6, 2, 2, 3, 1), Instruction(57, 1), Instruction(19, 255), Instruction(19, 1, 4)};
	const auto class_value = vm.CreateClass();
	vm.NewSlot(class_value, std::string("_get"), vm.CreateClosure(std::move(getter)));
	const auto instance = vm.CreateInstance(class_value);
	BytecodeProgram program;
	program.MainFunction.SourceName = std::string("independent-parent-getter-suspension");
	program.MainFunction.Parameters = {std::string("this"), std::string("instance")};
	program.MainFunction.StackSize = 3;
	program.MainFunction.Literals = {std::int32_t{7}};
	program.MainFunction.Instructions = {Instruction(9, 2, 0, 1), Instruction(19, 1, 2)};
	const std::array<SquirrelValue, 2> arguments{vm.RootTable(), instance};
	const auto result = vm.Invoke(vm.CreateClosure(std::move(program)), arguments);
	Require(result.State == SquirrelRunState::Completed && StringText(result.Value) == "cannot suspend through native calls/metamethods" && !vm.IsSuspended(), "script metamethod Execute must catch the exact base guard error from its own native counter level");
	vm.Close();
	vm.RethrowReleaseFailure();
}

int main() {
	try {
		TestScriptCallSuspendsAndResumes(false);
		TestScriptCallSuspendsAndResumes(true);
		TestNativeMetaGetReturnsValueWithoutSuspending();
		TestDirectBaseCallRejectsAtCounterOne();
		TestBaseConstructorRequest(false);
		TestBaseConstructorRequest(true);
		TestNestedChildCanSuspend(false, false);
		TestNestedChildCanSuspend(false, true);
		TestNestedChildCanSuspend(true, false);
		TestNestedChildCanSuspend(true, true);
		TestNestedSameContextRejects();
		TestScriptMetamethodCounterRejects();
		std::cout << "Squirrel native base suspension checks passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}

	return 0;
}
