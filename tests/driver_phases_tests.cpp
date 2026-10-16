#include "sgre/script/driver_phases.hpp"

#include "sgre/scene/common_process_tasks.hpp"
#include "sgre/script/thread_bindings.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

using sgre::script::ScriptDriverPhase;
using sgre::script::ScriptDriverPhases;
using sgre::script::ScriptDriverPhaseServices;
using sgre::script::ScriptScheduler;
using sgre::script::SquirrelNativeResult;
using sgre::script::SquirrelValue;
using sgre::script::SquirrelValueType;
using sgre::script::SquirrelVm;

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

template <typename Operation>
static void RequireFailure(Operation operation) {
	try {
		operation();
	} catch (const std::runtime_error&) {
		return;
	}

	throw std::runtime_error("independent driver fixture expected a failure");
}

static void ReachSteady(ScriptDriverPhases& phases) {
	phases.OnProcess();
	phases.OnProcess();
	phases.OnProcess();
	Require(phases.Phase() == ScriptDriverPhase::Steady, "three separate onProcess visits must reach the native steady phase");
}

static void TestNativePhaseGateAndSameFramePublication() {
	SquirrelVm vm;
	ScriptScheduler scheduler(vm);
	sgre::scene::CommonProcessTasks common;
	std::int32_t setup_count = 0;
	std::int32_t parallel_updates = 0;
	std::int32_t reads = 0;
	float cached_step = 3.75f;
	ScriptDriverPhaseServices services;
	services.Setup = [&]() {
		++setup_count;
		sgre::scene::ParallelTaskCallbacks observed_task;
		observed_task.Update = [&](sgre::scene::ParallelTaskHandle) {
			++parallel_updates;
		};
		static_cast<void>(common.Parallel().Append(std::move(observed_task)));
	};
	services.ReadFrameStep = [&]() {
		++reads;

		return cached_step;
	};
	ScriptDriverPhases phases(vm, scheduler, "", {}, std::move(services));
	sgre::scene::SerialTaskCallbacks serial;
	serial.Update = [](sgre::scene::SerialTask& current) {
		current.SetStatus(sgre::scene::TaskStatus::Retire);
	};
	static_cast<void>(common.Serial().Append(std::make_unique<sgre::scene::SerialTask>(std::move(serial))));
	const auto process = [&]() {
		phases.OnProcess();
	};
	common.Update(process);
	Require(phases.Phase() == ScriptDriverPhase::ArmSetup && setup_count == 0, "serial work must suppress the actual driver phase until the following common update");
	common.Update(process);
	Require(phases.Phase() == ScriptDriverPhase::Setup && setup_count == 0, "arming setup must not execute setup in the same phase visit");
	common.Update(process);
	Require(phases.Phase() == ScriptDriverPhase::Startup && setup_count == 1 && parallel_updates == 1, "setup publication must run in the genuine same-frame embedded parallel traversal");
	common.Update(process);
	Require(phases.Phase() == ScriptDriverPhase::Steady && !phases.StartupSucceeded() && reads == 0 && scheduler.CurrentTick() == 0, "empty startup must skip Thread and wait for the next steady phase to advance the clock");
	common.Update(process);
	Require(reads == 1 && scheduler.CurrentTick() == 3 && scheduler.DiffTick() == 3, "steady driver must truncate the already cached float exactly once before real scheduler dispatch");
	cached_step = 0.75f;
	common.Update(process);
	Require(reads == 2 && scheduler.CurrentTick() == 3 && scheduler.DiffTick() == 0, "native conversion must truncate a supported fractional cached step toward zero");
	common.Clear();
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestDynamicRootThreadAndReturnedLifetime() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	ScriptScheduler scheduler(vm);
	std::int32_t calls = 0;
	std::int32_t retired = 0;
	SquirrelValue weak;
	const auto root = vm.RootTable();
	vm.NewSlot(root, std::string("Thread"), vm.CreateNativeClosure("initial_thread_slot", [](SquirrelVm&, std::span<const SquirrelValue>) -> SquirrelNativeResult {
		throw std::runtime_error("initial Thread slot must be replaced before startup lookup");
	}));
	const auto replacement = vm.CreateNativeClosure("actual_replacement_thread", [&](SquirrelVm& active, std::span<const SquirrelValue> parameters) {
		++calls;
		Require(parameters.size() == 5 && parameters[0] == root && active.Type(parameters[1]) == SquirrelValueType::Null, "startup must call root.Thread with actual root receiver and null environment");
		Require(sgre::script::StringText(parameters[2]) == "boot.nut" && sgre::script::StringText(parameters[3]) == "first" && sgre::script::StringText(parameters[4]).empty(), "startup filename/argv must use native NUL-terminated prefixes without normalization");
		const std::array<std::uint8_t, 1> bytes{31};
		const auto output = active.CreateUserData(bytes);
		weak = active.CreateWeakReference(output);
		active.SetUserDataReleaseHook(output, [&](SquirrelVm& releasing, const SquirrelValue&, std::span<std::uint8_t>) {
			Require(releasing.ContextState(releasing.RootContext()) == sgre::script::SquirrelContextState::Idle, "startup temporary output must pop after the root call has unwound");
			++retired;
		});
		active.PushNativeValue(output);

		return SquirrelNativeResult::StackStatus(1);
	});
	ScriptDriverPhaseServices services;
	services.Setup = [&]() {
		vm.SetSlot(root, std::string("Thread"), replacement);
	};
	services.ReadFrameStep = []() {
		return 1.0f;
	};
	const std::array<std::string_view, 2> arguments{std::string_view("first\0tail", 10), ""};
	ScriptDriverPhases phases(vm, scheduler, std::string_view("boot.nut\0tail", 13), arguments, std::move(services));
	ReachSteady(phases);
	vm.RethrowReleaseFailure();
	Require(calls == 1 && retired == 1 && phases.StartupSucceeded() == true && vm.Type(vm.ResolveWeakReference(weak)) == SquirrelValueType::Null, "dynamic startup must use the replacement Thread and leave no extra facade root for its returned owner");
	phases.OnProcess();
	Require(calls == 1 && scheduler.CurrentTick() == 1, "successful startup must not recur during actual steady scheduling");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestDelegatedRootThreadLookup() {
	SquirrelVm vm;
	ScriptScheduler scheduler(vm);
	const auto root = vm.RootTable();
	std::int32_t lookups = 0;
	std::int32_t calls = 0;
	const auto thread = vm.CreateNativeClosure("delegated_startup_thread", [&](SquirrelVm&, std::span<const SquirrelValue> arguments) {
		Require(arguments.size() == 3 && arguments[0] == root && sgre::script::StringText(arguments[2]) == "delegated.nut", "delegated Thread must receive the actual root startup arguments");
		++calls;

		return SquirrelNativeResult::StackStatus(0);
	});
	const auto delegate = vm.CreateTable();
	vm.NewSlot(delegate, std::string("_get"), vm.CreateNativeClosure("root_thread_get", [&](SquirrelVm& active, std::span<const SquirrelValue> arguments) {
		Require(arguments.size() == 2 && arguments[0] == root && sgre::script::StringText(arguments[1]) == "Thread", "startup must use genuine general Get with the original root receiver");
		++lookups;
		active.PushNativeValue(thread);

		return SquirrelNativeResult::StackStatus(1);
	}));
	sgre::script::BytecodeProgram delegation;
	delegation.MainFunction.SourceName = std::string("independent-driver-delegation.nut");
	delegation.MainFunction.Name = std::string("set_delegate");
	delegation.MainFunction.Parameters = {std::string("this"), std::string("target"), std::string("parent")};
	delegation.MainFunction.StackSize = 4;
	sgre::script::SquirrelInstruction attach;
	attach.Opcode = 53;
	attach.Argument0 = 3;
	attach.Argument1 = 1;
	attach.Argument2 = 2;
	delegation.MainFunction.Instructions.push_back(attach);
	sgre::script::SquirrelInstruction finish;
	finish.Opcode = 19;
	finish.Argument0 = 255;
	delegation.MainFunction.Instructions.push_back(finish);
	const std::array<SquirrelValue, 3> delegate_arguments{root, root, delegate};
	static_cast<void>(vm.InvokeWithoutResultInContext(vm.RootContext(), vm.CreateClosure(std::move(delegation)), delegate_arguments));
	ScriptDriverPhaseServices services;
	services.Setup = [&]() {
		Require(lookups == 0, "setup must not perform the later startup lookup");
	};
	services.ReadFrameStep = []() {
		return 1.0f;
	};
	ScriptDriverPhases phases(vm, scheduler, "delegated.nut", {}, std::move(services));
	ReachSteady(phases);
	Require(lookups == 1 && calls == 1 && phases.StartupSucceeded() == true, "startup must execute delegated root.Thread exactly once");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestActualThreadRegistrationAndScheduler() {
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	ScriptScheduler scheduler(vm);
	sgre::script::BytecodeProgram program;
	program.MainFunction.SourceName = std::string("independent-driver-startup.nut");
	program.MainFunction.Name = std::string("fixture");
	program.MainFunction.Parameters = {std::string("this")};
	program.MainFunction.StackSize = 1;
	sgre::script::SquirrelInstruction finish;
	finish.Opcode = 19;
	finish.Argument0 = 255;
	program.MainFunction.Instructions.push_back(finish);
	const auto callable = vm.CreateClosure(std::move(program));
	std::int32_t requests = 0;
	std::int32_t polls = 0;
	sgre::script::ScriptThreadBindings threads(vm, scheduler, [&](std::string_view filename) {
		Require(filename == "boot.nut", "genuine Thread constructor must receive the original startup filename");
		++requests;

		return [&]() -> std::optional<SquirrelValue> {
			++polls;

			return callable;
		};
	});
	std::int32_t setups = 0;
	ScriptDriverPhaseServices services;
	services.Setup = [&]() {
		++setups;
	};
	services.ReadFrameStep = []() {
		return 2.0f;
	};
	ScriptDriverPhases phases(vm, scheduler, "boot.nut", {}, std::move(services));
	ReachSteady(phases);
	Require(setups == 1 && requests == 1 && polls == 0 && scheduler.ActiveThreads().size() == 1 && vm.Length(scheduler.NativeThreadList()) == 1, "actual Thread startup must leave its scheduler-owned registration intact after temporary result pop");
	phases.OnProcess();
	Require(polls == 1 && scheduler.CurrentTick() == 2 && scheduler.ActiveThreads().empty() && vm.Length(scheduler.NativeThreadList()) == 0, "steady phase must execute and prune the actual ready registered startup thread");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestNativeStartupFailureAndAuthoredServiceFailure() {
	SquirrelVm vm;
	ScriptScheduler scheduler(vm);
	std::int32_t setup_count = 0;
	ScriptDriverPhaseServices services;
	services.Setup = [&]() {
		++setup_count;
	};
	services.ReadFrameStep = []() {
		return 1.0f;
	};
	ScriptDriverPhases missing(vm, scheduler, "missing.nut", {}, services);
	ReachSteady(missing);
	Require(missing.StartupSucceeded() == false && !missing.Failed() && vm.Type(vm.LastError()) == SquirrelValueType::String, "failed native root.Thread lookup must preserve LastError and enter steady without an invented retry");
	vm.NewSlot(vm.RootTable(), std::string("Thread"), vm.CreateNativeClosure("startup_typed_failure", [](SquirrelVm&, std::span<const SquirrelValue>) -> SquirrelNativeResult {
		throw sgre::script::SquirrelVmException(std::int32_t{71}, "independent startup call failure");
	}));
	ScriptDriverPhases failed_call(vm, scheduler, "boot.nut", {}, services);
	ReachSteady(failed_call);
	Require(failed_call.StartupSucceeded() == false && !failed_call.Failed() && std::get<std::int32_t>(vm.LastError()) == 71 && setup_count == 2, "failed native Thread call must preserve its typed error and still enter steady");
	services.Setup = []() {
		throw std::runtime_error("independent partial setup failure");
	};
	ScriptDriverPhases partial(vm, scheduler, "", {}, services);
	partial.OnProcess();
	RequireFailure([&]() {
		partial.OnProcess();
	});
	Require(partial.Failed() && partial.Phase() == ScriptDriverPhase::Setup, "partial authored setup must latch failure before any repeated publication");
	RequireFailure([&]() {
		partial.OnProcess();
	});
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestRequiredServicesBoundsAndReentry() {
	SquirrelVm vm;
	ScriptScheduler scheduler(vm);
	RequireFailure([&]() {
		ScriptDriverPhases absent(vm, scheduler, "", {}, {});
	});
	ScriptDriverPhaseServices services;
	ScriptDriverPhases* current = nullptr;
	bool reentry_rejected = false;
	services.Setup = [&]() {
		try {
			current->OnProcess();
		} catch (const std::runtime_error&) {
			reentry_rejected = true;
		}
	};
	services.ReadFrameStep = []() {
		return std::numeric_limits<float>::infinity();
	};
	sgre::script::ScriptDriverPhaseLimits limits;
	limits.MaxStartupBytes = 3;
	RequireFailure([&]() {
		ScriptDriverPhases large(vm, scheduler, "four", {}, services, limits);
	});
	const std::array<std::string_view, 1> arguments{"two"};
	RequireFailure([&]() {
		ScriptDriverPhases large(vm, scheduler, "a", arguments, services, limits);
	});
	ScriptDriverPhases phases(vm, scheduler, "", {}, services);
	current = &phases;
	ReachSteady(phases);
	Require(reentry_rejected && !phases.Failed(), "caught service reentry must leave the real phase operation intact");
	RequireFailure([&]() {
		phases.OnProcess();
	});
	Require(phases.Failed() && scheduler.CurrentTick() == 0, "unsupported CVTTSS2SI values must fail before mutating the actual scheduler tick");
	vm.Close();
	vm.RethrowReleaseFailure();
}

int main() {
	try {
		TestNativePhaseGateAndSameFramePublication();
		TestDynamicRootThreadAndReturnedLifetime();
		TestDelegatedRootThreadLookup();
		TestActualThreadRegistrationAndScheduler();
		TestNativeStartupFailureAndAuthoredServiceFailure();
		TestRequiredServicesBoundsAndReentry();
		std::cout << "six independent native driver phase fixture groups pass\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
