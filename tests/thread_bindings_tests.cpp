#include "sgre/script/thread_bindings.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using sgre::script::BytecodeProgram;
using sgre::script::ScriptScheduler;
using sgre::script::ScriptThreadBindings;
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
	program.MainFunction.SourceName = std::string("independent-thread.nut");
	program.MainFunction.Parameters.push_back(std::string("this"));
	program.MainFunction.StackSize = 4;
	program.MainFunction.Instructions = std::move(instructions);

	return program;
}

static SquirrelValue NativeCall(SquirrelVm& vm, std::string name, std::optional<SquirrelValue> argument = {}) {
	auto program = Program({Instruction(8, 1, 0, 0, 2), Instruction(6, 3, 1, 2, 1), Instruction(19, 1, 3)});
	program.MainFunction.Literals.push_back(std::move(name));
	if (argument) {
		const auto* text = sgre::script::TryStringText(*argument);
		if (!text) {
			throw std::runtime_error("fixture target must be a string");
		}

		program.MainFunction.Literals.push_back(*text);
		program.MainFunction.Instructions.insert(program.MainFunction.Instructions.begin() + 1, Instruction(1, 3, 1));
		program.MainFunction.Instructions[2].Argument3 = 2;
	}

	return vm.CreateClosure(std::move(program));
}

static void TestSystemLoadingAndIdentity() {
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	ScriptScheduler scheduler(vm);
	const auto child = vm.CreateClosure(Program({Instruction(2, 1, 93), Instruction(19, 1, 1)}));
	std::size_t polls = 0;
	ScriptThreadBindings bindings(vm, scheduler, [&](std::string_view filename) {
		Require(filename == "script/independent.nut", "script request identity changed");

		return [&]() -> std::optional<SquirrelValue> {
			++polls;
			if (polls == 1) {
				return {};
			}

			return child;
		};
	});
	const auto parent = bindings.Spawn(NativeCall(vm, "system", std::string("script/independent.nut")));
	Require(vm.Type(parent) == sgre::script::SquirrelValueType::Instance, "native Thread is not a real instance");
	const auto handle = bindings.ThreadHandle(parent);
	Require(scheduler.Advance(1).ActiveThreads == 2, "native system did not fork and suspend");
	const auto active = scheduler.ActiveThreads();
	Require(scheduler.Info(active[1]).Status == ScriptThreadStatus::LoadingFile, "string target skipped the loading-file phase");
	static_cast<void>(scheduler.Advance(1));
	Require(scheduler.Info(active[1]).Status == ScriptThreadStatus::LoadingFile && scheduler.Info(active[1]).ElapsedTicks == 0, "pending resource accumulated execution ticks");
	static_cast<void>(scheduler.Advance(1));
	Require(scheduler.Info(active[1]).Status == ScriptThreadStatus::None && scheduler.Info(handle).Status == ScriptThreadStatus::Running, "child completion resumed an earlier parent within the same snapshot");
	static_cast<void>(scheduler.Advance(1));
	Require(std::get<std::int32_t>(scheduler.Info(handle).Result) == 93, "native system lost child return value");
	const auto exit_method = vm.GetSlot(parent, std::string("getExitCode"));
	const std::array<SquirrelValue, 1> receiver{parent};
	Require(std::get<std::int32_t>(vm.Invoke(exit_method, receiver).Value) == 93, "Thread.getExitCode did not expose the actual script result");

	const auto identity = bindings.Spawn(NativeCall(vm, "getCurrentThread"));
	static_cast<void>(scheduler.Advance(1));
	Require(std::get<sgre::script::SquirrelObjectHandle>(scheduler.Info(bindings.ThreadHandle(identity)).Result) == std::get<sgre::script::SquirrelObjectHandle>(identity), "getCurrentThread returned a different native instance");
}

