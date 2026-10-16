#include "sgre/script/squirrel_vm.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

using sgre::script::SquirrelHostRoot;
using sgre::script::SquirrelObjectIdentity;
using sgre::script::SquirrelValue;
using sgre::script::SquirrelVm;

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static void TestFinalizerAllocationsFollowNativeRestoredList(bool cyclic, bool mark_before_release) {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelHostRoot created_root;
	SquirrelObjectIdentity created_identity;
	std::size_t calls = 0;
	SquirrelVm vm(limits);
	const auto class_value = vm.CreateClass();
	const auto class_root = vm.Retain(class_value);
	SquirrelValue instance = vm.CreateInstance(class_value);
	vm.SetInstanceReleaseHook(instance, [&](SquirrelVm& current, const SquirrelValue&) {
		const auto created = current.CreateArray();
		if (cyclic) {
			current.AppendArray(created, created);
		}

		created_identity = current.BorrowObjectIdentity(created);
		created_root = current.Retain(created);
		++calls;
	});
	SquirrelValue cycle = vm.CreateArray();
	vm.AppendArray(cycle, cycle);
	vm.AppendArray(cycle, instance);
	instance = {};
	cycle = {};
	Require(vm.CollectGarbage() == 1 && calls == 1 && vm.ResolveObjectIdentity(created_identity), "native collection must finish the original array iteration while preserving the owner-created allocation");
	vm.RethrowReleaseFailure();
	Require(vm.RetainedReferenceCount(created_root.Value()) == 1, "native finalizer AddRef must preserve one owned value outside the restored collectable list");
	if (mark_before_release) {
		Require(vm.CollectGarbage() == 0 && vm.ResolveObjectIdentity(created_identity), "a later external-root mark must reattach the owner-created allocation without finalizing it");
	}

	created_root = {};
	const auto remaining_count = vm.CollectGarbage();
	const bool orphaned_cycle = cyclic && !mark_before_release;
	Require(remaining_count == (cyclic && mark_before_release ? 1u : 0u), "finalizer allocation must match the original collector count after explicit root release");
	Require(vm.ResolveObjectIdentity(created_identity).has_value() == orphaned_cycle, "a new cycle released before its next mark must retain the native outside-list lifetime edge");
	if (const auto orphan = vm.ResolveObjectIdentity(created_identity)) {
		vm.ClearArray(*orphan);
	}

	Require(!vm.ResolveObjectIdentity(created_identity), "explicitly clearing the detached cycle must still retire its ordinary reference-counted storage");
	vm.RethrowReleaseFailure();
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestCollectionAllocationWorkIsBoundedDuringSlotReuse() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	limits.MaxObjects = 16;
	std::size_t allocations = 0;
	SquirrelVm vm(limits);
	const auto class_value = vm.CreateClass();
	const auto class_root = vm.Retain(class_value);
	SquirrelValue instance = vm.CreateInstance(class_value);
	vm.SetInstanceReleaseHook(instance, [&](SquirrelVm& current, const SquirrelValue&) {
		for (std::size_t index = 0; index <= limits.MaxObjects; ++index) {
			static_cast<void>(current.CreateArray());
			++allocations;
		}
	});
	SquirrelValue cycle = vm.CreateArray();
	vm.AppendArray(cycle, cycle);
	vm.AppendArray(cycle, instance);
	const auto identity = vm.BorrowObjectIdentity(cycle);
	instance = {};
	cycle = {};
	Require(vm.CollectGarbage() == 1 && allocations == limits.MaxObjects && !vm.ResolveObjectIdentity(identity), "collection allocation work must stay bounded even when each successful temporary reuses one arena slot");
	bool caught = false;
	try {
		vm.RethrowReleaseFailure();
	} catch (const std::runtime_error& error) {
		caught = std::string(error.what()) == "Squirrel collection allocation work exceeds limit";
	}

	Require(caught, "bounded collection allocation must retain its authored safety diagnostic after completing cleanup");
	vm.RethrowReleaseFailure();
	Require(vm.CollectGarbage() == 0, "bounded finalizer allocation failure must leave collection state reusable");
}

static sgre::script::SquirrelInstruction Instruction(std::uint8_t opcode, std::uint8_t first, std::int32_t second) {
	sgre::script::SquirrelInstruction instruction;
	instruction.Opcode = opcode;
	instruction.Argument0 = first;
	instruction.Argument1 = second;

	return instruction;
}

