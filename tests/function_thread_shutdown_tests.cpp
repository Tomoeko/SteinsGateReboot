#include "sgre/script/object_bindings.hpp"
#include "sgre/script/script_scheduler.hpp"
#include "sgre/script/thread_bindings.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

using sgre::script::ScriptObjectBindings;
using sgre::script::ScriptScheduler;
using sgre::script::ScriptThreadBindings;
using sgre::script::ScriptThreadHandle;
using sgre::script::ScriptThreadStatus;
using sgre::script::SquirrelValue;
using sgre::script::SquirrelVm;

static void Require(bool value, std::string_view message) {
	if (!value) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view message) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(message) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("missing expected failure: " + std::string(message));
}

static sgre::script::SquirrelInstruction Instruction(std::uint8_t opcode, std::uint8_t destination, std::int32_t argument1 = 0, std::uint8_t argument2 = 0, std::uint8_t argument3 = 0) {
	sgre::script::SquirrelInstruction value;
	value.Opcode = opcode;
	value.Argument0 = destination;
	value.Argument1 = argument1;
	value.Argument2 = argument2;
	value.Argument3 = argument3;

	return value;
}

static sgre::script::BytecodeProgram Program(std::vector<sgre::script::SquirrelInstruction> instructions) {
	sgre::script::BytecodeProgram value;
	value.MainFunction.SourceName = std::string("independent-driver-shutdown.nut");
	value.MainFunction.Name = std::string("fixture");
	value.MainFunction.Parameters.push_back(std::string("this"));
	value.MainFunction.StackSize = 4;
	value.MainFunction.Instructions = std::move(instructions);

	return value;
}

static SquirrelValue IdleFunction(SquirrelVm& vm) {
	return vm.CreateClosure(Program({Instruction(2, 1, 17), Instruction(19, 1, 1)}));
}

static SquirrelValue NativeCaller(SquirrelVm& vm, std::string name) {
	auto program = Program({Instruction(8, 1, 0, 0, 2), Instruction(6, 3, 1, 2, 1), Instruction(19, 1, 3)});
	program.MainFunction.Literals.push_back(std::move(name));

	return vm.CreateClosure(std::move(program));
}

static sgre::script::SquirrelVmLimits ReclaimingLimits() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;

	return limits;
}

