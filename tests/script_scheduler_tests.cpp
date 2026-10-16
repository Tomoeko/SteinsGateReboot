#include "sgre/script/script_scheduler.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using sgre::script::BytecodeProgram;
using sgre::script::ScriptScheduler;
using sgre::script::ScriptThreadHandle;
using sgre::script::ScriptThreadStatus;
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

static sgre::script::SquirrelInstruction Instruction(std::uint8_t opcode, std::uint8_t destination, std::int32_t argument1 = 0, std::uint8_t argument2 = 0, std::uint8_t argument3 = 0) {
	sgre::script::SquirrelInstruction instruction;
	instruction.Opcode = opcode;
	instruction.Argument0 = destination;
	instruction.Argument1 = argument1;
	instruction.Argument2 = argument2;
	instruction.Argument3 = argument3;

	return instruction;
}

static BytecodeProgram Program(std::vector<sgre::script::SquirrelInstruction> instructions) {
	BytecodeProgram program;
	program.MainFunction.Name = std::string("fixture");
	program.MainFunction.SourceName = std::string("independent-scheduler.nut");
	program.MainFunction.Parameters.push_back(std::string("this"));
	program.MainFunction.StackSize = 4;
	program.MainFunction.Instructions = std::move(instructions);

	return program;
}

static SquirrelValue Calling(SquirrelVm& vm, std::string native_name) {
	auto program = Program({Instruction(8, 1, 0, 0, 2), Instruction(6, 3, 1, 2, 1), Instruction(19, 1, 3)});
	program.MainFunction.Literals.push_back(std::move(native_name));

	return vm.CreateClosure(std::move(program));
}

static std::array<SquirrelValue, 1> Arguments(const SquirrelVm& vm) {
	return {vm.RootTable()};
}

static void TestPriorityAndSnapshot() {
	SquirrelVm vm;
	ScriptScheduler scheduler(vm);
	std::vector<std::int32_t> order;
	const auto child = vm.CreateClosure(Program({Instruction(2, 1, 42), Instruction(19, 1, 1)}));
	std::optional<ScriptThreadHandle> spawned;
	vm.RegisterNative("first", [&](SquirrelVm&, std::span<const SquirrelValue>) {
		order.push_back(1);
		spawned = scheduler.Fork(child, Arguments(vm), -50);

		return sgre::script::SquirrelNativeResult{};
	});
	vm.RegisterNative("second", [&](SquirrelVm&, std::span<const SquirrelValue>) {
		order.push_back(2);

		return sgre::script::SquirrelNativeResult{};
	});
	vm.RegisterNative("third", [&](SquirrelVm&, std::span<const SquirrelValue>) {
		order.push_back(3);

		return sgre::script::SquirrelNativeResult{};
	});
	const auto second = scheduler.Fork(Calling(vm, "second"), Arguments(vm), 10);
	const auto first = scheduler.Fork(Calling(vm, "first"), Arguments(vm), -1);
	const auto third = scheduler.Fork(Calling(vm, "third"), Arguments(vm), 10);
	Require(scheduler.Info(first).Status == ScriptThreadStatus::LoadingFunction, "new closure must retain native loading-function phase");
	const auto tick = scheduler.Advance(16);
	Require(order == std::vector<std::int32_t>{1, 2, 3}, "signed priorities or equal-priority insertion order changed");
	Require(tick.Completed == std::vector<ScriptThreadHandle>{first, second, third}, "completion order differs from execution order");
	Require(tick.ActiveThreads == 1 && scheduler.Info(*spawned).ElapsedTicks == 0, "a child forked during a tick executed in that snapshot");
	Require(!scheduler.CurrentThread(), "current thread escaped execution scope");
	Require(scheduler.Advance(16).Completed == std::vector<ScriptThreadHandle>{*spawned}, "child did not execute on the next tick");
	Require(std::get<std::int32_t>(scheduler.Info(*spawned).Result) == 42, "child result changed");
}

