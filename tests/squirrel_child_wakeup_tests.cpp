#include "sgre/script/squirrel_vm.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>

using sgre::script::BytecodeProgram;
using sgre::script::SquirrelContextHandle;
using sgre::script::SquirrelContextState;
using sgre::script::SquirrelInstruction;
using sgre::script::SquirrelNativeResult;
using sgre::script::SquirrelRunState;
using sgre::script::SquirrelValue;
using sgre::script::SquirrelVm;

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

static BytecodeProgram CallProgram(bool repeat = false, bool fail = false) {
	BytecodeProgram program;
	program.MainFunction.SourceName = std::string("independent-child-wakeup");
	program.MainFunction.Parameters = {std::string("this"), std::string("function"), std::string("value")};
	program.MainFunction.StackSize = 5;
	program.MainFunction.Instructions = {Instruction(10, 3, 0), Instruction(10, 4, 2), Instruction(6, 2, 1, 3, 2)};
	if (repeat) {
		program.MainFunction.Instructions.push_back(Instruction(10, 4, 2));
		program.MainFunction.Instructions.push_back(Instruction(6, 2, 1, 3, 2));
	}
	if (fail) {
		program.MainFunction.Instructions.push_back(Instruction(2, 2, 91));
		program.MainFunction.Instructions.push_back(Instruction(58, 2));
	}

	program.MainFunction.Instructions.push_back(Instruction(19, 1, 2));

	return program;
}

static SquirrelValue MarkedRoot(SquirrelVm& vm, std::int32_t marker) {
	const auto root = vm.CreateTable();
	vm.NewSlot(root, std::string("identity"), marker);

	return root;
}

static void Suspend(SquirrelVm& vm, SquirrelContextHandle context, const SquirrelValue& root, const SquirrelValue& callback, bool repeat = false, bool fail = false) {
	const std::array<SquirrelValue, 3> arguments{root, callback, std::int32_t{23}};
	const auto result = vm.InvokeInContext(context, vm.CreateClosure(CallProgram(repeat, fail)), arguments);
	Require(result.State == SquirrelRunState::Suspended && std::get<std::int32_t>(result.Value) == 23, "the independent child must suspend at its actual first base CALL before any wakeup");
}

