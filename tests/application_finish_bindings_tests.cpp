#include "sgre/script/application_finish_bindings.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Operation>
static void RequireRejected(Operation operation) {
	try {
		operation();
	} catch (const std::runtime_error&) {
		return;
	}

	throw std::runtime_error("missing expected application callback rejection");
}

static sgre::script::SquirrelValue Entry(sgre::script::SquirrelVm& vm) {
	return vm.GetSlot(vm.RootTable(), std::string("entryRequestFinishCallback"));
}

static void Register(sgre::script::SquirrelVm& vm, const sgre::script::SquirrelValue& value) {
	const std::array<sgre::script::SquirrelValue, 2> arguments = {vm.RootTable(), value};
	const auto result = vm.Invoke(Entry(vm), arguments);
	Require(result.State == sgre::script::SquirrelRunState::Completed && std::holds_alternative<std::monostate>(result.Value), "native registration must return zero results");
}

static void TestRegistrationAndDecisions() {
	sgre::script::SquirrelVm vm;
	sgre::script::ScriptApplicationFinishBindings application(vm);
	Require(application.Evaluate(1).Outcome == sgre::script::ApplicationFinishOutcome::ConfirmationRequired, "missing callback must require confirmation");
	std::int32_t observed_reason = 0;
	const auto callback = vm.CreateNativeClosure("finish decision", [&](sgre::script::SquirrelVm& current, auto arguments) {
		Require(arguments.size() == 2 && arguments[0] == current.RootTable(), "callback must receive the genuine root environment");
		observed_reason = std::get<std::int32_t>(arguments[1]);

		return sgre::script::SquirrelNativeResult::ReturnValue(observed_reason == 2);
	});
	Register(vm, callback);
	Require(application.Evaluate(1).Outcome == sgre::script::ApplicationFinishOutcome::RejectedByCallback && observed_reason == 1, "reason1 callback veto changed");
	Require(application.Evaluate(2).Outcome == sgre::script::ApplicationFinishOutcome::ApprovedByCallback && observed_reason == 2, "reason2 callback approval changed");
	Require(application.Evaluate(0).Outcome == sgre::script::ApplicationFinishOutcome::SoundShutdownRequired && observed_reason == 2, "other reasons require actual sound work without calling the callback");
	const std::array<sgre::script::SquirrelValue, 3> extra = {vm.RootTable(), {}, std::int32_t{9}};
	static_cast<void>(vm.Invoke(Entry(vm), extra));
	Require(application.Evaluate(1).Outcome == sgre::script::ApplicationFinishOutcome::ConfirmationRequired, "registration must preserve an explicit null and ignore extra argument slots");
	const std::array<sgre::script::SquirrelValue, 1> absent = {vm.RootTable()};
	RequireRejected([&] {
		static_cast<void>(vm.Invoke(Entry(vm), absent));
	});
	std::cout << "root registration, genuine receiver/reasons, null and deferred platform work PASS\n";
}