static void TestChildWakeAndError() {
	for (const bool throw_value : {false, true}) {
		SquirrelVm vm;
		ScriptScheduler scheduler(vm);
		auto child_program = Program({Instruction(2, 1, 73), Instruction(throw_value ? 58 : 19, throw_value ? 1 : 1, throw_value ? 0 : 1)});
		const auto child = vm.CreateClosure(std::move(child_program));
		std::optional<ScriptThreadHandle> spawned;
		vm.RegisterNative("system_fixture", [&](SquirrelVm&, std::span<const SquirrelValue>) {
			Require(scheduler.CurrentThread().has_value(), "native system has no engine thread");
			spawned = scheduler.Fork(child, Arguments(vm), 5);
			scheduler.WaitForChild(*scheduler.CurrentThread(), *spawned);
			sgre::script::SquirrelNativeResult result;
			result.Suspend = true;

			return result;
		});
		const auto parent = scheduler.Fork(Calling(vm, "system_fixture"), Arguments(vm), 10);
		Require(scheduler.Advance(1).ActiveThreads == 2, "system did not suspend its parent and retain its child");
		const auto wake = scheduler.Advance(1);
		Require(wake.Completed == std::vector<ScriptThreadHandle>{*spawned, parent}, "a completed higher-priority child did not wake its later parent in the same tick");
		Require(std::get<std::int32_t>(scheduler.Info(parent).Result) == 73, "child result or thrown value was not supplied to the suspended call");
		Require(scheduler.Info(parent).Error.empty(), "child error was rethrown instead of delivered as a value");
		Require(scheduler.Info(*spawned).Error.empty() != throw_value, "child error metadata did not preserve failure");
	}
}

static void TestTimeoutAndNotification() {
	SquirrelVm vm;
	ScriptScheduler scheduler(vm);
	vm.RegisterNative("wait_fixture", [&](SquirrelVm&, std::span<const SquirrelValue>) {
		const std::array<std::string, 2> notifications{"advance", "cancel"};
		scheduler.WaitForNotifications(*scheduler.CurrentThread(), notifications, 2.9f);
		sgre::script::SquirrelNativeResult result;
		result.Suspend = true;

		return result;
	});
	const auto timed = scheduler.Fork(Calling(vm, "wait_fixture"), Arguments(vm));
	static_cast<void>(scheduler.Advance(10));
	Require(scheduler.Info(timed).TimeoutTicks == 2, "float wait did not truncate its timer while retaining its resume value");
	static_cast<void>(scheduler.Advance(2));
	Require(scheduler.Info(timed).TimeoutTicks == 0 && scheduler.Info(timed).Status == ScriptThreadStatus::Running, "timeout woke at zero instead of strictly below zero");
	static_cast<void>(scheduler.Advance(0));
	Require(scheduler.Info(timed).Status == ScriptThreadStatus::Running, "zero delta woke a zero timeout");
	static_cast<void>(scheduler.Advance(1));
	Require(std::get<float>(scheduler.Info(timed).Result) == 2.9f, "numeric timeout resume value changed");

	const auto notified = scheduler.Fork(Calling(vm, "wait_fixture"), Arguments(vm));
	static_cast<void>(scheduler.Advance(1));
	scheduler.Notify("unrelated");
	static_cast<void>(scheduler.Advance(0));
	Require(scheduler.Info(notified).Status == ScriptThreadStatus::Running, "unrelated notification woke a thread");
	scheduler.Notify("cancel");
	static_cast<void>(scheduler.Advance(0));
	Require(sgre::script::StringText(scheduler.Info(notified).Result) == "cancel" && scheduler.Info(notified).TimeoutTicks == -1, "matching notification did not clear the entire wait and preserve its value");
}

static bool HasStringTarget(SquirrelVm& vm, const sgre::script::SquirrelHostRoot& weak) {
	return std::holds_alternative<sgre::script::SquirrelString>(vm.ResolveWeakReference(weak.Value()));
}