static void TestNativeConstructorAndExpiredOwner() {
	SquirrelVm vm;
	ScriptScheduler scheduler(vm);
	const auto base = vm.CreateClass();
	vm.NewSlot(base, std::string("fixture_base_field"), std::int32_t{21});
	SquirrelValue retained_class;
	{
		ScriptThreadBindings bindings(vm, scheduler, [](std::string_view) {
			return []() -> std::optional<SquirrelValue> {
				throw std::runtime_error("fixture requested an unavailable script");
			};
		},
		    base);
		retained_class = bindings.ThreadClass();
		const std::array<SquirrelValue, 1> arguments{vm.RootTable()};
		const auto instance = vm.Invoke(retained_class, arguments).Value;
		Require(scheduler.Info(bindings.ThreadHandle(instance)).Status == ScriptThreadStatus::None, "argument-free Thread did not stay dormant");
		Require(std::get<std::int32_t>(vm.GetSlot(instance, std::string("fixture_base_field"))) == 21, "native Thread lost its supplied Object base fields");
		try {
			static_cast<void>(vm.Invoke(vm.GetSlot(vm.RootTable(), std::string("getCurrentThread")), arguments));
			throw std::runtime_error("getCurrentThread invented an engine thread outside scheduling");
		} catch (const std::runtime_error& error) {
			Require(std::string_view(error.what()).find("no thread") != std::string_view::npos, error.what());
		}
	}

	const std::array<SquirrelValue, 1> arguments{vm.RootTable()};
	try {
		static_cast<void>(vm.Invoke(vm.GetSlot(vm.RootTable(), std::string("getCurrentTick")), arguments));
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find("expired") != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("expired native bindings were still callable");
}

static void TestReclaimedNativeRegistration() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	limits.MaxContexts = 2;
	SquirrelVm vm(limits);
	sgre::script::ScriptObjectBindings objects(vm);
	std::vector<std::int32_t> events;
	sgre::script::ScriptSchedulerLimits scheduler_limits;
	scheduler_limits.MaxThreads = 1;
	ScriptScheduler scheduler(vm, scheduler_limits);
	ScriptThreadBindings bindings(vm, scheduler, [](std::string_view) {
		return ScriptScheduler::LoadPoll{};
	},
	    objects);
	std::optional<sgre::script::ScriptThreadHandle> handle;
	bool observe_destructor = true;
	vm.NewSlot(bindings.ThreadClass(), std::string("destructor"), vm.CreateNativeClosure("fixture.thread.destructor", [&](SquirrelVm&, const auto arguments) {
		if (observe_destructor) {
			Require(objects.HasInstance(arguments[0]) && handle && scheduler.Info(*handle).Status == ScriptThreadStatus::None, "Thread script destructor lost its native state before release");
			events.push_back(2);
		}

		return sgre::script::SquirrelNativeResult{};
	}));
	auto target = vm.CreateClosure(Program({Instruction(2, 1, 9), Instruction(19, 1, 1)}));
	const auto target_identity = vm.BorrowObjectIdentity(target);
	auto instance = bindings.Spawn(target);
	handle = bindings.ThreadHandle(instance);
	const auto identity = vm.BorrowObjectIdentity(instance);
	const auto weak = vm.Retain(vm.CreateWeakReference(instance));
	const auto listener = vm.CreateInstance(objects.ObjectClass());
	objects.AttachInstance(listener);
	objects.Subscribe(instance, listener, [&](const auto&) {
		Require(scheduler.Info(*handle).Status == ScriptThreadStatus::Running && std::get<std::int32_t>(scheduler.Info(*handle).Result) == 9, "native Thread completion must notify before stop/context cleanup");
		events.push_back(1);

		return false;
	});
	Require(vm.RetainedReferenceCount(instance) == 0 && vm.Length(scheduler.NativeThreadList()) == 1, "active registration must be an array-owned reference, not an external self root");
	instance = {};
	target = {};
	static_cast<void>(vm.CollectGarbage());
	Require(vm.ResolveObjectIdentity(identity).has_value() && vm.ResolveObjectIdentity(target_identity).has_value(), "native registration/callable roots failed before the first tick");
	static_cast<void>(scheduler.Advance(1));
	vm.RethrowReleaseFailure();
	Require(events == std::vector<std::int32_t>{1, 2} && vm.Length(scheduler.NativeThreadList()) == 0 && !vm.ResolveObjectIdentity(identity) && std::holds_alternative<std::monostate>(vm.ResolveWeakReference(weak.Value())), "Thread completion/pruning stranded its native instance or weak target");
	observe_destructor = false;
	const std::array<SquirrelValue, 1> receiver{vm.RootTable()};
	const auto dormant = vm.Invoke(bindings.ThreadClass(), receiver).Value;
	Require(scheduler.Info(bindings.ThreadHandle(dormant)).Status == ScriptThreadStatus::None && vm.Length(scheduler.NativeThreadList()) == 0, "dormant Thread received a registration or failed to reuse released context capacity");
}

static void TestSharedListMutationAndPriority() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	sgre::script::ScriptObjectBindings objects(vm);
	ScriptScheduler scheduler(vm);
	ScriptThreadBindings bindings(vm, scheduler, [](std::string_view) {
		return ScriptScheduler::LoadPoll{};
	},
	    objects);
	std::vector<std::int32_t> order;
	for (const auto value : std::array<std::int32_t, 2>{1, 2}) {
		vm.RegisterNative(value == 1 ? "first" : "second", [&, value](SquirrelVm&, const auto) {
			order.push_back(value);

			return sgre::script::SquirrelNativeResult{};
		});
	}

	const auto first = bindings.Spawn(NativeCall(vm, "first"));
	const auto second = bindings.Spawn(NativeCall(vm, "second"));
	const std::array<SquirrelValue, 1> receiver{vm.RootTable()};
	const auto getter = vm.GetSlot(vm.RootTable(), std::string("getThreadList"));
	const auto list = vm.Invoke(getter, receiver).Value;
	Require(list == vm.Invoke(getter, receiver).Value && list == scheduler.NativeThreadList(), "getThreadList returned a copy instead of the actual rooted native array");
	vm.SetSlot(list, std::int32_t(0), second);
	vm.SetSlot(list, std::int32_t(1), first);
	static_cast<void>(scheduler.Advance(1));
	Require(order == std::vector<std::int32_t>{2, 1} && vm.Length(list) == 0, "native list mutation did not affect stable equal-priority order or forward pruning");
	const auto delayed = bindings.Spawn(NativeCall(vm, "first"));
	const auto removed = vm.RemoveArray(list, 0);
	Require(removed == delayed, "shared native list removal changed the instance identity");
	static_cast<void>(scheduler.Advance(1));
	Require(order == std::vector<std::int32_t>{2, 1}, "a removed native registration still executed");
	vm.AppendArray(list, delayed);
	static_cast<void>(scheduler.Advance(1));
	Require(order == std::vector<std::int32_t>{2, 1, 1} && vm.Length(list) == 0, "reinserted native registration did not execute/prune normally");
}