static void TestNativeResultCoercionAndFailure() {
	sgre::script::SquirrelVm vm;
	sgre::script::ScriptApplicationFinishBindings application(vm);
	const std::array<sgre::script::SquirrelValue, 10> values = {false, true, std::int32_t{0}, std::int32_t{256}, std::int32_t{-257}, 0.75f, -1.75f, std::numeric_limits<float>::quiet_NaN(), {}, std::string("false")};
	const std::array expected = {sgre::script::ApplicationFinishOutcome::RejectedByCallback, sgre::script::ApplicationFinishOutcome::ApprovedByCallback, sgre::script::ApplicationFinishOutcome::RejectedByCallback, sgre::script::ApplicationFinishOutcome::ApprovedByCallback, sgre::script::ApplicationFinishOutcome::ApprovedByCallback, sgre::script::ApplicationFinishOutcome::RejectedByCallback, sgre::script::ApplicationFinishOutcome::ApprovedByCallback, sgre::script::ApplicationFinishOutcome::ApprovedByCallback, sgre::script::ApplicationFinishOutcome::ApprovedAfterCallbackFailure, sgre::script::ApplicationFinishOutcome::ApprovedAfterCallbackFailure};
	for (std::size_t index = 0; index < values.size(); ++index) {
		Register(vm, vm.CreateNativeClosure("native result vector", [&, index](sgre::script::SquirrelVm&, auto) {
			return sgre::script::SquirrelNativeResult::ReturnValue(values[index]);
		}));
		Require(application.Evaluate(1).Outcome == expected[index], "actual numeric/boolean result conversion or failure default differs");
	}
	Register(vm, std::int32_t{1});
	Require(application.Evaluate(1).Outcome == sgre::script::ApplicationFinishOutcome::ApprovedAfterCallbackFailure, "non-callable registration must be accepted and fail when invoked");
	Register(vm, vm.CreateNativeClosure("failed actual callback", [](sgre::script::SquirrelVm&, auto) -> sgre::script::SquirrelNativeResult {
		throw std::runtime_error("bounded callback failure witness");
	}));
	const auto failed = application.Evaluate(2);
	Require(failed.Outcome == sgre::script::ApplicationFinishOutcome::ApprovedAfterCallbackFailure && failed.CallbackFailure && failed.CallbackFailure->find("bounded callback failure witness") != std::string::npos, "native failure default must retain useful failure diagnostics");
	std::cout << "numeric/boolean getter vectors and actual callback failure/default PASS\n";
}

static void TestStrongWeakReplacementAndRetirement() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	sgre::script::SquirrelVm vm(limits);
	auto application = std::make_unique<sgre::script::ScriptApplicationFinishBindings>(vm);
	auto callback = vm.CreateNativeClosure("retained actual callback", [](sgre::script::SquirrelVm&, auto) {
		return sgre::script::SquirrelNativeResult::ReturnValue(true);
	});
	const auto identity = vm.BorrowObjectIdentity(callback);
	const auto weak = vm.CreateWeakReference(callback);
	Register(vm, weak);
	Require(application->Evaluate(1).Outcome == sgre::script::ApplicationFinishOutcome::ApprovedByCallback, "live weak callback must resolve and invoke its genuine target");
	Register(vm, callback);
	callback = {};
	Require(vm.ResolveObjectIdentity(identity).has_value() && application->Evaluate(1).Outcome == sgre::script::ApplicationFinishOutcome::ApprovedByCallback, "registered callback must survive its last ordinary root");
	Register(vm, weak);
	Require(!vm.ResolveObjectIdentity(identity) && application->Evaluate(1).Outcome == sgre::script::ApplicationFinishOutcome::ConfirmationRequired, "weak callback replacement must retire prior strong owner and observe expired target");
	callback = vm.CreateNativeClosure("owner retirement", [](sgre::script::SquirrelVm&, auto) {
		return sgre::script::SquirrelNativeResult::ReturnValue(false);
	});
	const auto last_identity = vm.BorrowObjectIdentity(callback);
	Register(vm, callback);
	callback = {};
	application.reset();
	Require(!vm.ResolveObjectIdentity(last_identity), "binding destruction must release its last retained callback");
	RequireRejected([&] {
		Register(vm, {});
	});
	std::cout << "strong roots, weak expiry, replacement and last owner retirement PASS\n";
}

static void TestReplacementDuringCallAndClose() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	sgre::script::SquirrelVm vm(limits);
	sgre::script::ScriptApplicationFinishBindings application(vm);
	auto callback = vm.CreateNativeClosure("replacement during call", [&](sgre::script::SquirrelVm& current, auto) {
		Register(current, {});

		return sgre::script::SquirrelNativeResult::ReturnValue(false);
	});
	const auto identity = vm.BorrowObjectIdentity(callback);
	Register(vm, callback);
	callback = {};
	Require(application.Evaluate(1).Outcome == sgre::script::ApplicationFinishOutcome::RejectedByCallback && !vm.ResolveObjectIdentity(identity), "in-flight callback snapshot must survive registration replacement until return");
	Require(application.Evaluate(1).Outcome == sgre::script::ApplicationFinishOutcome::ConfirmationRequired, "replaced callback state must be observed on the next request");
	callback = vm.CreateNativeClosure("close retained root", [](sgre::script::SquirrelVm&, auto) {
		return sgre::script::SquirrelNativeResult::ReturnValue(true);
	});
	const auto closed_identity = vm.BorrowObjectIdentity(callback);
	Register(vm, callback);
	callback = {};
	vm.Close();
	Require(!vm.ResolveObjectIdentity(closed_identity), "VM.Close must invalidate an otherwise strongly retained callback");
	RequireRejected([&] {
		static_cast<void>(application.Evaluate(1));
	});
	std::cout << "in-flight replacement and forced VM.Close invalidation PASS\n";
}