static void TestCanonicalNotificationOwnership() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	ScriptScheduler scheduler(vm);
	std::array<sgre::script::SquirrelHostRoot, 2> weak;
	vm.RegisterNative("canonical_wait_fixture", [&](SquirrelVm& active_vm, std::span<const SquirrelValue>) {
		const auto first = active_vm.CreateString("canonical-selected-wait");
		const auto second = active_vm.CreateString("canonical-unmatched-wait");
		weak[0] = active_vm.Retain(active_vm.CreateWeakReference(first));
		weak[1] = active_vm.Retain(active_vm.CreateWeakReference(second));
		const std::array<SquirrelValue, 3> values{first, second, first};
		scheduler.WaitForNotificationValues(*scheduler.CurrentThread(), values, 17.5f);
		sgre::script::SquirrelNativeResult result;
		result.Suspend = true;

		return result;
	});
	const auto thread = scheduler.Fork(Calling(vm, "canonical_wait_fixture"), Arguments(vm));
	static_cast<void>(scheduler.Advance(1));
	Require(HasStringTarget(vm, weak[0]) && HasStringTarget(vm, weak[1]), "native wait strings expired when temporary callback values were released");
	scheduler.Notify("different-canonical-wait");
	Require(HasStringTarget(vm, weak[0]) && HasStringTarget(vm, weak[1]) && scheduler.Info(thread).TimeoutTicks == 17, "unmatched notification changed wait ownership or timeout");
	scheduler.Notify("canonical-selected-wait");
	Require(HasStringTarget(vm, weak[0]) && !HasStringTarget(vm, weak[1]) && scheduler.Info(thread).TimeoutTicks == -1, "matching notification did not transfer its exact value before releasing all remaining targets");
	static_cast<void>(scheduler.Advance(0));
	Require(std::get<sgre::script::SquirrelString>(scheduler.Info(thread).Result).SharesIdentity(std::get<sgre::script::SquirrelString>(vm.ResolveWeakReference(weak[0].Value()))), "matching notification reconstructed equal bytes instead of returning the original canonical identity");
	scheduler.Release(thread);
	Require(!HasStringTarget(vm, weak[0]) && !HasStringTarget(vm, weak[1]), "completed notification values remained after result and context release");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestNotificationCancellationOwnership() {
	for (const bool timeout : {false, true}) {
		sgre::script::SquirrelVmLimits limits;
		limits.ReclaimObjects = true;
		SquirrelVm vm(limits);
		ScriptScheduler scheduler(vm);
		sgre::script::SquirrelHostRoot weak;
		vm.RegisterNative("cancel_wait_fixture", [&](SquirrelVm& active_vm, std::span<const SquirrelValue>) {
			const auto name = active_vm.CreateString("canonical-cancelled-wait");
			weak = active_vm.Retain(active_vm.CreateWeakReference(name));
			const std::array<SquirrelValue, 1> values{name};
			scheduler.WaitForNotificationValues(*scheduler.CurrentThread(), values, timeout ? std::optional<SquirrelValue>(2.75f) : std::nullopt);
			sgre::script::SquirrelNativeResult result;
			result.Suspend = true;

			return result;
		});
		const auto thread = scheduler.Fork(Calling(vm, "cancel_wait_fixture"), Arguments(vm));
		static_cast<void>(scheduler.Advance(1));
		Require(HasStringTarget(vm, weak), "wait did not retain the original string until cancellation");
		if (timeout) {
			static_cast<void>(scheduler.Advance(2));
			Require(HasStringTarget(vm, weak) && scheduler.Info(thread).TimeoutTicks == 0, "zero timeout prematurely cleared the original wait string");
			static_cast<void>(scheduler.Advance(1));
			Require(std::get<float>(scheduler.Info(thread).Result) == 2.75f, "timeout changed its original numeric resume value");
		} else {
			scheduler.Stop(thread);
		}

		Require(!HasStringTarget(vm, weak), "cancelled or expired wait strings remained retained until Thread destruction");
		scheduler.Release(thread);
		vm.Close();
		vm.RethrowReleaseFailure();
	}
}