static void TestDormantDelegateAndClose() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	sgre::script::ScriptObjectBindings objects(vm);
	ScriptScheduler scheduler(vm);
	ScriptThreadBindings bindings(vm, scheduler, [](std::string_view) {
		return ScriptScheduler::LoadPoll{};
	},
	    objects);
	auto environment = vm.CreateTable();
	const auto environment_identity = vm.BorrowObjectIdentity(environment);
	std::array<SquirrelValue, 2> arguments{vm.RootTable(), environment};
	auto instance = vm.Invoke(bindings.ThreadClass(), arguments).Value;
	vm.NewSlot(vm.RootTable(), std::string("fixture-thread"), instance);
	Require(objects.Delegate(instance) == environment && vm.RetainedReferenceCount(instance) == 0, "Thread base delegate/self unexpectedly retained their weak targets");
	arguments[1] = {};
	environment = {};
	static_cast<void>(vm.CollectGarbage());
	Require(!vm.ResolveObjectIdentity(environment_identity) && std::holds_alternative<std::monostate>(objects.Delegate(instance)), "cached Thread delegate wrapper marked its target");
	const auto identity = vm.BorrowObjectIdentity(instance);
	const auto handle = bindings.ThreadHandle(instance);
	vm.Close();
	vm.RethrowReleaseFailure();
	Require(vm.IsClosed() && !vm.ResolveObjectIdentity(identity), "explicit Close left Thread identity alive");
	try {
		static_cast<void>(scheduler.Info(handle));
		throw std::runtime_error("closed Thread scheduler handle remained valid");
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find("stale") != std::string_view::npos || std::string_view(error.what()).find("closed") != std::string_view::npos, error.what());
	}
	instance = {};
}