static void TestSuspendedCallbackBoundary() {
	sgre::script::SquirrelVm vm;
	sgre::script::ScriptApplicationFinishBindings application(vm);
	vm.RegisterNative("pauseFinish", [](sgre::script::SquirrelVm&, auto) {
		sgre::script::SquirrelNativeResult result;
		result.Value = false;
		result.Suspend = true;

		return result;
	});

	// Suspension requires a real script frame; a top-level native call has no continuation to retain.
	sgre::script::BytecodeProgram program;
	program.MainFunction.Name = std::string("suspended finish fixture");
	program.MainFunction.SourceName = std::string("independent-application-finish-fixture.nut");
	program.MainFunction.Parameters = {std::string("this"), std::string("reason")};
	program.MainFunction.Literals = {std::string("pauseFinish")};
	program.MainFunction.StackSize = 5;
	sgre::script::SquirrelInstruction prepare;
	prepare.Opcode = 8;
	prepare.Argument0 = 2;
	prepare.Argument3 = 3;
	sgre::script::SquirrelInstruction call;
	call.Opcode = 6;
	call.Argument0 = 4;
	call.Argument1 = 2;
	call.Argument2 = 3;
	call.Argument3 = 1;
	sgre::script::SquirrelInstruction result;
	result.Opcode = 19;
	result.Argument0 = 1;
	result.Argument1 = 4;
	program.MainFunction.Instructions = {prepare, call, result};
	const auto callback = vm.CreateClosure(std::move(program));
	Register(vm, callback);
	RequireRejected([&] {
		static_cast<void>(application.Evaluate(1));
	});
	Require(!vm.IsSuspended() && vm.ContextState(vm.RootContext()) == sgre::script::SquirrelContextState::Idle, "completed result observation must reject suspension before changing its continuation state");
	const std::array<sgre::script::SquirrelValue, 2> arguments = {vm.RootTable(), std::int32_t{1}};
	Require(vm.Invoke(callback, arguments).State == sgre::script::SquirrelRunState::Suspended, "ordinary invocation must retain its genuine suspension behavior");
	RequireRejected([&] {
		static_cast<void>(application.Evaluate(1));
	});
	vm.ResetExecution();
	std::cout << "explicit suspended callback continuation boundary PASS\n";
}