static void TestNotificationWaitReplacement() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	ScriptScheduler scheduler(vm);
	sgre::script::SquirrelHostRoot first_weak;
	sgre::script::SquirrelHostRoot second_weak;
	vm.RegisterNative("replace_wait_fixture", [&](SquirrelVm& active_vm, std::span<const SquirrelValue>) {
		SquirrelValue first = active_vm.CreateString("canonical-replaced-wait");
		first_weak = active_vm.Retain(active_vm.CreateWeakReference(first));
		scheduler.WaitForNotificationValues(*scheduler.CurrentThread(), std::array<SquirrelValue, 1>{first});
		first = {};
		Require(HasStringTarget(active_vm, first_weak), "first wait value was not retained independently of its caller");
		const std::array<SquirrelValue, 1> malformed{std::int32_t{9}};
		RequireFailure([&] {
			scheduler.WaitForNotificationValues(*scheduler.CurrentThread(), malformed);
		},
		    "string values");
		Require(HasStringTarget(active_vm, first_weak), "rejected wait input corrupted a live wait");
		const auto second = active_vm.CreateString("canonical-replacement-wait");
		second_weak = active_vm.Retain(active_vm.CreateWeakReference(second));
		scheduler.WaitForNotificationValues(*scheduler.CurrentThread(), std::array<SquirrelValue, 1>{second});
		Require(!HasStringTarget(active_vm, first_weak), "replacing a wait retained its old canonical string");
		sgre::script::SquirrelNativeResult result;
		result.Suspend = true;

		return result;
	});
	const auto thread = scheduler.Fork(Calling(vm, "replace_wait_fixture"), Arguments(vm));
	static_cast<void>(scheduler.Advance(1));
	Require(HasStringTarget(vm, second_weak), "replacement wait string expired with its temporary callback value");
	scheduler.Stop(thread);
	Require(!HasStringTarget(vm, second_weak), "replacement wait string remained after cancellation");
	scheduler.Release(thread);
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestNativeNotificationPrefixes() {
	for (const bool native_values : {false, true}) {
		sgre::script::SquirrelVmLimits limits;
		limits.ReclaimObjects = true;
		SquirrelVm vm(limits);
		ScriptScheduler scheduler(vm);
		std::array<sgre::script::SquirrelHostRoot, 2> weak;
		vm.RegisterNative("prefix_wait_fixture", [&](SquirrelVm& active_vm, std::span<const SquirrelValue>) {
			const std::array<std::string, 2> names{std::string("prefix\0first", 12), std::string("prefix\0last", 11)};
			const std::array<SquirrelValue, 2> values{active_vm.CreateString(names[0]), active_vm.CreateString(names[1])};
			weak[0] = active_vm.Retain(active_vm.CreateWeakReference(values[0]));
			weak[1] = active_vm.Retain(active_vm.CreateWeakReference(values[1]));
			if (native_values) {
				scheduler.WaitForNotificationValues(*scheduler.CurrentThread(), values);
			} else {
				scheduler.WaitForNotifications(*scheduler.CurrentThread(), names);
			}

			sgre::script::SquirrelNativeResult result;
			result.Suspend = true;

			return result;
		});
		const auto thread = scheduler.Fork(Calling(vm, "prefix_wait_fixture"), Arguments(vm));
		static_cast<void>(scheduler.Advance(1));
		scheduler.Notify("prefix");
		if (native_values) {
			Require(!HasStringTarget(vm, weak[0]) && HasStringTarget(vm, weak[1]), "native strcmp matching failed to select the last original value with an equal NUL-terminated prefix");
			static_cast<void>(scheduler.Advance(0));
			Require(sgre::script::StringText(scheduler.Info(thread).Result) == std::string("prefix\0last", 11), "native notification lost the full extent of its last original matched string");
			Require(std::get<sgre::script::SquirrelString>(scheduler.Info(thread).Result).SharesIdentity(std::get<sgre::script::SquirrelString>(vm.ResolveWeakReference(weak[1].Value()))), "native prefix comparison reconstructed the matched value's identity");
		} else {
			Require(HasStringTarget(vm, weak[0]) && HasStringTarget(vm, weak[1]) && scheduler.Info(thread).Status == ScriptThreadStatus::Running, "native prefix semantics changed generic scheduler full-byte notification equality");
			scheduler.Notify(std::string("prefix\0first", 12));
			Require(HasStringTarget(vm, weak[0]) && !HasStringTarget(vm, weak[1]), "generic scheduler did not transfer its exact full-byte matching value");
			static_cast<void>(scheduler.Advance(0));
			Require(sgre::script::StringText(scheduler.Info(thread).Result) == std::string("prefix\0first", 12), "generic scheduler notification extent changed");
		}

		scheduler.Release(thread);
		Require(!HasStringTarget(vm, weak[0]) && !HasStringTarget(vm, weak[1]), "notification result or losing wait value remained after context release");
		vm.Close();
		vm.RethrowReleaseFailure();
	}
}