static void TestReclaimedSystemAndSubscriptions() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	limits.MaxContexts = 3;
	SquirrelVm vm(limits);
	sgre::script::ScriptObjectBindings objects(vm);
	ScriptScheduler scheduler(vm);
	auto prepared = std::make_shared<sgre::script::SquirrelHostRoot>(vm.Retain(vm.CreateClosure(Program({Instruction(2, 1, 93), Instruction(19, 1, 1)}))));
	bool ready = false;
	ScriptThreadBindings bindings(vm, scheduler, [prepared, &ready](std::string_view) {
		return [prepared, &ready]() -> std::optional<SquirrelValue> {
			return ready ? std::optional<SquirrelValue>(prepared->Value()) : std::nullopt;
		};
	},
	    objects);
	auto parent = bindings.Spawn(NativeCall(vm, "system", std::string("fixture.nut")));
	const auto parent_identity = vm.BorrowObjectIdentity(parent);
	const auto parent_weak = vm.Retain(vm.CreateWeakReference(parent));
	const auto parent_handle = bindings.ThreadHandle(parent);
	const auto listener = vm.CreateInstance(objects.ObjectClass());
	objects.AttachInstance(listener);
	std::size_t completions = 0;
	objects.Subscribe(parent, listener, [&](const auto&) {
		Require(std::get<std::int32_t>(scheduler.Info(parent_handle).Result) == 93, "native system subscription failed to deliver the stored child value");
		++completions;

		return false;
	});
	static_cast<void>(scheduler.Advance(1));
	const auto child_identity = vm.BorrowObjectIdentity(vm.GetSlot(scheduler.NativeThreadList(), 1));
	const auto child_weak = vm.Retain(vm.CreateWeakReference(*vm.ResolveObjectIdentity(child_identity)));
	Require(vm.RetainedReferenceCount(parent) == 0 && vm.RetainedReferenceCount(*vm.ResolveObjectIdentity(child_identity)) == 0, "system wait strongly rooted self or its weak child target outside native arrays");
	parent = {};
	static_cast<void>(vm.CollectGarbage());
	Require(vm.ResolveObjectIdentity(parent_identity) && vm.ResolveObjectIdentity(child_identity), "active/subscriber ownership lost the waiting parent or pending child");
	static_cast<void>(scheduler.Advance(1));
	ready = true;
	static_cast<void>(scheduler.Advance(1));
	vm.RethrowReleaseFailure();
	Require(!vm.ResolveObjectIdentity(child_identity) && std::holds_alternative<std::monostate>(vm.ResolveWeakReference(child_weak.Value())) && scheduler.Info(parent_handle).Status == ScriptThreadStatus::Running, "finished child did not release its weak target while retaining parent resume state");
	static_cast<void>(scheduler.Advance(1));
	vm.RethrowReleaseFailure();
	Require(completions == 1 && !vm.ResolveObjectIdentity(parent_identity) && std::holds_alternative<std::monostate>(vm.ResolveWeakReference(parent_weak.Value())) && vm.Length(scheduler.NativeThreadList()) == 0, "system completion left a parent/subscriber/context cycle alive");
	const std::array<SquirrelValue, 1> receiver{vm.RootTable()};
	static_cast<void>(vm.Invoke(bindings.ThreadClass(), receiver));
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestReleasePreflightAndIdleChild() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	limits.MaxContexts = 3;
	SquirrelVm vm(limits);
	sgre::script::ScriptObjectBindings objects(vm);
	ScriptScheduler scheduler(vm);
	ScriptThreadBindings bindings(vm, scheduler, [](std::string_view) {
		return ScriptScheduler::LoadPoll{};
	},
	    objects);
	const std::array<SquirrelValue, 1> receiver{vm.RootTable()};
	const auto idle = bindings.Spawn(vm.CreateClosure(Program({Instruction(2, 1, 17), Instruction(19, 1, 1)})));
	static_cast<void>(scheduler.Advance(1));
	const auto handle = bindings.ThreadHandle(idle);
	const auto listener = vm.CreateInstance(objects.ObjectClass());
	objects.AttachInstance(listener);
	objects.Subscribe(idle, listener, [&](const auto&) {
		try {
			bindings.ReleaseInstance(idle);
			throw std::runtime_error("Thread release during notify was accepted");
		} catch (const std::runtime_error& error) {
			Require(std::string_view(error.what()).find("notification") != std::string_view::npos, error.what());
		}

		Require(objects.HasInstance(idle) && bindings.ThreadHandle(idle) == handle && scheduler.Info(handle).Status == ScriptThreadStatus::None, "rejected Thread release destroyed native/base/context state");

		return false;
	});
	objects.Notify(idle, true);
	bool released = false;
	vm.RegisterNative("release_idle_child", [&](SquirrelVm&, const auto) {
		bindings.ReleaseInstance(idle);
		released = true;

		return sgre::script::SquirrelNativeResult{};
	});
	const auto parent = bindings.Spawn(NativeCall(vm, "release_idle_child"));
	static_cast<void>(scheduler.Advance(1));
	Require(released && !objects.HasInstance(idle) && scheduler.Info(bindings.ThreadHandle(parent)).Error.empty(), "an executing parent blocked idle native child/context release");
	const auto replacement = vm.Invoke(bindings.ThreadClass(), receiver).Value;
	Require(bindings.ThreadHandle(replacement).Index == handle.Index && bindings.ThreadHandle(replacement).Generation != handle.Generation, "idle child context did not permit checked generation reuse");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestOwnerExpiryAndSuspendedClose() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	limits.MaxContexts = 2;
	SquirrelVm vm(limits);
	sgre::script::ScriptObjectBindings objects(vm);
	ScriptScheduler scheduler(vm);
	SquirrelValue retained;
	sgre::script::ScriptThreadHandle handle;
	{
		ScriptThreadBindings bindings(vm, scheduler, [](std::string_view) {
			return ScriptScheduler::LoadPoll{};
		},
		    objects);
		retained = bindings.Spawn(NativeCall(vm, "wait", std::string("fixture-notification")));
		handle = bindings.ThreadHandle(retained);
		static_cast<void>(scheduler.Advance(1));
		Require(scheduler.Info(handle).Status == ScriptThreadStatus::Running, "wait did not retain its suspended native child context");
	}

	Require(!objects.HasInstance(retained) && vm.Length(scheduler.NativeThreadList()) == 0, "expired Thread binding owner retained active base/context registration");
	const auto replacement = scheduler.CreateDormant();
	Require(replacement.Index == handle.Index && replacement.Generation != handle.Generation, "owner cleanup failed to free its child context/slot");
	scheduler.Release(replacement);
	const std::array<SquirrelValue, 1> receiver{vm.RootTable()};
	try {
		static_cast<void>(vm.Invoke(vm.GetSlot(vm.RootTable(), std::string("getCurrentTick")), receiver));
		throw std::runtime_error("expired Thread callback remained callable");
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find("expired") != std::string_view::npos, error.what());
	}

	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestReleaseFutureSnapshotEntry() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	sgre::script::ScriptObjectBindings objects(vm);
	ScriptScheduler scheduler(vm);
	ScriptThreadBindings bindings(vm, scheduler, [](std::string_view) {
		return ScriptScheduler::LoadPoll{};
	},
	    objects);
	std::optional<sgre::script::SquirrelObjectIdentity> future_identity;
	std::size_t executions = 0;
	vm.RegisterNative("remove_future", [&](SquirrelVm&, const auto) {
		const auto future = vm.ResolveObjectIdentity(*future_identity);
		Require(future.has_value(), "future snapshot instance expired before its active registration was removed");
		bindings.ReleaseInstance(*future);

		return sgre::script::SquirrelNativeResult{};
	});
	vm.RegisterNative("future_must_not_execute", [&](SquirrelVm&, const auto) {
		++executions;

		return sgre::script::SquirrelNativeResult{};
	});
	const auto first = bindings.Spawn(NativeCall(vm, "remove_future"));
	auto future = bindings.Spawn(NativeCall(vm, "future_must_not_execute"));
	future_identity = vm.BorrowObjectIdentity(future);
	future = {};
	static_cast<void>(scheduler.Advance(1));
	vm.RethrowReleaseFailure();
	Require(executions == 0 && !vm.ResolveObjectIdentity(*future_identity) && scheduler.Info(bindings.ThreadHandle(first)).Error.empty(), "released generation in a borrowed tick snapshot was followed or leaked");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestDestructorFailureCleanup() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	limits.MaxContexts = 2;
	SquirrelVm vm(limits);
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptSchedulerLimits scheduler_limits;
	scheduler_limits.MaxThreads = 1;
	ScriptScheduler scheduler(vm, scheduler_limits);
	ScriptThreadBindings bindings(vm, scheduler, [](std::string_view) {
		return ScriptScheduler::LoadPoll{};
	},
	    objects);
	bool fail_destructor = true;
	vm.NewSlot(bindings.ThreadClass(), std::string("destructor"), vm.CreateNativeClosure("fixture.thread.throwing_destructor", [&](SquirrelVm&, const auto arguments) {
		Require(objects.HasInstance(arguments[0]), "script Thread destructor ran after native/base records were cleared");
		if (fail_destructor) {
			throw std::runtime_error("fixture Thread destructor failure");
		}

		return sgre::script::SquirrelNativeResult{};
	}));
	auto instance = bindings.Spawn(vm.CreateClosure(Program({Instruction(2, 1, 3), Instruction(19, 1, 1)})));
	const auto identity = vm.BorrowObjectIdentity(instance);
	const auto handle = bindings.ThreadHandle(instance);
	instance = {};
	static_cast<void>(scheduler.Advance(1));
	vm.RethrowReleaseFailure();
	Require(vm.LastError() == vm.LastErrorInContext(vm.RootContext()) && sgre::script::StringText(vm.LastError()).find("fixture Thread destructor failure") != std::string::npos, "ignored Thread destructor status lost the VM last-error diagnostic");

	Require(!vm.ResolveObjectIdentity(identity) && vm.Length(scheduler.NativeThreadList()) == 0, "failed destructor stranded native/base/registration state");
	fail_destructor = false;
	const std::array<SquirrelValue, 1> receiver{vm.RootTable()};
	const auto replacement = vm.Invoke(bindings.ThreadClass(), receiver).Value;
	Require(bindings.ThreadHandle(replacement).Index == handle.Index && bindings.ThreadHandle(replacement).Generation != handle.Generation, "failed destructor stranded child-context or scheduler capacity");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestCloseSuspendedNativeThread() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	sgre::script::ScriptObjectBindings objects(vm);
	ScriptScheduler scheduler(vm);
	ScriptThreadBindings bindings(vm, scheduler, [](std::string_view) {
		return ScriptScheduler::LoadPoll{};
	},
	    objects);
	const auto instance = bindings.Spawn(NativeCall(vm, "wait", std::string("fixture-notification")));
	const auto identity = vm.BorrowObjectIdentity(instance);
	static_cast<void>(scheduler.Advance(1));
	Require(vm.Length(scheduler.NativeThreadList()) == 1, "suspended native instance lost its strong active-array owner");
	vm.Close();
	vm.RethrowReleaseFailure();
	Require(vm.IsClosed() && !vm.ResolveObjectIdentity(identity), "Close retained a suspended native Thread/context/wait owner");
}