static void TestAuthoredInstructionBudgetBoundary() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	sgre::script::SquirrelVm vm(limits);
	sgre::script::ScriptApplicationFinishBindings application(vm);
	sgre::script::BytecodeProgram program;
	program.MainFunction.Name = std::string("bounded finish fixture");
	program.MainFunction.SourceName = std::string("independent-finish-budget-fixture.nut");
	program.MainFunction.Parameters = {std::string("this"), std::string("reason")};
	program.MainFunction.StackSize = 4;
	sgre::script::SquirrelInstruction load;
	load.Opcode = 2;
	load.Argument0 = 2;
	load.Argument1 = 1;
	sgre::script::SquirrelInstruction move;
	move.Opcode = 10;
	move.Argument0 = 3;
	move.Argument1 = 2;
	sgre::script::SquirrelInstruction result;
	result.Opcode = 19;
	result.Argument0 = 1;
	result.Argument1 = 3;
	program.MainFunction.Instructions = {load, move, result};
	Register(vm, vm.CreateClosure(std::move(program)));
	std::size_t previous_releases = 0;
	sgre::script::SquirrelValue previous_weak;
	const auto prime = vm.CreateNativeClosure("budget prior result", [&](sgre::script::SquirrelVm& current, auto) {
		const std::array<std::uint8_t, 1> bytes = {5};
		const auto value = current.CreateUserData(bytes);
		previous_weak = current.CreateWeakReference(value);
		current.SetUserDataReleaseHook(value, [&](sgre::script::SquirrelVm&, const auto&, auto) {
			++previous_releases;
		});

		return sgre::script::SquirrelNativeResult::ReturnValue(value);
	});
	const std::array<sgre::script::SquirrelValue, 1> receiver = {vm.RootTable()};
	static_cast<void>(vm.Invoke(prime, receiver));
	RequireRejected([&] {
		static_cast<void>(application.Evaluate(1, 1));
	});
	Require(vm.ContextState(vm.RootContext()) == sgre::script::SquirrelContextState::Idle, "budget failure must unwind its script frames without approving exit");
	Require(previous_releases == 0 && !std::holds_alternative<std::monostate>(vm.ResolveWeakReference(previous_weak)), "authored budget rejection must preserve the unrelated prior result owner");
	Require(application.Evaluate(1, 3).Outcome == sgre::script::ApplicationFinishOutcome::ApprovedByCallback, "the actual bounded callback must succeed only when all three instructions execute");
	Require(previous_releases == 0 && !std::holds_alternative<std::monostate>(vm.ResolveWeakReference(previous_weak)), "successful result pop must also preserve the unrelated prior result owner");
	vm.Close();
	Require(previous_releases == 1, "the preserved prior result must retire once during actual VM.Close");
	std::cout << "authored instruction budget rejection and recovered idle context PASS\n";
}

static void TestReturnedOwnerPopAndPreviousResult() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	sgre::script::SquirrelVm vm(limits);
	sgre::script::ScriptApplicationFinishBindings application(vm);
	std::size_t previous_releases = 0;
	std::size_t callback_releases = 0;
	sgre::script::SquirrelValue previous_weak;
	sgre::script::SquirrelValue callback_weak;
	const std::array<sgre::script::SquirrelValue, 1> receiver = {vm.RootTable()};
	const auto prime = vm.CreateNativeClosure("prior context result owner", [&](sgre::script::SquirrelVm& current, auto) {
		const std::array<std::uint8_t, 1> bytes = {3};
		const auto value = current.CreateUserData(bytes);
		previous_weak = current.CreateWeakReference(value);
		current.SetUserDataReleaseHook(value, [&](sgre::script::SquirrelVm&, const auto&, auto) {
			++previous_releases;
		});

		return sgre::script::SquirrelNativeResult::ReturnValue(value);
	});
	Register(vm, vm.CreateNativeClosure("actual reference-bearing finish output", [&](sgre::script::SquirrelVm& current, auto) {
		const std::array<std::uint8_t, 1> bytes = {7};
		const auto value = current.CreateUserData(bytes);
		callback_weak = current.CreateWeakReference(value);
		current.SetUserDataReleaseHook(value, [&](sgre::script::SquirrelVm& releasing, const auto&, auto) {
			Require(releasing.CurrentContext() == releasing.RootContext(), "callback result must pop in its actual receiving context");
			++callback_releases;
		});

		return sgre::script::SquirrelNativeResult::ReturnValue(value);
	}));
	static_cast<void>(vm.Invoke(prime, receiver));
	Require(previous_releases == 0 && !std::holds_alternative<std::monostate>(vm.ResolveWeakReference(previous_weak)), "prior context output must be its last actual owner");
	Require(application.Evaluate(1).Outcome == sgre::script::ApplicationFinishOutcome::ApprovedAfterCallbackFailure, "reference-bearing non-boolean output must preserve the native getter failure default");
	Require(callback_releases == 1 && std::holds_alternative<std::monostate>(vm.ResolveWeakReference(callback_weak)), "completed callback output must retire exactly once before evaluation returns");
	Require(previous_releases == 0 && !std::holds_alternative<std::monostate>(vm.ResolveWeakReference(previous_weak)), "callback result pop must preserve the unrelated prior context output");
	const auto clear = vm.CreateNativeClosure("ordinary result replacement", [](sgre::script::SquirrelVm&, auto) {
		return sgre::script::SquirrelNativeResult::NoValues();
	});
	static_cast<void>(vm.Invoke(clear, receiver));
	Require(previous_releases == 1, "ordinary context result replacement must retire its own previous output once");
	std::cout << "actual reference-bearing callback pop and prior result ownership PASS\n";
}