static void TestHandlesStopsAndLimits() {
	SquirrelVm vm;
	sgre::script::ScriptSchedulerLimits limits;
	limits.MaxThreads = 1;
	ScriptScheduler scheduler(vm, limits);
	const auto target = vm.CreateClosure(Program({Instruction(2, 1, 1), Instruction(19, 1, 1)}));
	const auto thread = scheduler.Fork(target, Arguments(vm));
	RequireFailure([&] {
		static_cast<void>(scheduler.Fork(target, Arguments(vm)));
	},
	    "allocation limit");
	RequireFailure([&] {
		scheduler.Release(thread);
	},
	    "active script thread");
	scheduler.SetExecutionStopped(thread, true);
	Require(scheduler.Advance(std::numeric_limits<std::int32_t>::max()).ActiveThreads == 1, "stopped execution consumed its closure");
	static_cast<void>(scheduler.Advance(1));
	Require(scheduler.Info(thread).ElapsedTicks == std::numeric_limits<std::int32_t>::min(), "thread elapsed counter did not preserve native int32 wrapping");
	scheduler.SetExecutionStopped(thread, false);
	static_cast<void>(scheduler.Advance(0));
	scheduler.Release(thread);
	const auto replacement = scheduler.Fork(target, Arguments(vm));
	Require(replacement.Index == thread.Index && replacement.Generation != thread.Generation, "released thread slot did not retain checked generations");
	RequireFailure([&] {
		static_cast<void>(scheduler.Info(thread));
	},
	    "stale");
	ScriptScheduler other(vm);
	RequireFailure([&] {
		static_cast<void>(other.Info(replacement));
	},
	    "another scheduler");
	RequireFailure([&] {
		static_cast<void>(scheduler.Advance(-1));
	},
	    "nonnegative");
	RequireFailure([&] {
		static_cast<void>(scheduler.Fork(std::int32_t{1}, Arguments(vm)));
	},
	    "closure");
}