static void TestDormantDoesNotOwnChildContext() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	limits.MaxContexts = 1;
	SquirrelVm vm(limits);
	sgre::script::ScriptObjectBindings objects(vm);
	ScriptScheduler scheduler(vm);
	sgre::script::ScriptThreadBindings bindings(vm, scheduler, [](std::string_view) {
		return ScriptScheduler::LoadPoll{};
	},
	    objects);
	RequireFailure([&] {
		static_cast<void>(vm.CreateContext());
	},
	    "context limit");
	const std::array<SquirrelValue, 1> receiver{vm.RootTable()};
	for (std::size_t index = 0; index < 5; ++index) {
		auto instance = vm.Invoke(bindings.ThreadClass(), receiver).Value;
		const auto handle = bindings.ThreadHandle(instance);
		Require(scheduler.Info(handle).Status == ScriptThreadStatus::None, "dormant construction created an execution phase");
		bindings.ReleaseInstance(instance);
		Require(vm.Length(scheduler.NativeThreadList()) == 0, "dormant cleanup created a native registration");
	}

	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestNativeThreadUsesReadyChildReceiver() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	sgre::script::ScriptObjectBindings objects(vm);
	ScriptScheduler scheduler(vm);
	ScriptThreadBindings bindings(vm, scheduler, [](std::string_view) {
		return ScriptScheduler::LoadPoll{};
	},
	    objects);
	const auto instance = bindings.Spawn(vm.CreateClosure(Program({Instruction(19, 1, 0)})));
	const auto handle = bindings.ThreadHandle(instance);
	const auto ready_root = vm.CreateTable();
	vm.SetRootTable(ready_root);
	static_cast<void>(vm.CollectGarbage());
	Require(scheduler.Advance(1).Completed == std::vector<sgre::script::ScriptThreadHandle>{handle}, "native Thread did not execute after root replacement");
	Require(vm.BorrowObjectIdentity(scheduler.Info(handle).Result) == vm.BorrowObjectIdentity(ready_root), "Thread adapter captured the spawning root rather than pushing its new child's root");
	bindings.ReleaseInstance(instance);
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestExecOnBaseUsesBaseRootReceiver() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	sgre::script::ScriptObjectBindings objects(vm);
	ScriptScheduler scheduler(vm);
	ScriptThreadBindings bindings(vm, scheduler, [](std::string_view) {
		return ScriptScheduler::LoadPoll{};
	},
	    objects);
	const auto base_root = vm.RootTable();
	const auto exec = vm.GetSlot(base_root, std::string("execOnBase"));
	const auto child = vm.CreateContext();
	const auto child_root = vm.CreateTable();
	vm.SetRootTableInContext(child, child_root);
	bool called = false;
	const auto target = vm.CreateNativeClosure("fixture.base_receiver", [&](SquirrelVm& active_vm, const auto arguments) {
		Require(active_vm.CurrentContext() == active_vm.RootContext() && arguments.size() == 2 && active_vm.BorrowObjectIdentity(arguments[0]) == active_vm.BorrowObjectIdentity(base_root) && std::get<std::int32_t>(arguments[1]) == 17, "execOnBase did not push its target base context's root before user parameters");
		called = true;
		sgre::script::SquirrelNativeResult result;
		result.Value = std::int32_t{42};

		return result;
	});
	const std::array<SquirrelValue, 3> arguments{child_root, target, std::int32_t{17}};
	const auto result = vm.InvokeInContext(child, exec, arguments);
	Require(called && std::holds_alternative<std::monostate>(result.Value) && vm.BorrowObjectIdentity(vm.RootTableInContext(child)) == vm.BorrowObjectIdentity(child_root), "execOnBase returned its scratch value or changed the caller's distinct root");
	vm.ReleaseContext(child);
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestCanonicalNativeWaitResult() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	sgre::script::ScriptObjectBindings objects(vm);
	ScriptScheduler scheduler(vm);
	ScriptThreadBindings bindings(vm, scheduler, [](std::string_view) {
		return ScriptScheduler::LoadPoll{};
	},
	    objects);
	std::array<sgre::script::SquirrelHostRoot, 2> weak;
	sgre::script::SquirrelHostRoot instance;
	{
		const auto first = vm.CreateString(std::string("native-prefix\0first", 19));
		const auto last = vm.CreateString(std::string("native-prefix\0last", 18));
		weak[0] = vm.Retain(vm.CreateWeakReference(first));
		weak[1] = vm.Retain(vm.CreateWeakReference(last));
		auto program = Program({Instruction(10, 5, 1), Instruction(10, 6, 2), Instruction(8, 3, 0, 0, 4), Instruction(6, 7, 3, 4, 3), Instruction(19, 1, 7)});
		program.MainFunction.StackSize = 8;
		program.MainFunction.Parameters.push_back(std::string("first"));
		program.MainFunction.Parameters.push_back(std::string("last"));
		program.MainFunction.Literals.push_back(std::string("wait"));
		const auto callable = vm.CreateClosure(std::move(program));
		instance = vm.Retain(bindings.Spawn(callable, std::array<SquirrelValue, 2>{first, last}));
	}

	const auto handle = bindings.ThreadHandle(instance.Value());
	static_cast<void>(scheduler.Advance(1));
	Require(scheduler.Info(handle).Status == ScriptThreadStatus::Running && std::holds_alternative<sgre::script::SquirrelString>(vm.ResolveWeakReference(weak[0].Value())) && std::holds_alternative<sgre::script::SquirrelString>(vm.ResolveWeakReference(weak[1].Value())), "native wait lost canonical parameters while its child context was suspended");
	const auto notify = vm.GetSlot(vm.RootTable(), std::string("notify"));
	static_cast<void>(vm.Invoke(notify, std::array<SquirrelValue, 2>{vm.RootTable(), vm.CreateString(std::string("native-prefix\0supplied", 22))}));
	static_cast<void>(scheduler.Advance(0));
	Require(scheduler.Info(handle).Status == ScriptThreadStatus::None && sgre::script::StringText(scheduler.Info(handle).Result) == std::string("native-prefix\0last", 18), "native wait/notify adapter did not return the last matching full original value");
	Require(std::get<sgre::script::SquirrelString>(scheduler.Info(handle).Result).SharesIdentity(std::get<sgre::script::SquirrelString>(vm.ResolveWeakReference(weak[1].Value()))), "native wait/notify adapter changed canonical return identity");
	bindings.ReleaseInstance(instance.Value());
	Require(std::holds_alternative<std::monostate>(vm.ResolveWeakReference(weak[0].Value())) && std::holds_alternative<std::monostate>(vm.ResolveWeakReference(weak[1].Value())), "native Thread cleanup retained wait parameters, result or context strings");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestCueStartsOnlyItsLoadingFunction() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	sgre::script::ScriptObjectBindings objects(vm);
	ScriptScheduler scheduler(vm);
	ScriptThreadBindings bindings(vm, scheduler, [](std::string_view) {
		return ScriptScheduler::LoadPoll{};
	}, objects);
	std::int32_t work_calls = 0;
	std::int32_t sibling_calls = 0;
	std::optional<sgre::script::ScriptThreadHandle> child_handle;
	vm.RegisterNative("cueWork", [&](SquirrelVm&, const auto) {
		Require(child_handle && scheduler.CurrentThread() == child_handle && scheduler.Info(*child_handle).ElapsedTicks == 0, "cue did not execute the actual child as current at delta zero");
		Require(scheduler.CurrentTick() == 0 && scheduler.DiffTick() == 0, "cue advanced the scheduler clock");
		RequireFailure([&] { static_cast<void>(scheduler.Advance(0)); }, "cannot advance recursively");
		++work_calls;

		return sgre::script::SquirrelNativeResult::ReturnValue(std::int32_t{42});
	});
	vm.RegisterNative("cueSibling", [&](SquirrelVm&, const auto) {
		++sibling_calls;

		return sgre::script::SquirrelNativeResult{};
	});
	const auto child = vm.Retain(bindings.Spawn(NativeCall(vm, "cueWork")));
	child_handle = bindings.ThreadHandle(child.Value());
	const auto sibling = vm.Retain(bindings.Spawn(NativeCall(vm, "cueSibling")));
	const auto sibling_handle = bindings.ThreadHandle(sibling.Value());
	const auto listener = vm.Retain(vm.CreateInstance(objects.ObjectClass()));
	objects.AttachInstance(listener.Value());
	std::int32_t notifications = 0;
	objects.Subscribe(child.Value(), listener.Value(), [&](const auto&) {
		Require(scheduler.Info(*child_handle).Status == ScriptThreadStatus::Running && scheduler.CurrentThread() == child_handle, "cue completion notified after losing its actual running owner");
		++notifications;

		return false;
	});
	const auto cue = vm.Retain(vm.GetSlot(child.Value(), std::string("cue")));
	const auto ignored = vm.CreateTable();
	vm.NewSlot(ignored, std::string("_tostring"), vm.CreateNativeClosure("fixture.cue.noConversion", [](SquirrelVm&, const auto) -> sgre::script::SquirrelNativeResult {
		throw std::runtime_error("cue converted an ignored extra argument");
	}));
	const std::array<SquirrelValue, 3> arguments{child.Value(), ignored, std::int32_t{17}};
	Require(std::holds_alternative<std::monostate>(vm.Invoke(cue.Value(), arguments).Value), "cue returned a native scratch result");
	Require(work_calls == 1 && notifications == 1 && sibling_calls == 0 && !scheduler.CurrentThread(), "cue visited its sibling or lost current-thread restoration/completion");
	Require(scheduler.Info(*child_handle).Status == ScriptThreadStatus::None && std::get<std::int32_t>(scheduler.Info(*child_handle).Result) == 42, "cue did not publish the actual child result");
	Require(vm.Length(scheduler.NativeThreadList()) == 2 && scheduler.Info(sibling_handle).Status == ScriptThreadStatus::LoadingFunction && !scheduler.Info(sibling_handle).Context, "cue pruned the registration array or prepared another child");
	static_cast<void>(vm.Invoke(cue.Value(), arguments));
	Require(work_calls == 1 && notifications == 1, "cue restarted a completed thread");
	static_cast<void>(scheduler.Advance(0));
	Require(sibling_calls == 1 && vm.Length(scheduler.NativeThreadList()) == 0, "ordinary scheduler tail did not execute/prune after cue");
	RequireFailure([&] { static_cast<void>(vm.Invoke(cue.Value(), std::array<SquirrelValue, 1>{listener.Value()})); }, "host type tag");
	bindings.ReleaseInstance(child.Value());
	RequireFailure([&] { scheduler.Cue(*child_handle); }, "stale");
	bindings.ReleaseInstance(sibling.Value());
	objects.ReleaseInstance(listener.Value());
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestCuePreservesNonloadingWaitsAndStepMode() {
	SquirrelVm vm;
	ScriptScheduler scheduler(vm);
	std::int32_t polls = 0;
	ScriptThreadBindings bindings(vm, scheduler, [&](std::string_view) {
		return [&]() -> std::optional<SquirrelValue> {
			++polls;

			return {};
		};
	});
	const auto file = bindings.Spawn(std::string("script/cue-pending.nut"));
	const auto file_handle = bindings.ThreadHandle(file);
	const auto dormant = vm.Invoke(bindings.ThreadClass(), std::array<SquirrelValue, 1>{vm.RootTable()}).Value;
	const auto dormant_handle = bindings.ThreadHandle(dormant);
	scheduler.Cue(file_handle);
	scheduler.Cue(dormant_handle);
	Require(polls == 0 && scheduler.Info(file_handle).Status == ScriptThreadStatus::LoadingFile && !scheduler.Info(file_handle).Context && scheduler.Info(dormant_handle).Status == ScriptThreadStatus::None && !scheduler.Info(dormant_handle).Context, "cue polled a file or initialized a dormant Thread");

	const auto waiting = bindings.Spawn(NativeCall(vm, "cueWait"));
	const auto waiting_handle = bindings.ThreadHandle(waiting);
	std::int32_t wait_calls = 0;
	vm.RegisterNative("cueWait", [&](SquirrelVm&, const auto) {
		++wait_calls;
		const std::array<SquirrelValue, 1> notifications{std::string("cue-release")};
		scheduler.WaitForNotificationValues(waiting_handle, notifications, std::int32_t{10});
		sgre::script::SquirrelNativeResult result;
		result.Suspend = true;

		return result;
	});
	scheduler.SetExecutionStopped(waiting_handle, true);
	scheduler.Cue(waiting_handle);
	Require(scheduler.Info(waiting_handle).Status == ScriptThreadStatus::Running && scheduler.Info(waiting_handle).Context && scheduler.Info(waiting_handle).ElapsedTicks == 0 && wait_calls == 0, "cue ignored manual-step inhibition or failed to initialize its actual context");
	scheduler.SetExecutionStopped(waiting_handle, false);
	scheduler.Cue(waiting_handle);
	Require(wait_calls == 0, "cue executed a thread whose status was already Running");
	static_cast<void>(scheduler.Advance(3));
	const auto before = scheduler.ObserveWait(waiting_handle);
	Require(wait_calls == 1 && before.ElapsedTicks == 3 && before.TimeoutTicks == 10 && before.Notifications == std::vector<std::string>{"cue-release"}, "ordinary execution did not retain its actual wait");
	static_cast<void>(vm.Invoke(vm.GetSlot(waiting, std::string("cue")), std::array<SquirrelValue, 1>{waiting}));
	const auto after = scheduler.ObserveWait(waiting_handle);
	Require(after.Status == before.Status && after.ElapsedTicks == before.ElapsedTicks && after.TimeoutTicks == before.TimeoutTicks && after.Notifications == before.Notifications && after.Context == before.Context && wait_calls == 1 && polls == 1, "cue cleared or aged a Running thread's wait");
	scheduler.Notify("cue-release");
	static_cast<void>(scheduler.Advance(0));
	Require(scheduler.Info(waiting_handle).Status == ScriptThreadStatus::None && sgre::script::StringText(scheduler.Info(waiting_handle).Result) == "cue-release", "actual notification could not resume the cued thread");
	bindings.ReleaseInstance(waiting);
	bindings.ReleaseInstance(file);
	bindings.ReleaseInstance(dormant);
}

