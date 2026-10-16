#include "sgre/script/squirrel_vm.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

using sgre::script::BytecodeProgram;
using sgre::script::SquirrelContextHandle;
using sgre::script::SquirrelInstruction;
using sgre::script::SquirrelNativeResult;
using sgre::script::SquirrelRunState;
using sgre::script::SquirrelValue;
using sgre::script::SquirrelValueType;
using sgre::script::SquirrelVm;

struct OwnerObservation {
	SquirrelValue Weak;
	SquirrelContextHandle Context;
	std::int32_t Released = 0;
	bool SuspendedAtRelease = false;
};

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

static BytecodeProgram Program(std::vector<SquirrelInstruction> instructions, std::size_t parameters = 1) {
	BytecodeProgram program;
	program.MainFunction.SourceName = std::string("independent-no-result-call-fixture");
	program.MainFunction.Name = std::string("main");
	program.MainFunction.Parameters = {std::string("this")};
	for (std::size_t index = 1; index < parameters; ++index) {
		program.MainFunction.Parameters.push_back(std::string("argument") + std::to_string(index));
	}

	program.MainFunction.StackSize = 5;
	program.MainFunction.Instructions = std::move(instructions);

	return program;
}

static SquirrelValue Producer(SquirrelVm& vm, OwnerObservation& observation, std::int32_t status = 1) {
	return vm.CreateNativeClosure("produce_owned_result", [&observation, status](SquirrelVm& active, std::span<const SquirrelValue>) {
		const std::array<std::uint8_t, 1> bytes{37};
		const auto owner = active.CreateUserData(bytes);
		observation.Weak = active.CreateWeakReference(owner);
		active.SetUserDataReleaseHook(owner, [&observation](SquirrelVm& releasing, const SquirrelValue&, std::span<std::uint8_t>) {
			Require(releasing.CurrentContext() == observation.Context, "discarded result must retire while its actual invocation context remains selected");
			++observation.Released;
			observation.SuspendedAtRelease = releasing.IsSuspended();
		});
		active.PushNativeValue(owner);

		return SquirrelNativeResult::StackStatus(status);
	});
}

static void Finish(SquirrelVm& vm) {
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestNativeOutputRetirement(std::int32_t status, bool child) {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	OwnerObservation observation;
	SquirrelVm vm(limits);
	observation.Context = child ? vm.CreateContext() : vm.RootContext();
	const auto root = vm.RootTableInContext(observation.Context);
	const std::array<SquirrelValue, 1> arguments{root};
	const auto result = vm.InvokeWithoutResultInContext(observation.Context, Producer(vm, observation, status), arguments);
	vm.RethrowReleaseFailure();
	Require(result.State == SquirrelRunState::Completed && vm.Type(result.Value) == SquirrelValueType::Null && result.Instructions == 0, "native no-result call must preserve completion metadata without returning an owner");
	Require(observation.Released == 1 && !observation.SuspendedAtRelease && vm.Type(vm.ResolveWeakReference(observation.Weak)) == SquirrelValueType::Null, "positive and raw-suspend generic native results must retire before no-result API return");
	Require(vm.CurrentContext() == vm.RootContext() && vm.RootTableInContext(observation.Context) == root, "discarding a child result must restore caller selection while preserving its independent root");
	Finish(vm);
}

static void TestExistingResultRouteKeepsFacadeOutput() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	OwnerObservation observation;
	SquirrelVm vm(limits);
	observation.Context = vm.RootContext();
	{
		const auto result = vm.Invoke(Producer(vm, observation), {});
		Require(vm.Type(result.Value) == SquirrelValueType::UserData, "existing result route must continue returning the actual owned value");
	}

	Require(observation.Released == 0 && vm.Type(vm.ResolveWeakReference(observation.Weak)) == SquirrelValueType::UserData, "existing facade output must remain owned after a caller discards its returned metadata");
	const auto empty = vm.CreateNativeClosure("discard_old_facade_output", [](SquirrelVm&, std::span<const SquirrelValue>) {
		return SquirrelNativeResult::NoValues();
	});
	static_cast<void>(vm.InvokeWithoutResultInContext(vm.RootContext(), empty, {}));
	vm.RethrowReleaseFailure();
	Require(observation.Released == 1, "new no-result invocation must retire the previous facade output instead of keeping a hidden saved copy");
	Finish(vm);
}