static void TestOrderedArgumentRetirement() {
	SquirrelVm vm(ReclaimingLimits());
	ScriptObjectBindings objects(vm);
	ScriptScheduler scheduler(vm);
	ScriptThreadBindings bindings(vm, scheduler, [](std::string_view) {
		return ScriptScheduler::LoadPoll{};
	},
	    objects);
	std::vector<std::uint32_t> order;
	std::vector<ScriptThreadHandle> handles;
	std::vector<sgre::script::SquirrelHostRoot> owners;
	std::vector<sgre::script::SquirrelObjectIdentity> arguments;
	const auto listener = vm.Retain(vm.CreateInstance(objects.ObjectClass()));
	objects.AttachInstance(listener.Value());
	const auto function = IdleFunction(vm);
	for (std::uint32_t index = 0; index < 6; ++index) {
		auto argument = vm.CreateUserData({});
		arguments.push_back(vm.BorrowObjectIdentity(argument));
		const std::array<SquirrelValue, 1> parameters{argument};
		const auto instance = bindings.Spawn(function, parameters);
		const auto handle = bindings.ThreadHandle(instance);
		handles.push_back(handle);
		owners.push_back(vm.Retain(instance));
		objects.Subscribe(instance, listener.Value(), [&, index, handle](const SquirrelValue&) {
			Require(scheduler.Info(handle).Status == ScriptThreadStatus::LoadingFunction, "driver notification changed native status before cleanup");
			Require(vm.Length(scheduler.NativeThreadList()) == 6, "driver removed individual registration before completing all rows");
			Require(vm.ResolveObjectIdentity(arguments[index]).has_value(), "argument owner retired before notification");
			order.push_back(index);

			return false;
		});
	}

	const auto list = vm.Retain(scheduler.NativeThreadList());
	scheduler.ShutdownNativeFunctionThreads();
	Require(order == std::vector<std::uint32_t>{0, 1, 2, 3, 4, 5}, "driver native-array visit order differs from registration order");
	Require(vm.Length(list.Value()) == 0 && std::holds_alternative<std::monostate>(scheduler.NativeThreadList()), "driver did not clear and release its native array owner");
	for (const auto handle : handles) {
		Require(scheduler.Info(handle).Status == ScriptThreadStatus::None, "native thread did not enter None after argument cleanup");
	}

	static_cast<void>(vm.CollectGarbage());
	for (const auto& identity : arguments) {
		Require(!vm.ResolveObjectIdentity(identity), "function shutdown retained argument-array payload");
	}

	Require(scheduler.ActiveThreads().empty(), "retired native array still supplies active threads");
	scheduler.ShutdownNativeFunctionThreads();
	RequireFailure([&] {
		static_cast<void>(scheduler.Fork(function, std::array<SquirrelValue, 1>{vm.RootTable()}));
	},
	    "driver shutdown");
	RequireFailure([&] {
		static_cast<void>(scheduler.Advance(1));
	},
	    "scheduler");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestLiveIndexMutationAndGuards() {
	SquirrelVm vm(ReclaimingLimits());
	ScriptObjectBindings objects(vm);
	ScriptScheduler scheduler(vm);
	ScriptThreadBindings bindings(vm, scheduler, [](std::string_view) {
		return ScriptScheduler::LoadPoll{};
	},
	    objects);
	std::array<sgre::script::SquirrelHostRoot, 3> rows;
	std::array<ScriptThreadHandle, 3> handles;
	const auto listener = vm.Retain(vm.CreateInstance(objects.ObjectClass()));
	objects.AttachInstance(listener.Value());
	const auto function = IdleFunction(vm);
	for (std::size_t index = 0; index < rows.size(); ++index) {
		rows[index] = vm.Retain(bindings.Spawn(function));
		handles[index] = bindings.ThreadHandle(rows[index].Value());
	}

	const auto list = vm.Retain(scheduler.NativeThreadList());
	std::vector<std::size_t> order;
	for (std::size_t index = 0; index < rows.size(); ++index) {
		objects.Subscribe(rows[index].Value(), listener.Value(), [&, index](const SquirrelValue&) {
			order.push_back(index);
			if (index == 0) {
				RequireFailure([&] {
					scheduler.ShutdownNativeFunctionThreads();
				},
				    "idle serialized");
				RequireFailure([&] {
					static_cast<void>(bindings.Spawn(function));
				},
				    "driver shutdown");
				RequireFailure([&] {
					bindings.ReleaseInstance(rows[0].Value());
				},
				    "completing");
				static_cast<void>(vm.RemoveArray(list.Value(), 0));
			}

			return false;
		});
	}

	scheduler.ShutdownNativeFunctionThreads();
	Require(order == std::vector<std::size_t>{0, 2}, "driver copied all elements instead of reading each live initial-count index");
	Require(scheduler.Info(handles[0]).Status == ScriptThreadStatus::None && scheduler.Info(handles[1]).Status == ScriptThreadStatus::LoadingFunction && scheduler.Info(handles[2]).Status == ScriptThreadStatus::None, "live-index mutation changed unvisited row state");
	Require(vm.Length(list.Value()) == 0, "driver failed to clear remaining native references after mutation");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestPendingAndReadyFileRejection() {
	for (const bool ready : {false, true}) {
		SquirrelVm vm(ReclaimingLimits());
		ScriptObjectBindings objects(vm);
		ScriptScheduler scheduler(vm);
		const auto function = IdleFunction(vm);
		ScriptThreadBindings bindings(vm, scheduler, [&](std::string_view) {
			return [&, ready]() -> std::optional<SquirrelValue> {
				return ready ? std::optional<SquirrelValue>{function} : std::nullopt;
			};
		},
		    objects);
		const auto first = vm.Retain(bindings.Spawn(function));
		const auto first_handle = bindings.ThreadHandle(first.Value());
		scheduler.SetExecutionStopped(first_handle, true);
		const auto file = vm.Retain(bindings.Spawn(std::string("script/unsupported-buffer.nut")));
		const auto file_handle = bindings.ThreadHandle(file.Value());
		scheduler.SetExecutionStopped(file_handle, true);
		if (ready) {
			static_cast<void>(scheduler.Advance(1));
		}

		const auto before = scheduler.Info(first_handle).Status;
		RequireFailure([&] {
			scheduler.ShutdownNativeFunctionThreads();
		},
		    "ScriptBuffer");
		Require(scheduler.Info(first_handle).Status == before && vm.Length(scheduler.NativeThreadList()) == 2, "unsupported file owner was discovered after mutating an earlier row");
		vm.Close();
		vm.RethrowReleaseFailure();
	}
}

static void TestSuspendedContextSurvives() {
	SquirrelVm vm(ReclaimingLimits());
	ScriptObjectBindings objects(vm);
	ScriptScheduler scheduler(vm);
	ScriptThreadBindings bindings(vm, scheduler, [](std::string_view) {
		return ScriptScheduler::LoadPoll{};
	},
	    objects);
	std::optional<sgre::script::SquirrelContextHandle> context;
	sgre::script::SquirrelHostRoot notification_weak;
	vm.RegisterNative("wait_fixture", [&](SquirrelVm& active, std::span<const SquirrelValue>) {
		context = active.CurrentContext();
		const auto notification = active.CreateString("wake later");
		notification_weak = active.Retain(active.CreateWeakReference(notification));
		const std::array<SquirrelValue, 1> waits{notification};
		scheduler.WaitForNotificationValues(*scheduler.CurrentThread(), waits, std::int32_t{19});
		sgre::script::SquirrelNativeResult result;
		result.Suspend = true;

		return result;
	});
	const auto instance = vm.Retain(bindings.Spawn(NativeCaller(vm, "wait_fixture")));
	const auto handle = bindings.ThreadHandle(instance.Value());
	static_cast<void>(scheduler.Advance(1));
	Require(context && vm.ContextState(*context) == sgre::script::SquirrelContextState::Suspended, "fixture did not suspend its genuine child context");
	Require(!std::holds_alternative<std::monostate>(vm.ResolveWeakReference(notification_weak.Value())), "native wait does not retain the notification value");
	scheduler.ShutdownNativeFunctionThreads();
	Require(scheduler.Info(handle).Status == ScriptThreadStatus::None && scheduler.Info(handle).TimeoutTicks == -1, "driver left native wait/status fields live");
	Require(vm.ContextState(*context) == sgre::script::SquirrelContextState::Suspended, "driver phase released the child context before VM Close");
	static_cast<void>(vm.CollectGarbage());
	Require(std::holds_alternative<std::monostate>(vm.ResolveWeakReference(notification_weak.Value())), "driver wait cleanup retained notification values");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestDiagnosticRetentionAndMissingNotification() {
	SquirrelVm vm(ReclaimingLimits());
	ScriptObjectBindings objects(vm);
	ScriptScheduler scheduler(vm);
	ScriptThreadBindings bindings(vm, scheduler, [](std::string_view) {
		return ScriptScheduler::LoadPoll{};
	},
	    objects);
	const auto instance = vm.Retain(bindings.Spawn(IdleFunction(vm)));
	const auto handle = bindings.ThreadHandle(instance.Value());
	const auto listener = vm.Retain(vm.CreateInstance(objects.ObjectClass()));
	objects.AttachInstance(listener.Value());
	bool fail = true;
	objects.Subscribe(instance.Value(), listener.Value(), [&](const SquirrelValue&) {
		if (fail) {
			throw std::runtime_error("notification diagnostic");
		}

		return false;
	});
	RequireFailure([&] {
		scheduler.ShutdownNativeFunctionThreads();
	},
	    "notification diagnostic");
	Require(scheduler.Info(handle).Status == ScriptThreadStatus::LoadingFunction && vm.Length(scheduler.NativeThreadList()) == 1, "failed notification discarded the current owner or poisoned cleanup guard");
	fail = false;
	scheduler.ShutdownNativeFunctionThreads();
	vm.Close();
	vm.RethrowReleaseFailure();

	SquirrelVm generic_vm;
	ScriptScheduler generic(generic_vm);
	const auto generic_handle = generic.Fork(IdleFunction(generic_vm), std::array<SquirrelValue, 1>{generic_vm.RootTable()});
	generic.AttachNativeInstance(generic_handle, generic_vm.CreateInstance(generic_vm.CreateClass()));
	RequireFailure([&] {
		generic.ShutdownNativeFunctionThreads();
	},
	    "actual Object notification");
}

int main() {
	try {
		TestOrderedArgumentRetirement();
		TestLiveIndexMutationAndGuards();
		TestPendingAndReadyFileRejection();
		TestSuspendedContextSurvives();
		TestDiagnosticRetentionAndMissingNotification();
		std::cout << "native function-thread driver cleanup groups passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