static void TestNestedWakeup(bool target_is_root, bool suspend_again, bool script_parent) {
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	const auto suspend = vm.GetSlot(vm.RootTable(), std::string("suspend"));
	const auto target = target_is_root ? vm.RootContext() : vm.CreateContext();
	const auto parent = target_is_root ? vm.CreateContext() : vm.RootContext();
	const auto target_root = MarkedRoot(vm, 23);
	const auto parent_root = MarkedRoot(vm, 41);
	vm.SetRootTableInContext(target, target_root);
	vm.SetRootTableInContext(parent, parent_root);
	Suspend(vm, target, target_root, suspend, suspend_again);
	std::size_t calls = 0;
	const auto bridge = vm.CreateNativeClosure("independent-nested-child-wakeup", [&](SquirrelVm& current, std::span<const SquirrelValue>) {
		Require(current.CurrentContext() == parent && current.RootTable() == parent_root, "parent callback must begin on its own real context and replaced root");
		const auto result = current.ResumeInContext(target, std::int32_t{53});
		Require(result.State == (suspend_again ? SquirrelRunState::Suspended : SquirrelRunState::Completed) && std::get<std::int32_t>(result.Value) == 53, "nested native wakeup must deliver the actual value and preserve repeated suspension");
		Require(current.ContextState(target) == (suspend_again ? SquirrelContextState::Suspended : SquirrelContextState::Idle), "wakeup must preserve the target context's actual continuation state");
		Require(current.CurrentContext() == parent && current.RootTable() == parent_root && !current.IsSuspended(), "wakeup must restore the running parent context, root and suspended flag");
		Require(std::get<std::int32_t>(current.GetSlot(current.RootTable(), std::string("identity"))) == 41, "native work after child wakeup must still read the actual parent root");
		++calls;

		return SquirrelNativeResult::ReturnValue(std::int32_t{41});
	});
	if (script_parent) {
		const std::array<SquirrelValue, 3> arguments{parent_root, bridge, std::int32_t{17}};
		const auto result = vm.InvokeInContext(parent, vm.CreateClosure(CallProgram()), arguments);
		Require(result.State == SquirrelRunState::Completed && std::get<std::int32_t>(result.Value) == 41, "parent script must execute its real RETURN after nested wakeup restores running state");
	} else {
		const std::array<SquirrelValue, 1> arguments{parent_root};
		const auto result = vm.InvokeInContext(parent, bridge, arguments);
		Require(result.State == SquirrelRunState::Completed && std::get<std::int32_t>(result.Value) == 41, "direct native parent must retain its returned value after nested wakeup");
	}

	Require(calls == 1 && vm.ContextState(parent) == SquirrelContextState::Idle, "parent wakeup callback must complete exactly once and leave no active invocation");
	if (suspend_again) {
		const auto result = vm.ResumeInContext(target, std::int32_t{59});
		Require(result.State == SquirrelRunState::Completed && std::get<std::int32_t>(result.Value) == 59, "the second suspended child CALL must retain a genuine later resume destination");
	}

	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestChildFailureRestoresParent() {
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	const auto suspend = vm.GetSlot(vm.RootTable(), std::string("suspend"));
	const auto child = vm.CreateContext();
	const auto child_root = MarkedRoot(vm, 23);
	vm.SetRootTableInContext(child, child_root);
	Suspend(vm, child, child_root, suspend, false, true);
	const auto parent_root = vm.RootTable();
	const auto bridge = vm.CreateNativeClosure("independent-failed-child-wakeup", [&](SquirrelVm& current, std::span<const SquirrelValue>) {
		const auto parent = current.CurrentContext();
		bool failed = false;
		try {
			static_cast<void>(current.ResumeInContext(child, std::int32_t{53}));
		} catch (const sgre::script::SquirrelVmException& error) {
			failed = std::get<std::int32_t>(error.ThrownValue()) == 91;
		}

		Require(failed && std::get<std::int32_t>(current.LastErrorInContext(child)) == 91, "failed child wakeup must retain its own actual thrown value and last-error owner");
		Require(current.CurrentContext() == parent && current.RootTable() == parent_root && !current.IsSuspended(), "failed child wakeup must restore the parent before returning to its native callback");
		Require(std::holds_alternative<std::monostate>(current.LastError()), "child failure must not overwrite the running parent's independent last error");

		return SquirrelNativeResult::ReturnValue(std::int32_t{41});
	});
	const std::array<SquirrelValue, 3> arguments{parent_root, bridge, std::int32_t{17}};
	const auto result = vm.Invoke(vm.CreateClosure(CallProgram()), arguments);
	Require(result.State == SquirrelRunState::Completed && std::get<std::int32_t>(result.Value) == 41, "parent must remain executable after catching the separate child's failed resume");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestNestedBudgetIsNotReset(std::uint64_t budget) {
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	const auto suspend = vm.GetSlot(vm.RootTable(), std::string("suspend"));
	const auto child = vm.CreateContext();
	Suspend(vm, child, vm.RootTableInContext(child), suspend);
	const auto bridge = vm.CreateNativeClosure("independent-shared-wakeup-budget", [&](SquirrelVm& current, std::span<const SquirrelValue>) {
		const auto result = current.ResumeInContext(child, std::int32_t{53}, 1000);
		Require(result.State == SquirrelRunState::Completed && result.Instructions == 1 && std::get<std::int32_t>(result.Value) == 53, "child wakeup must execute and charge its one genuine pending RETURN instruction");

		return SquirrelNativeResult::ReturnValue(std::int32_t{41});
	});
	const std::array<SquirrelValue, 3> arguments{vm.RootTable(), bridge, std::int32_t{17}};
	bool failed = false;
	try {
		const auto result = vm.Invoke(vm.CreateClosure(CallProgram()), arguments, budget);
		Require(budget == 5 && result.State == SquirrelRunState::Completed && result.Instructions == 5 && std::get<std::int32_t>(result.Value) == 41, "four parent instructions and one child continuation must consume the shared outer budget");
	} catch (const std::runtime_error& error) {
		failed = budget == 4 && std::string(error.what()) == "Squirrel instruction budget exceeded";
		if (!failed) {
			throw;
		}
	}

	Require(failed == (budget == 4) && vm.CurrentContext() == vm.RootContext(), "a nested child's larger requested budget must not reset the caller's remaining safety bound");
	vm.ResetExecution();
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestNestedDepthPreflightPreservesSuspension() {
	sgre::script::SquirrelVmLimits limits;
	limits.MaxFrames = 1;
	SquirrelVm vm(limits);
	vm.InstallRecoveredBaseLibrary();
	const auto suspend = vm.GetSlot(vm.RootTable(), std::string("suspend"));
	const auto child = vm.CreateContext();
	Suspend(vm, child, vm.RootTableInContext(child), suspend);
	const auto callback = vm.CreateNativeClosure("independent-nested-wakeup-depth-bound", [&](SquirrelVm& current, std::span<const SquirrelValue>) {
		const auto parent = current.CurrentContext();
		bool failed = false;
		try {
			static_cast<void>(current.ResumeInContext(child, std::int32_t{53}));
		} catch (const std::runtime_error& error) {
			failed = std::string(error.what()) == "Squirrel native invocation depth limit exceeded";
		}

		Require(failed && current.CurrentContext() == parent && current.ContextState(child) == SquirrelContextState::Suspended, "authored global invocation-depth preflight must reject before mutating the child or active parent context");

		return SquirrelNativeResult::ReturnValue(std::int32_t{41});
	});
	const std::array<SquirrelValue, 1> arguments{vm.RootTable()};
	Require(std::get<std::int32_t>(vm.Invoke(callback, arguments).Value) == 41, "bounded preflight failure must leave the parent native callback usable");
	Require(std::get<std::int32_t>(vm.ResumeInContext(child, std::int32_t{59}).Value) == 59, "the rejected nested wakeup must preserve the original later host continuation");
	vm.Close();
	vm.RethrowReleaseFailure();
}

int main() {
	try {
		for (const bool target_is_root : {false, true}) {
			for (const bool suspend_again : {false, true}) {
				for (const bool script_parent : {false, true}) {
					TestNestedWakeup(target_is_root, suspend_again, script_parent);
				}
			}
		}

		TestChildFailureRestoresParent();
		TestNestedBudgetIsNotReset(4);
		TestNestedBudgetIsNotReset(5);
		TestNestedDepthPreflightPreservesSuspension();
		std::cout << "Squirrel nested child wakeup checks passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}

	return 0;
}