static void TestDeletedFutureCursorReportsExplicitSafetyBoundary() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	std::size_t releases = 0;
	SquirrelVm vm(limits);
	const auto class_value = vm.CreateClass();
	const auto class_root = vm.Retain(class_value);
	SquirrelValue owner = vm.CreateTable();
	SquirrelValue instance = vm.CreateInstance(class_value);
	const auto instance_identity = vm.BorrowObjectIdentity(instance);
	vm.SetInstanceReleaseHook(instance, [&](SquirrelVm&, const SquirrelValue&) {
		++releases;
	});
	sgre::script::BytecodeProgram program;
	program.MainFunction.SourceName = std::string("independent-collector-cursor-fixture");
	program.MainFunction.Parameters = {std::string("this"), std::string("owner"), std::string("default")};
	program.MainFunction.StackSize = 4;
	program.MainFunction.Instructions = {Instruction(48, 3, 0), Instruction(19, 1, 3)};
	sgre::script::SquirrelFunction child;
	child.SourceName = std::string("independent-collector-cursor-fixture");
	child.Name = std::string("owned_default");
	child.Parameters = {std::string("this"), std::string("default")};
	child.StackSize = 2;
	child.DefaultParameters.push_back(2);
	sgre::script::SquirrelOuterBinding outer;
	outer.Type = 0;
	outer.Source = std::int32_t{1};
	outer.Name = std::string("owner");
	child.OuterBindings.push_back(outer);
	child.Instructions = {Instruction(19, 1, 1)};
	program.MainFunction.Functions.push_back(child);
	SquirrelValue closure;
	{
		const std::array<SquirrelValue, 3> arguments{vm.RootTable(), owner, instance};
		closure = vm.Invoke(vm.CreateClosure(std::move(program)), arguments).Value;
	}

	vm.NewSlot(owner, std::string("closure"), closure);
	const auto closure_identity = vm.BorrowObjectIdentity(closure);
	owner = {};
	instance = {};
	closure = {};
	vm.ResetContext(vm.RootContext());
	Require(vm.CollectGarbage() == 1 && releases == 1 && !vm.ResolveObjectIdentity(instance_identity) && !vm.ResolveObjectIdentity(closure_identity), "a closure destructor that retires the future cursor must finish owned releases without following a dead arena generation");
	bool caught = false;
	try {
		vm.RethrowReleaseFailure();
	} catch (const std::runtime_error& error) {
		caught = std::string(error.what()) == "Squirrel collector cursor expired during destructor callbacks";
	}

	Require(caught, "a native next-pointer lifetime gap must remain an explicit authored safety diagnostic");
	Require(vm.CollectGarbage() == 0, "a reported collector cursor boundary must restore reusable collection state");
}

static std::int32_t InvokeRegisteredCollector(SquirrelVm& vm) {
	const auto callback = vm.GetSlot(vm.RootTable(), std::string("collectgarbage"));
	const std::array<SquirrelValue, 1> arguments{vm.RootTable()};
	const auto value = vm.Invoke(callback, arguments).Value;
	Require(std::holds_alternative<std::int32_t>(value), "native collectgarbage must return one integer value");

	return std::get<std::int32_t>(value);
}

