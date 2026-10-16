#include "sgre/script/thread_bindings.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using sgre::script::ScriptThreadStatus;
using sgre::script::SquirrelValue;
using sgre::script::SquirrelValueType;

static void Require(bool condition, std::string_view text) {
	if (!condition) {
		throw std::runtime_error(std::string(text));
	}
}

static sgre::script::SquirrelInstruction Instruction(std::uint8_t opcode, std::uint8_t target, std::int32_t first = 0, std::uint8_t second = 0, std::uint8_t third = 0) {
	sgre::script::SquirrelInstruction result;
	result.Opcode = opcode;
	result.Argument0 = target;
	result.Argument1 = first;
	result.Argument2 = second;
	result.Argument3 = third;
	return result;
}

static SquirrelValue WaitingFunction(sgre::script::SquirrelVm& vm) {
	sgre::script::BytecodeProgram program;
	program.MainFunction.Name = std::string("independent-waiting-exit-owner");
	program.MainFunction.Parameters = {std::string("this"), std::string("retained-argument")};
	program.MainFunction.StackSize = 6;
	program.MainFunction.Literals = {std::string("wait"), std::string("independent-unmatched-notification")};
	program.MainFunction.Instructions = {Instruction(8, 2, 0, 0, 3), Instruction(1, 4, 1), Instruction(6, 5, 2, 3, 2), Instruction(19, 1, 1)};
	return vm.CreateClosure(std::move(program));
}