static void TestOrdinaryScriptReturnKeepsIndependentTemporary() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	OwnerObservation observation;
	SquirrelVm vm(limits);
	observation.Context = vm.RootContext();
	{
		const auto producer = Producer(vm, observation);
		const auto target = vm.CreateClosure(Program({Instruction(10, 2, 0), Instruction(6, 3, 1, 2, 1), Instruction(19, 1, 3)}, 2));
		const std::array<SquirrelValue, 2> arguments{vm.RootTable(), producer};
		const auto result = vm.InvokeWithoutResultInContext(vm.RootContext(), target, arguments);
		Require(result.State == SquirrelRunState::Completed && vm.Type(result.Value) == SquirrelValueType::Null && observation.Released == 0, "discarding ordinary script output must preserve its independent owned Execute temporary");
	}

	Require(vm.Type(vm.ResolveWeakReference(observation.Weak)) == SquirrelValueType::UserData && vm.CollectGarbage() == 0, "script temporary must remain traced after no-result invocation");
	const std::array<SquirrelValue, 1> receiver{vm.RootTable()};
	static_cast<void>(vm.InvokeWithoutResultInContext(vm.RootContext(), vm.CreateClosure(Program({Instruction(19, 255)})), receiver));
	vm.RethrowReleaseFailure();
	Require(observation.Released == 1 && vm.Type(vm.ResolveWeakReference(observation.Weak)) == SquirrelValueType::Null, "actual later Execute replacement must retire the independently retained script output");
	Finish(vm);
}

static void TestSuspendedScriptDiscardsSeparateYieldedOwner() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	OwnerObservation observation;
	SquirrelVm vm(limits);
	vm.InstallRecoveredBaseLibrary();
	observation.Context = vm.CreateContext();
	const auto producer = Producer(vm, observation);
	const auto target = vm.CreateClosure(Program({Instruction(10, 3, 0), Instruction(6, 3, 1, 3, 1), Instruction(6, 4, 2, 3, 1), Instruction(19, 1, 4)}, 3));
	const std::array<SquirrelValue, 3> arguments{vm.RootTableInContext(observation.Context), producer, vm.GetSlot(vm.RootTable(), std::string("suspend"))};
	const auto result = vm.InvokeWithoutResultInContext(observation.Context, target, arguments);
	vm.RethrowReleaseFailure();
	Require(result.State == SquirrelRunState::Suspended && vm.Type(result.Value) == SquirrelValueType::Null && vm.ContextState(observation.Context) == sgre::script::SquirrelContextState::Suspended, "no-result call must preserve a real child suspension and omit the separately yielded output");
	Require(observation.Released == 1 && observation.SuspendedAtRelease && vm.Type(vm.ResolveWeakReference(observation.Weak)) == SquirrelValueType::Null, "unretained yielded owner must retire while the child remains suspended, rather than masquerade as the called target temporary");
	const auto resumed = vm.ResumeInContext(observation.Context, std::int32_t{23});
	Require(resumed.State == SquirrelRunState::Completed && std::get<std::int32_t>(resumed.Value) == 23 && observation.Released == 1, "discarded yielded output must not erase the actual continuation or alter child wakeup");
	Finish(vm);
}