static void TestRegisteredCollectorUsesActualSharedState() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	vm.InstallRecoveredBaseLibrary();
	const auto callback = vm.GetSlot(vm.RootTable(), std::string("collectgarbage"));
	const auto reflection = vm.GetSlot(callback, std::string("getinfos"));
	const std::array<SquirrelValue, 1> receiver{callback};
	const auto info = vm.Invoke(reflection, receiver).Value;
	const auto info_root = vm.Retain(info);
	Require(sgre::script::StringText(vm.GetSlot(info, std::string("name"))) == "collectgarbage" && std::get<std::int32_t>(vm.GetSlot(info, std::string("paramscheck"))) == 1, "collector must expose its actual native name and exact receiver-only signature");
	Require(InvokeRegisteredCollector(vm) == 0, "an empty real collection must return its actual zero count");

	SquirrelValue cycle = vm.CreateArray();
	vm.AppendArray(cycle, cycle);
	const auto identity = vm.BorrowObjectIdentity(cycle);
	auto host_root = vm.Retain(cycle);
	cycle = {};
	Require(InvokeRegisteredCollector(vm) == 0 && vm.ResolveObjectIdentity(identity), "registered collection must preserve a genuine external host root");
	host_root = {};
	Require(InvokeRegisteredCollector(vm) == 1 && !vm.ResolveObjectIdentity(identity), "registered collection must reclaim the actual unreachable cycle and return its native count");
	Require(InvokeRegisteredCollector(vm) == 0, "a second genuine collection must observe the reclaimed graph rather than replaying a count");
	vm.RethrowReleaseFailure();

	bool rejected = false;
	try {
		const std::array<SquirrelValue, 2> extra{vm.RootTable(), std::int32_t{1}};
		static_cast<void>(vm.Invoke(callback, extra));
	} catch (const std::runtime_error& error) {
		rejected = std::string(error.what()).find("wrong number of parameters") != std::string::npos;
	}
	Require(rejected, "native collectgarbage must reject an extra physical argument before collection");
}

static void TestScriptCollectorMarksActiveFrameAndRunsRealRelease() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	vm.InstallRecoveredBaseLibrary();
	const auto type = vm.CreateClass();
	const auto type_root = vm.Retain(type);
	std::size_t finalized = 0;
	SquirrelValue instance = vm.CreateInstance(type);
	vm.SetInstanceReleaseHook(instance, [&](SquirrelVm&, const SquirrelValue&) {
		++finalized;
	});
	SquirrelValue cycle = vm.CreateArray();
	vm.AppendArray(cycle, cycle);
	vm.AppendArray(cycle, instance);
	const auto identity = vm.BorrowObjectIdentity(instance);
	instance = {};
	cycle = {};

	SquirrelValue live_argument = vm.CreateArray();
	vm.AppendArray(live_argument, live_argument);
	const auto live_identity = vm.BorrowObjectIdentity(live_argument);
	sgre::script::BytecodeProgram program;
	program.MainFunction.SourceName = std::string("independent-collectgarbage-call");
	program.MainFunction.Parameters = {std::string("this"), std::string("live")};
	program.MainFunction.StackSize = 6;
	program.MainFunction.Literals = {std::string("collectgarbage")};
	auto prepare = Instruction(8, 2, 0);
	prepare.Argument2 = 0;
	prepare.Argument3 = 3;
	auto call = Instruction(6, 4, 2);
	call.Argument2 = 3;
	call.Argument3 = 1;
	program.MainFunction.Instructions = {prepare, call, Instruction(19, 1, 4)};
	const auto closure = vm.CreateClosure(std::move(program));
	const auto closure_root = vm.Retain(closure);
	const std::array<SquirrelValue, 2> arguments{vm.RootTable(), live_argument};
	const auto result = vm.Invoke(closure, arguments);
	Require(std::get<std::int32_t>(result.Value) == 1 && finalized == 1 && !vm.ResolveObjectIdentity(identity), "script global collector must run the real unreachable owner hook and return the collector count");
	Require(vm.ResolveObjectIdentity(live_identity).has_value(), "collector invoked by bytecode must mark actual active frame arguments");
	vm.RethrowReleaseFailure();
}

static void TestDisabledCollectorCannotReturnNominalSuccess() {
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	bool rejected = false;
	try {
		static_cast<void>(InvokeRegisteredCollector(vm));
	} catch (const std::runtime_error& error) {
		rejected = std::string(error.what()).find("collection is disabled or reentrant") != std::string::npos;
	}
	Require(rejected, "a disabled host collection policy must remain explicit instead of returning fake zero");
}

int main() {
	try {
		TestFinalizerAllocationsFollowNativeRestoredList(false, false);
		TestFinalizerAllocationsFollowNativeRestoredList(true, false);
		TestFinalizerAllocationsFollowNativeRestoredList(true, true);
		TestCollectionAllocationWorkIsBoundedDuringSlotReuse();
		TestDeletedFutureCursorReportsExplicitSafetyBoundary();
		TestRegisteredCollectorUsesActualSharedState();
		TestScriptCollectorMarksActiveFrameAndRunsRealRelease();
		TestDisabledCollectorCannotReturnNominalSuccess();
		std::cout << "Squirrel native collection allocation checks passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