static void TestExitResultAndOwnedPhases(bool start, bool explicit_result, bool extras) {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	sgre::script::SquirrelVm vm(limits);
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptScheduler scheduler(vm);
	sgre::script::ScriptThreadBindings bindings(vm, scheduler, [](std::string_view) {
		return []() -> std::optional<SquirrelValue> {
			throw std::runtime_error("exit fixture did not request file I/O");
		};
	},
	    objects);
	SquirrelValue argument_weak;
	SquirrelValue target;
	{
		const auto argument = vm.CreateTable();
		argument_weak = vm.CreateWeakReference(argument);
		const std::array<SquirrelValue, 1> parameters{argument};
		target = bindings.Spawn(WaitingFunction(vm), parameters);
	}
	const auto handle = bindings.ThreadHandle(target);
	if (start) {
		static_cast<void>(scheduler.Advance(1));
	}
	const auto before = scheduler.Info(handle);
	Require(before.Status == (start ? ScriptThreadStatus::Running : ScriptThreadStatus::LoadingFunction), "exit fixture lost its genuine prior state: " + std::to_string(static_cast<std::int32_t>(before.Status)) + " error=" + before.Error);
	if (start) {
		Require(before.Context && vm.ContextState(*before.Context) == sgre::script::SquirrelContextState::Suspended && scheduler.ObserveWait(handle).NotificationCount == 1, "exit fixture requires its actual suspended context and retained notification wait");
	}
	const auto list = scheduler.NativeThreadList();
	const auto listener = vm.CreateInstance(objects.ObjectClass());
	const auto result_owner = vm.CreateTable();
	std::size_t notifications = 0;
	objects.Subscribe(target, listener, [&](const auto& actual) {
		const auto current = scheduler.Info(handle);
		Require(actual == target && current.Status == before.Status && current.Context == before.Context, "Thread.exit must notify its real prior status and context before retiring waits");
		Require(current.Result == (explicit_result ? result_owner : SquirrelValue{}), "Thread.exit must publish optional physical cell2 before notification");
		Require(vm.Type(vm.ResolveWeakReference(argument_weak)) != SquirrelValueType::Null && vm.Length(list) == 1, "Thread.exit prematurely removed actual arguments or native array registration");
		if (start) {
			Require(current.TimeoutTicks == -1, "native wait timeout changed before notification");
		}
		++notifications;
		return true;
	});
	std::vector<SquirrelValue> arguments{target};
	if (explicit_result) {
		arguments.push_back(result_owner);
	}
	if (extras) {
		arguments.push_back(std::int32_t{713});
		arguments.push_back(std::string("ignored-extra"));
	}
	const auto exit_method = vm.GetSlot(target, std::string("exit"));
	Require(vm.Type(vm.Invoke(exit_method, arguments).Value) == SquirrelValueType::Null, "Thread.exit must return native zero values");
	const auto after = scheduler.Info(handle);
	Require(notifications == 1 && after.Status == ScriptThreadStatus::None && after.Context == before.Context && after.NativeCallResult == before.NativeCallResult && after.Result == (explicit_result ? result_owner : SquirrelValue{}), "Thread.exit changed retained context/call status or lost its actual result");
	Require(vm.Length(list) == 1, "Thread.exit must leave pruning to the real scheduler tail");
	Require((vm.Type(vm.ResolveWeakReference(argument_weak)) != SquirrelValueType::Null) == start, "Thread.exit must drop the argument owner while retaining an already-suspended context's physical cells");
	static_cast<void>(scheduler.Advance(1));
	Require(vm.Length(list) == 0 && scheduler.Info(handle).Context == before.Context, "actual scheduler prune must retain the finished child context until native owner release");
	bindings.ReleaseInstance(target);
	Require(vm.Type(vm.ResolveWeakReference(argument_weak)) == SquirrelValueType::Null, "actual owner release must finally retire the retained context");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestDormantAndRunningSafety() {
	sgre::script::SquirrelVm vm;
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptScheduler scheduler(vm);
	sgre::script::ScriptThreadBindings bindings(vm, scheduler, [](std::string_view) {
		return []() -> std::optional<SquirrelValue> {
			return {};
		};
	},
	    objects);
	const auto dormant = vm.CreateInstance(bindings.ThreadClass());
	const std::array<SquirrelValue, 1> dormant_receiver{dormant};
	static_cast<void>(vm.Invoke(vm.GetSlot(dormant, std::string("constructor")), dormant_receiver));
	static_cast<void>(vm.Invoke(vm.GetSlot(dormant, std::string("exit")), dormant_receiver));
	Require(scheduler.Info(bindings.ThreadHandle(dormant)).Status == ScriptThreadStatus::None && vm.Length(scheduler.NativeThreadList()) == 0, "dormant native Thread.exit must not manufacture active registration");
	SquirrelValue current;
	const auto native = vm.CreateNativeClosure("independent-active-exit-guard", [&](auto& active, auto) {
		const std::array<SquirrelValue, 1> receiver{current};
		static_cast<void>(active.InvokeInContext(active.CurrentContext(), active.GetSlot(current, std::string("exit")), receiver));
		return sgre::script::SquirrelNativeResult::NoValues();
	});
	vm.NewSlot(vm.RootTable(), std::string("independent-active-exit-guard"), native);
	sgre::script::BytecodeProgram calling;
	calling.MainFunction.Parameters = {std::string("this")};
	calling.MainFunction.StackSize = 4;
	calling.MainFunction.Literals = {std::string("independent-active-exit-guard")};
	calling.MainFunction.Instructions = {Instruction(8, 1, 0, 0, 2), Instruction(6, 3, 1, 2, 1), Instruction(19, 1, 3)};
	current = bindings.Spawn(vm.CreateClosure(std::move(calling)));
	static_cast<void>(scheduler.Advance(1));
	Require(scheduler.Info(bindings.ThreadHandle(current)).Error.find("executing context") != std::string::npos, "unrecovered active self-exit must retain its explicit safety guard");
	vm.Close();
}

int main() {
	try {
		for (const bool start : {false, true}) {
			TestExitResultAndOwnedPhases(start, false, false);
			TestExitResultAndOwnedPhases(start, true, false);
			TestExitResultAndOwnedPhases(start, true, true);
		}
		TestDormantAndRunningSafety();
		std::cout << "PASS actual Thread.exit result/notification/wait/argument/context/native-array phases and guarded self-exit\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