static void TestPreparationAndResultRoots() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	limits.MaxContexts = 2;
	SquirrelVm vm(limits);
	sgre::script::ScriptSchedulerLimits scheduler_limits;
	scheduler_limits.MaxThreads = 1;
	ScriptScheduler scheduler(vm, scheduler_limits);
	auto program = Program({Instruction(19, 1, 1)});
	program.MainFunction.Parameters.push_back(std::string("value"));
	auto callable = vm.CreateClosure(std::move(program));
	auto argument = vm.CreateTable();
	vm.NewSlot(argument, std::string("sentinel"), std::int32_t{47});
	const auto callable_identity = vm.BorrowObjectIdentity(callable);
	const auto argument_identity = vm.BorrowObjectIdentity(argument);
	std::array<SquirrelValue, 2> arguments{vm.RootTable(), argument};
	const auto thread = scheduler.Fork(callable, arguments);
	Require(vm.RetainedReferenceCount(callable) == 1 && vm.RetainedReferenceCount(argument) == 0, "preparation must root the target and one containing argument array");
	arguments[1] = {};
	argument = {};
	callable = {};
	static_cast<void>(vm.CollectGarbage());
	Require(vm.ResolveObjectIdentity(callable_identity) && vm.ResolveObjectIdentity(argument_identity), "scheduler preparation was absent from the native external root set");
	static_cast<void>(scheduler.Advance(1));
	static_cast<void>(vm.CollectGarbage());
	Require(vm.ResolveObjectIdentity(argument_identity) && vm.RetainedReferenceCount(scheduler.Info(thread).Result) == 1, "stored Thread result failed its explicit ObjectInfo ownership");
	scheduler.Release(thread);
	static_cast<void>(vm.CollectGarbage());
	Require(!vm.ResolveObjectIdentity(argument_identity) && !vm.ResolveObjectIdentity(callable_identity), "release retained callable, arguments, result, or child-context state");
	const auto replacement = scheduler.CreateDormant();
	Require(replacement.Index == thread.Index && replacement.Generation != thread.Generation, "reclaimed scheduler slot failed checked generation reuse");
	scheduler.Release(replacement);
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestPendingLoadOwnerHandoff() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	ScriptScheduler scheduler(vm);
	auto callable = vm.CreateClosure(Program({Instruction(2, 1, 61), Instruction(19, 1, 1)}));
	const auto identity = vm.BorrowObjectIdentity(callable);
	auto prepared = std::make_shared<sgre::script::SquirrelHostRoot>(vm.Retain(callable));
	std::weak_ptr<sgre::script::SquirrelHostRoot> weak_owner = prepared;
	bool ready = false;
	const auto thread = scheduler.ForkLoading([prepared, &ready]() -> std::optional<SquirrelValue> {
		return ready ? std::optional<SquirrelValue>(prepared->Value()) : std::nullopt;
	},
	    Arguments(vm));
	prepared.reset();
	callable = {};
	static_cast<void>(vm.CollectGarbage());
	static_cast<void>(scheduler.Advance(1));
	Require(vm.ResolveObjectIdentity(identity) && !weak_owner.expired() && scheduler.Info(thread).Status == ScriptThreadStatus::LoadingFile, "the requested load lost its explicit prepared closure owner while pending");
	ready = true;
	static_cast<void>(scheduler.Advance(1));
	Require(std::get<std::int32_t>(scheduler.Info(thread).Result) == 61 && weak_owner.expired(), "load handoff retained the poll owner or lost the ready callable");
	scheduler.Release(thread);
	static_cast<void>(vm.CollectGarbage());
	Require(!vm.ResolveObjectIdentity(identity), "completed loaded closure survived child-context release");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestCompletionReleaseGuard() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	ScriptScheduler scheduler(vm);
	const auto callable = vm.CreateClosure(Program({Instruction(2, 1, 1), Instruction(19, 1, 1)}));
	const auto thread = scheduler.Fork(callable, Arguments(vm));
	const auto instance = vm.CreateInstance(vm.CreateClass());
	scheduler.AttachNativeInstance(thread, instance, [&] {
		RequireFailure([&] {
			scheduler.Stop(thread);
		},
		    "completing");
		RequireFailure([&] {
			scheduler.Release(thread);
		},
		    "active");
	});
	static_cast<void>(scheduler.Advance(1));
	Require(scheduler.Info(thread).Status == ScriptThreadStatus::None && scheduler.Info(thread).Error.empty(), "guarded completion corrupted the stopped state");
	scheduler.Release(thread);
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestPendingLoadDoesNotOwnChildContext() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	limits.MaxContexts = 1;
	SquirrelVm vm(limits);
	ScriptScheduler scheduler(vm);
	const auto prepared = std::make_shared<sgre::script::SquirrelHostRoot>(vm.Retain(vm.CreateClosure(Program({Instruction(2, 1, 61), Instruction(19, 1, 1)}))));
	bool ready = false;
	std::size_t polls = 0;
	const std::array<SquirrelValue, 1> receiver{vm.RootTable()};
	const auto thread = scheduler.ForkLoading([prepared, &ready, &polls]() -> std::optional<SquirrelValue> {
		++polls;

		return ready ? std::optional<SquirrelValue>(prepared->Value()) : std::nullopt;
	},
	    receiver);
	static_cast<void>(vm.CollectGarbage());
	Require(scheduler.Advance(1).Completed.empty() && polls == 1 && scheduler.Info(thread).Status == ScriptThreadStatus::LoadingFile, "pending resource reserved an unavailable child context");
	ready = true;
	Require(scheduler.Advance(1).Completed == std::vector<sgre::script::ScriptThreadHandle>{thread} && scheduler.Info(thread).Error.find("context limit") != std::string::npos, "ready load did not allocate its child context at the execution boundary");
	scheduler.Release(thread);
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestFunctionContextAllocationAtFirstAdvance() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	limits.MaxContexts = 4;
	SquirrelVm vm(limits);
	ScriptScheduler scheduler(vm);
	const std::array<sgre::script::SquirrelContextHandle, 3> occupied{vm.CreateContext(), vm.CreateContext(), vm.CreateContext()};
	const std::array<SquirrelValue, 1> receiver{vm.RootTable()};
	std::vector<sgre::script::ScriptThreadHandle> handles;
	for (const auto result : std::array<std::int32_t, 3>{11, 22, 33}) {
		handles.push_back(scheduler.Fork(vm.CreateClosure(Program({Instruction(2, 1, result), Instruction(19, 1, 1)})), receiver));
	}

	for (const auto context : occupied) {
		vm.ReleaseContext(context);
	}

	static_cast<void>(vm.CollectGarbage());
	Require(scheduler.Advance(1).Completed == handles, "unstarted function contexts consumed capacity or failed first execution");
	for (std::size_t index = 0; index < handles.size(); ++index) {
		Require(std::get<std::int32_t>(scheduler.Info(handles[index]).Result) == static_cast<std::int32_t>((index + 1) * 11), "lazy first execution changed the stored result");
	}

	RequireFailure([&] {
		static_cast<void>(vm.CreateContext());
	},
	    "context limit");
	for (const auto handle : handles) {
		scheduler.Release(handle);
	}

	const auto replacement = vm.CreateContext();
	vm.ReleaseContext(replacement);
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestReadyContextInheritsCurrentRootHandler() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	vm.InstallRecoveredBaseLibrary();
	ScriptScheduler scheduler(vm);
	std::vector<std::int32_t> handlers;
	const auto install_handler = [&](std::int32_t identifier) {
		const auto handler = vm.CreateNativeClosure("fixture.error_handler", [&, identifier](SquirrelVm&, const auto arguments) {
			Require(arguments.size() == 2 && std::get<std::int32_t>(arguments[1]) == 42, "child error handler lost the native error value");
			handlers.push_back(identifier);

			return sgre::script::SquirrelNativeResult{};
		});
		const std::array<SquirrelValue, 2> arguments{vm.RootTable(), handler};
		static_cast<void>(vm.Invoke(vm.GetSlot(vm.RootTable(), std::string("seterrorhandler")), arguments));
	};
	install_handler(1);
	const std::array<SquirrelValue, 1> receiver{vm.RootTable()};
	const auto thread = scheduler.Fork(vm.CreateClosure(Program({Instruction(2, 1, 42), Instruction(58, 1)})), receiver);
	install_handler(2);
	static_cast<void>(vm.CollectGarbage());
	static_cast<void>(scheduler.Advance(1));
	Require(handlers == std::vector<std::int32_t>{2} && std::get<std::int32_t>(scheduler.Info(thread).Result) == 42, "child inherited the fork-time handler instead of the creating root VM's ready-time handler");
	scheduler.Release(thread);
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestChildRootReceiverAtReadiness() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	ScriptScheduler scheduler(vm);
	auto old_root = vm.RootTable();
	const auto old_identity = vm.BorrowObjectIdentity(old_root);
	const auto root_target = vm.CreateClosure(Program({Instruction(19, 1, 0)}));
	auto argument_program = Program({Instruction(19, 1, 1)});
	argument_program.MainFunction.Parameters.push_back(std::string("value"));
	const auto argument_target = vm.CreateClosure(std::move(argument_program));
	auto argument = vm.CreateTable();
	const auto argument_identity = vm.BorrowObjectIdentity(argument);
	std::array<SquirrelValue, 1> parameters{argument};
	const auto function = scheduler.Fork(root_target, {}, 10, sgre::script::ScriptThreadReceiver::ChildRoot);
	const auto parameterized = scheduler.Fork(argument_target, parameters, 10, sgre::script::ScriptThreadReceiver::ChildRoot);
	bool ready = false;
	auto prepared = std::make_shared<sgre::script::SquirrelHostRoot>(vm.Retain(root_target));
	const auto loading = scheduler.ForkLoading([prepared, &ready]() -> std::optional<SquirrelValue> {
		return ready ? std::optional<SquirrelValue>(prepared->Value()) : std::nullopt;
	},
	    {}, 10, sgre::script::ScriptThreadReceiver::ChildRoot);
	prepared.reset();
	parameters = {};
	argument = {};
	old_root = {};
	const auto function_root = vm.CreateTable();
	vm.SetRootTable(function_root);
	static_cast<void>(vm.CollectGarbage());
	Require(!vm.ResolveObjectIdentity(old_identity) && vm.ResolveObjectIdentity(argument_identity), "native pending parameters retained a fork-time root or lost their own strong owner");
	Require(scheduler.Advance(1).Completed == std::vector<ScriptThreadHandle>{function, parameterized}, "native child-root receiver failed first execution or started a pending file");
	Require(vm.BorrowObjectIdentity(scheduler.Info(function).Result) == vm.BorrowObjectIdentity(function_root), "native function used the fork-time root instead of its new child root");
	Require(vm.BorrowObjectIdentity(scheduler.Info(parameterized).Result) == argument_identity, "deferred receiver shifted or released the first native user parameter");
	const auto file_root = vm.CreateTable();
	vm.SetRootTable(file_root);
	ready = true;
	Require(scheduler.Advance(1).Completed == std::vector<ScriptThreadHandle>{loading}, "native pending file did not execute at readiness");
	Require(vm.BorrowObjectIdentity(scheduler.Info(loading).Result) == vm.BorrowObjectIdentity(file_root), "native file used its fork-time root instead of its ready-time child root");
	for (const auto thread : std::array<ScriptThreadHandle, 3>{function, parameterized, loading}) {
		scheduler.Release(thread);
	}

	static_cast<void>(vm.CollectGarbage());
	Require(!vm.ResolveObjectIdentity(argument_identity), "native released parameter/result remained rooted");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestNativeCreatorWhileAnotherContextExecutes() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	ScriptScheduler scheduler(vm);
	const auto base_root = vm.RootTable();
	const auto other = vm.CreateContext();
	const auto other_root = vm.CreateTable();
	vm.SetRootTableInContext(other, other_root);
	const auto target = vm.CreateClosure(Program({Instruction(19, 1, 0)}));
	const auto explicit_thread = scheduler.Fork(target, std::array<SquirrelValue, 1>{other_root});
	const auto native_thread = scheduler.Fork(target, {}, 10, sgre::script::ScriptThreadReceiver::ChildRoot);
	const auto tick = vm.CreateNativeClosure("fixture.tick_from_other", [&](SquirrelVm& active_vm, const auto) {
		Require(active_vm.CurrentContext() == other && active_vm.BorrowObjectIdentity(active_vm.RootTable()) == active_vm.BorrowObjectIdentity(other_root), "fixture did not enter its separate active root");
		Require(scheduler.Advance(1).Completed == std::vector<ScriptThreadHandle>{explicit_thread, native_thread}, "nested host advance failed both scheduler receiver modes");
		Require(active_vm.CurrentContext() == other && active_vm.BorrowObjectIdentity(active_vm.RootTable()) == active_vm.BorrowObjectIdentity(other_root), "native child creation displaced the executing context's root");

		return sgre::script::SquirrelNativeResult{};
	});
	static_cast<void>(vm.InvokeInContext(other, tick, std::array<SquirrelValue, 1>{other_root}));
	Require(vm.BorrowObjectIdentity(scheduler.Info(native_thread).Result) == vm.BorrowObjectIdentity(base_root), "native Thread created from the executing foreign context instead of the base VM");
	Require(vm.BorrowObjectIdentity(scheduler.Info(explicit_thread).Result) == vm.BorrowObjectIdentity(other_root), "native Thread path changed generic Scheduler explicit receiver behavior");
	scheduler.Release(native_thread);
	scheduler.Release(explicit_thread);
	vm.ReleaseContext(other);
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void RunFixture(std::string_view name, void (*fixture)()) {
	try {
		fixture();
	} catch (const std::exception& error) {
		throw std::runtime_error(std::string(name) + ": " + error.what());
	}
}