static SquirrelValue CueProbeThenWait(SquirrelVm& vm, std::string probe, std::string notification) {
	auto program = Program({Instruction(8, 1, 0, 0, 2), Instruction(6, 3, 1, 2, 1), Instruction(8, 1, 1, 0, 2), Instruction(1, 3, 2), Instruction(6, 4, 1, 2, 2), Instruction(19, 1, 4)});
	program.MainFunction.StackSize = 5;
	program.MainFunction.Literals = {std::move(probe), std::string("wait"), std::move(notification)};

	return vm.CreateClosure(std::move(program));
}

static void TestCueRestoresExecutingParentAndOwnerLifetime() {
	SquirrelVm vm;
	ScriptScheduler scheduler(vm);
	SquirrelValue retained_cue;
	SquirrelValue retained_child;
	{
		ScriptThreadBindings bindings(vm, scheduler, [](std::string_view) {
			return ScriptScheduler::LoadPoll{};
		});
		std::optional<sgre::script::ScriptThreadHandle> parent_handle;
		std::optional<sgre::script::ScriptThreadHandle> child_handle;
		std::optional<sgre::script::SquirrelContextHandle> parent_context;
		vm.RegisterNative("cueNestedChild", [&](SquirrelVm& active_vm, const auto) {
			Require(scheduler.CurrentThread() == child_handle && parent_context && active_vm.CurrentContext() != parent_context, "nested cue did not enter the actual child context");
			Require(scheduler.Info(*child_handle).ElapsedTicks == 0 && scheduler.CurrentTick() == 7 && scheduler.DiffTick() == 7, "nested cue altered parent/global timing");
			return sgre::script::SquirrelNativeResult::NoValues();
		});
		vm.RegisterNative("cueNestedParent", [&](SquirrelVm& active_vm, const auto) {
			Require(scheduler.CurrentThread() == parent_handle, "parent was not current before its synchronous child cue");
			parent_context = active_vm.CurrentContext();
			retained_child = bindings.Spawn(CueProbeThenWait(vm, "cueNestedChild", "child-done"));
			child_handle = bindings.ThreadHandle(retained_child);
			retained_cue = vm.GetSlot(retained_child, std::string("cue"));
			static_cast<void>(active_vm.InvokeInContext(*parent_context, retained_cue, std::array<SquirrelValue, 1>{retained_child}));
			Require(scheduler.CurrentThread() == parent_handle && active_vm.CurrentContext() == parent_context, "nested cue did not restore its actual executing parent");
			return sgre::script::SquirrelNativeResult::NoValues();
		});
		const auto parent = bindings.Spawn(CueProbeThenWait(vm, "cueNestedParent", "parent-done"));
		parent_handle = bindings.ThreadHandle(parent);
		static_cast<void>(scheduler.Advance(7));
		Require(scheduler.Info(*parent_handle).Error.empty(), "parent native cue failed: " + scheduler.Info(*parent_handle).Error);
		Require(child_handle.has_value(), "parent native cue did not create its actual child");
		Require(scheduler.Info(*child_handle).Error.empty(), "child native cue failed: " + scheduler.Info(*child_handle).Error);
		Require(scheduler.ObserveWait(*parent_handle).Notifications == std::vector<std::string>{"parent-done"} && scheduler.ObserveWait(*child_handle).Notifications == std::vector<std::string>{"child-done"}, "nested cue could not suspend child and caller independently");
		scheduler.Cue(*parent_handle);
		scheduler.Cue(*child_handle);
		Require(scheduler.Info(*parent_handle).ElapsedTicks == 7 && scheduler.Info(*child_handle).ElapsedTicks == 0 && !scheduler.CurrentThread(), "repeated cue resumed a suspended parent/child or retained its active identity");
		scheduler.Notify("child-done");
		static_cast<void>(scheduler.Advance(1));
		Require(scheduler.Info(*child_handle).Status == ScriptThreadStatus::None && scheduler.Info(*parent_handle).Status == ScriptThreadStatus::Running && scheduler.ObserveWait(*parent_handle).Notifications.size() == 1, "child completion disturbed its independent parent wait");
		scheduler.Notify("parent-done");
		static_cast<void>(scheduler.Advance(1));
		Require(sgre::script::StringText(scheduler.Info(*parent_handle).Result) == "parent-done" && sgre::script::StringText(scheduler.Info(*child_handle).Result) == "child-done", "nested cue lost actual resumed values");
	}

	RequireFailure([&] { static_cast<void>(vm.Invoke(retained_cue, std::array<SquirrelValue, 1>{retained_child})); }, "expired");
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
		RunFixture("TestSystemLoadingAndIdentity", TestSystemLoadingAndIdentity);
		RunFixture("TestNativeConstructorAndExpiredOwner", TestNativeConstructorAndExpiredOwner);
		RunFixture("TestReclaimedNativeRegistration", TestReclaimedNativeRegistration);
		RunFixture("TestSharedListMutationAndPriority", TestSharedListMutationAndPriority);
		RunFixture("TestDormantDelegateAndClose", TestDormantDelegateAndClose);
		RunFixture("TestReclaimedSystemAndSubscriptions", TestReclaimedSystemAndSubscriptions);
		RunFixture("TestReleasePreflightAndIdleChild", TestReleasePreflightAndIdleChild);
		RunFixture("TestOwnerExpiryAndSuspendedClose", TestOwnerExpiryAndSuspendedClose);
		RunFixture("TestReleaseFutureSnapshotEntry", TestReleaseFutureSnapshotEntry);
		RunFixture("TestDestructorFailureCleanup", TestDestructorFailureCleanup);
		RunFixture("TestCloseSuspendedNativeThread", TestCloseSuspendedNativeThread);
		RunFixture("TestDormantDoesNotOwnChildContext", TestDormantDoesNotOwnChildContext);
		RunFixture("TestNativeThreadUsesReadyChildReceiver", TestNativeThreadUsesReadyChildReceiver);
		RunFixture("TestExecOnBaseUsesBaseRootReceiver", TestExecOnBaseUsesBaseRootReceiver);
		RunFixture("TestCanonicalNativeWaitResult", TestCanonicalNativeWaitResult);
		RunFixture("TestCueStartsOnlyItsLoadingFunction", TestCueStartsOnlyItsLoadingFunction);
		RunFixture("TestCuePreservesNonloadingWaitsAndStepMode", TestCuePreservesNonloadingWaitsAndStepMode);
		RunFixture("TestCueRestoresExecutingParentAndOwnerLifetime", TestCueRestoresExecutingParentAndOwnerLifetime);
		std::cout << "native thread fixtures passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