static void TestTypedFailurePreservesErrorPolicy(bool script, bool raise_error) {
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	std::int32_t notifications = 0;
	const auto handler = vm.CreateNativeClosure("observe_no_result_error", [&](SquirrelVm&, std::span<const SquirrelValue> arguments) {
		++notifications;
		Require(std::get<std::int32_t>(arguments[1]) == 91, "script error handler must receive the actual thrown value");

		return SquirrelNativeResult::NoValues();
	});
	const std::array<SquirrelValue, 2> handler_arguments{vm.RootTable(), handler};
	static_cast<void>(vm.Invoke(vm.GetSlot(vm.RootTable(), std::string("seterrorhandler")), handler_arguments));
	const auto target = script ? vm.CreateClosure(Program({Instruction(2, 1, 91), Instruction(58, 1)})) : vm.CreateNativeClosure("throw_native_no_result", [](SquirrelVm&, std::span<const SquirrelValue>) -> SquirrelNativeResult {
		throw sgre::script::SquirrelVmException(std::int32_t{91}, "native typed failure");
	});
	const std::array<SquirrelValue, 1> arguments{vm.RootTable()};
	bool failed = false;
	try {
		static_cast<void>(vm.InvokeWithoutResultInContext(vm.RootContext(), target, arguments, 1000, raise_error));
	} catch (const sgre::script::SquirrelVmException& error) {
		failed = std::get<std::int32_t>(error.ThrownValue()) == 91;
	}

	Require(failed && std::get<std::int32_t>(vm.LastError()) == 91 && notifications == (script && raise_error ? 1 : 0), "no-result call must preserve native/script failure distinction, LastError and actual raise-error notification policy");
	Finish(vm);
}

static void TestNestedChildResultRetiresBeforeContextRestoration() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	OwnerObservation observation;
	SquirrelVm vm(limits);
	observation.Context = vm.CreateContext();
	const auto producer = Producer(vm, observation);
	const auto parent = vm.CreateNativeClosure("parent_no_result_bridge", [&](SquirrelVm& active, std::span<const SquirrelValue>) {
		const std::array<SquirrelValue, 1> arguments{active.RootTableInContext(observation.Context)};
		const auto result = active.InvokeWithoutResultInContext(observation.Context, producer, arguments);
		Require(result.State == SquirrelRunState::Completed && active.Type(result.Value) == SquirrelValueType::Null && observation.Released == 1, "nested child result must retire before returning control to its still-running parent");
		Require(active.CurrentContext() == active.RootContext() && active.NativeStackSize() == 1, "nested no-result invocation must preserve the active parent's context and physical frame");

		return SquirrelNativeResult::ReturnValue(std::int32_t{77});
	});
	const std::array<SquirrelValue, 1> arguments{vm.RootTable()};
	Require(std::get<std::int32_t>(vm.Invoke(parent, arguments).Value) == 77, "nested discarded output must not erase the actual parent's return value");
	Finish(vm);
}

static void TestDiscardedGeneratorReusesArenaStorage() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	limits.MaxObjects = 32;
	SquirrelVm vm(limits);
	auto program = Program({Instruction(3, 0, 17), Instruction(19, 255)});
	program.MainFunction.IsGenerator = true;
	const auto generator = vm.CreateClosure(std::move(program));
	const std::array<SquirrelValue, 1> arguments{vm.RootTable()};
	for (std::size_t index = 0; index < 512; ++index) {
		const auto result = vm.InvokeWithoutResultInContext(vm.RootContext(), generator, arguments);
		Require(result.State == SquirrelRunState::Completed && vm.Type(result.Value) == SquirrelValueType::Null, "discarded initial Generator output must retire its saved frame while preserving the closure temporary");
	}

	Finish(vm);
}

int main() {
	try {
		TestNativeOutputRetirement(1, false);
		TestNativeOutputRetirement(-666, true);
		TestExistingResultRouteKeepsFacadeOutput();
		TestOrdinaryScriptReturnKeepsIndependentTemporary();
		TestSuspendedScriptDiscardsSeparateYieldedOwner();
		TestTypedFailurePreservesErrorPolicy(false, true);
		TestTypedFailurePreservesErrorPolicy(true, false);
		TestTypedFailurePreservesErrorPolicy(true, true);
		TestNestedChildResultRetiresBeforeContextRestoration();
		TestDiscardedGeneratorReusesArenaStorage();
		std::cout << "ten independent no-result invocation fixture groups pass\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