int main() {
	try {
		RunFixture("TestPriorityAndSnapshot", TestPriorityAndSnapshot);
		RunFixture("TestChildWakeAndError", TestChildWakeAndError);
		RunFixture("TestTimeoutAndNotification", TestTimeoutAndNotification);
		RunFixture("TestCanonicalNotificationOwnership", TestCanonicalNotificationOwnership);
		RunFixture("TestNotificationCancellationOwnership", TestNotificationCancellationOwnership);
		RunFixture("TestNotificationWaitReplacement", TestNotificationWaitReplacement);
		RunFixture("TestNativeNotificationPrefixes", TestNativeNotificationPrefixes);
		RunFixture("TestHandlesStopsAndLimits", TestHandlesStopsAndLimits);
		RunFixture("TestPreparationAndResultRoots", TestPreparationAndResultRoots);
		RunFixture("TestPendingLoadOwnerHandoff", TestPendingLoadOwnerHandoff);
		RunFixture("TestCompletionReleaseGuard", TestCompletionReleaseGuard);
		RunFixture("TestPendingLoadDoesNotOwnChildContext", TestPendingLoadDoesNotOwnChildContext);
		RunFixture("TestFunctionContextAllocationAtFirstAdvance", TestFunctionContextAllocationAtFirstAdvance);
		RunFixture("TestReadyContextInheritsCurrentRootHandler", TestReadyContextInheritsCurrentRootHandler);
		RunFixture("TestChildRootReceiverAtReadiness", TestChildRootReceiverAtReadiness);
		RunFixture("TestNativeCreatorWhileAnotherContextExecutes", TestNativeCreatorWhileAnotherContextExecutes);
		std::cout << "script scheduler fixtures passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