static void TestScopedObserverAndActiveCaller() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	sgre::script::SquirrelVm vm(limits);
	const auto child = vm.CreateContext();
	const std::array<sgre::script::SquirrelValue, 1> child_receiver = {vm.RootTableInContext(child)};
	std::size_t releases = 0;
	const auto output = vm.CreateNativeClosure("child actual output", [&](sgre::script::SquirrelVm& current, auto) {
		const std::array<std::uint8_t, 1> bytes = {11};
		const auto value = current.CreateUserData(bytes);
		current.SetUserDataReleaseHook(value, [&](sgre::script::SquirrelVm& releasing, const auto&, auto) {
			Require(releasing.CurrentContext() == child, "child output must pop in the receiving context after call unwind");
			++releases;
		});

		return sgre::script::SquirrelNativeResult::ReturnValue(value);
	});
	const auto caller = vm.CreateNativeClosure("active caller witness", [&](sgre::script::SquirrelVm& current, auto) {
		Require(current.CurrentContext() == current.RootContext() && current.ContextState(current.RootContext()) == sgre::script::SquirrelContextState::Running, "caller must have a real active native frame");
		bool observed = false;
		const auto result = current.InvokeObserveCompletedResultInContext(child, output, child_receiver, [&](const auto& value) {
			Require(current.CurrentContext() == child && current.UserDataBytes(value)[0] == 11 && releases == 0, "scoped observer must borrow the live output in its receiving context");
			RequireRejected([&] {
				current.ResetContext(child);
			});
			RequireRejected([&] {
				current.ReleaseContext(child);
			});
			RequireRejected([&] {
				current.Close();
			});
			observed = true;
		});
		Require(observed && releases == 1 && std::holds_alternative<std::monostate>(result.Value) && result.State == sgre::script::SquirrelRunState::Completed && result.Instructions == 0, "completed observer must pop exactly its output and retain actual invocation counters");
		Require(current.CurrentContext() == current.RootContext() && current.ContextState(child) == sgre::script::SquirrelContextState::Idle, "observer must restore its active caller and leave receiving frames idle");

		return sgre::script::SquirrelNativeResult::ReturnValue(std::int32_t{77});
	});
	const std::array<sgre::script::SquirrelValue, 1> root_receiver = {vm.RootTable()};
	Require(std::get<std::int32_t>(vm.Invoke(caller, root_receiver).Value) == 77 && releases == 1, "nested observation must preserve the active caller's actual return value");
	RequireRejected([&] {
		static_cast<void>(vm.InvokeObserveCompletedResultInContext(child, output, child_receiver, [&](const auto&) {
			throw std::runtime_error("observer rejection witness");
		}));
	});
	Require(releases == 2 && vm.ContextState(child) == sgre::script::SquirrelContextState::Idle, "throwing observer must pop its completed output and release its guard scopes");
	vm.ReleaseContext(child);
	bool stale_observed = false;
	RequireRejected([&] {
		static_cast<void>(vm.InvokeObserveCompletedResultInContext(child, output, child_receiver, [&](const auto&) {
			stale_observed = true;
		}));
	});
	Require(!stale_observed && releases == 2, "stale context must reject before observing or creating an output");
	std::cout << "receiving context pop, scoped observer, active caller and stale admission PASS\n";
}

int main() {
	try {
		TestRegistrationAndDecisions();
		TestNativeResultCoercionAndFailure();
		TestStrongWeakReplacementAndRetirement();
		TestReplacementDuringCallAndClose();
		TestSuspendedCallbackBoundary();
		TestAuthoredInstructionBudgetBoundary();
		TestReturnedOwnerPopAndPreviousResult();
		TestScopedObserverAndActiveCaller();

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
