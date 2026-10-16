#include "sgre/script/original_checkpoint.hpp"

#include "sgre/script/debug_scene_entry.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

using sgre::script::SquirrelValue;
using sgre::script::SquirrelVm;

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static void Reject(const auto& operation) {
	try {
		operation();
	} catch (const std::runtime_error&) {
		return;
	}
	throw std::runtime_error("expected checkpoint rejection");
}

static sgre::script::OriginalCheckpointLocation Location() {
	sgre::script::OriginalCheckpointLocation result;
	result.Storage = "fixture.ks";
	result.Label = "*branch";
	result.SourceLine = 123;
	result.Point = 9;
	result.Record = 17;

	return result;
}

static SquirrelValue Data(SquirrelVm& vm) {
	const auto data = vm.CreateTable();
	const auto pin = vm.Retain(data);
	const auto flags = vm.CreateTable();
	vm.NewSlot(data, std::string("flags"), flags);
	vm.NewSlot(data, std::string("storage"), vm.CreateString("fixture.ks"));
	vm.NewSlot(data, std::string("target"), vm.CreateString("*branch"));
	vm.NewSlot(data, std::string("point"), std::int32_t{9});
	vm.NewSlot(data, std::string("keys"), std::int32_t{88});
	vm.NewSlot(flags, std::string("phone-state"), std::int32_t{-2147483647});
	vm.NewSlot(flags, std::string("binary\0key", 10), vm.CreateString(std::string("a\0\xff", 3)));
	vm.NewSlot(flags, std::string("float"), std::bit_cast<float>(std::uint32_t{0xffc12345}));
	vm.NewSlot(flags, std::string("negative-zero"), std::bit_cast<float>(std::uint32_t{0x80000000}));
	vm.NewSlot(flags, std::string("items"), vm.CreateArray(std::array<SquirrelValue, 4>{SquirrelValue{}, true, false, std::numeric_limits<std::int32_t>::max()}));

	return data;
}

static void TestWholeDataAndSafety() {
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	const auto data = Data(vm);
	const auto pin = vm.Retain(data);
	const auto bytes = sgre::script::EncodeOriginalCheckpoint(vm, data, Location(), "resource-identity", "authentic-parent");
	const auto copy = sgre::script::RestoreOriginalCheckpoint(vm, bytes, Location(), "resource-identity");
	const auto copy_pin = vm.Retain(copy);
	const auto flags = vm.GetSlot(copy, std::string("flags"));
	Require(std::get<std::int32_t>(vm.GetSlot(flags, std::string("phone-state"))) == -2147483647, "signed flag word changed");
	Require(sgre::script::StringText(vm.GetSlot(flags, std::string("binary\0key", 10))) == std::string("a\0\xff", 3), "binary string/key bytes changed");
	Require(std::bit_cast<std::uint32_t>(std::get<float>(vm.GetSlot(flags, std::string("float")))) == 0xffc12345, "NaN payload/sign changed");
	Require(std::bit_cast<std::uint32_t>(std::get<float>(vm.GetSlot(flags, std::string("negative-zero")))) == 0x80000000, "negative zero changed");
	Require(bytes == sgre::script::EncodeOriginalCheckpoint(vm, copy, Location(), "resource-identity", "authentic-parent"), "whole primitive tree failed canonical round trip");
	vm.SetSlot(flags, std::string("phone-state"), std::int32_t{7});
	const auto fresh = sgre::script::RestoreOriginalCheckpoint(vm, bytes, Location(), "resource-identity");
	Require(std::get<std::int32_t>(vm.GetSlot(vm.GetSlot(fresh, std::string("flags")), std::string("phone-state"))) == -2147483647, "checkpoint consumer changed the stored tree");
	for (std::size_t length = 0; length < bytes.size(); ++length) {
		SquirrelVm truncated;
		Reject([&] {
			static_cast<void>(sgre::script::RestoreOriginalCheckpoint(truncated, std::span(bytes).first(length), Location(), "resource-identity"));
		});
	}
	Reject([&] {
		static_cast<void>(sgre::script::RestoreOriginalCheckpoint(vm, bytes, Location(), "different-resource"));
	});
	auto other = Location();
	++other.Record;
	Reject([&] {
		static_cast<void>(sgre::script::RestoreOriginalCheckpoint(vm, bytes, other, "resource-identity"));
	});
	auto trailing = bytes;
	trailing.push_back(0);
	Reject([&] {
		static_cast<void>(sgre::script::RestoreOriginalCheckpoint(vm, trailing, Location(), "resource-identity"));
	});
	vm.NewSlot(data, std::string("cycle"), data);
	Reject([&] {
		static_cast<void>(sgre::script::EncodeOriginalCheckpoint(vm, data, Location(), "resource-identity", "parent"));
	});
	vm.SetSlot(data, std::string("cycle"), SquirrelValue{});
	vm.NewSlot(data, std::string("native-object"), vm.CreateClass());
	Reject([&] {
		static_cast<void>(sgre::script::EncodeOriginalCheckpoint(vm, data, Location(), "resource-identity", "parent"));
	});
}

static sgre::script::SquirrelInstruction Instruction(std::uint8_t opcode, std::uint8_t destination, std::int32_t operand = 0, std::uint8_t argument2 = 0, std::uint8_t argument3 = 0) {
	return sgre::script::SquirrelInstruction{operand, opcode, destination, argument2, argument3};
}

static SquirrelValue OriginalMethod(SquirrelVm& vm) {
	sgre::script::BytecodeProgram program;
	auto& method = program.MainFunction;
	method.Name = std::string("original checkSaveMode fixture");
	method.SourceName = std::string("authored-checkpoint-fixture");
	method.Parameters = {std::string("this")};
	method.StackSize = 4;
	method.Literals = {std::string("awaitOriginal")};
	method.Instructions = {Instruction(8, 2, 0, 0, 3), Instruction(6, 1, 2, 3, 1), Instruction(19, 1, 1)};

	return vm.CreateClosure(std::move(program));
}

static void TestRecorderOrdinaryCallsAndCancellation() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	vm.InstallRecoveredBaseLibrary();
	const auto player_class = vm.CreateClass();
	vm.NewSlot(vm.RootTable(), std::string("ScenePlayer"), player_class);
	vm.NewSlot(player_class, std::string("checkSaveMode"), OriginalMethod(vm));
	std::size_t original_calls = 0;
	std::size_t producer_calls = 0;
	bool fail_producer = false;
	vm.NewSlot(player_class, std::string("awaitOriginal"), vm.CreateNativeClosure("yielding original fixture", [&](SquirrelVm& active, auto) {
		++original_calls;
		const auto suspended = active.RequestNativeSuspension();
		active.PushNativeValue(std::int32_t{37});

		return suspended;
	}));
	vm.NewSlot(player_class, std::string("getSaveData"), vm.CreateNativeClosure("dynamic original getSaveData fixture", [&](SquirrelVm& active, auto) {
		++producer_calls;
		if (fail_producer) {
			throw std::runtime_error("fixture optional capture failure");
		}

		return sgre::script::SquirrelNativeResult::ReturnValue(Data(active));
	}));
	std::vector<std::uint8_t> captured;
	sgre::script::OriginalCheckpointRecorder recorder(vm, "resource-identity", "new-game", [&](const auto& location, auto bytes) {
		Require(location == Location(), "recorder changed dispatcher cur++ coordinates");
		captured = std::move(bytes);

		return "content-digest";
	});
	recorder.InstallAvailableHook();
	const auto derived = vm.CreateClass(player_class);
	vm.NewSlot(vm.RootTable(), std::string("MyPlayer"), derived);
	for (const auto& [key, value] : std::array<std::pair<std::string, SquirrelValue>, 4>{{{"curSceneName", vm.CreateString("fixture.ks*branch")}, {"cur", std::int32_t{18}}, {"curLine", std::int32_t{123}}, {"curPoint", std::int32_t{9}}}}) {
		vm.NewSlot(derived, key, value);
	}
	const auto player = vm.CreateInstance(derived);
	const auto pin = vm.Retain(player);
	const auto context = vm.CreateContext();
	const auto method = vm.GetSlot(player, std::string("checkSaveMode"));
	const auto suspended = vm.InvokeInContext(context, method, std::array<SquirrelValue, 1>{player});
	Require(suspended.State == sgre::script::SquirrelRunState::Suspended && original_calls == 1 && producer_calls == 0 && recorder.Captures() == 0, "recorder ran before original yielding check completed");
	static_cast<void>(vm.CollectGarbage());
	const auto completed = vm.ResumeInContext(context, std::int32_t{37});
	Require(completed.State == sgre::script::SquirrelRunState::Completed && std::get<std::int32_t>(completed.Value) == 37 && producer_calls == 1 && recorder.Captures() == 1 && !captured.empty(), "ordinary yielding check result/capture was lost");
	static_cast<void>(sgre::script::RestoreOriginalCheckpoint(vm, captured, Location(), "resource-identity"));
	fail_producer = true;
	static_cast<void>(vm.InvokeInContext(context, method, std::array<SquirrelValue, 1>{player}));
	const auto failed_capture = vm.ResumeInContext(context, std::int32_t{37});
	Require(failed_capture.State == sgre::script::SquirrelRunState::Completed && std::get<std::int32_t>(failed_capture.Value) == 37 && producer_calls == 2 && recorder.Captures() == 1 && recorder.Error().find("fixture optional capture failure") != std::string_view::npos, "optional capture failure leaked into gameplay or disappeared");
	recorder.Cancel();
	static_cast<void>(vm.InvokeInContext(context, method, std::array<SquirrelValue, 1>{player}));
	const auto cancelled = vm.ResumeInContext(context, std::int32_t{37});
	Require(cancelled.State == sgre::script::SquirrelRunState::Completed && std::get<std::int32_t>(cancelled.Value) == 37 && original_calls == 3 && producer_calls == 2 && recorder.Captures() == 1, "cancelled recorder changed ordinary gameplay calls");
	vm.ReleaseContext(context);
}

static void TestFullSceneEntryWithoutHeadlessPolicy(bool pending) {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	vm.InstallRecoveredBaseLibrary();
	const auto data = Data(vm);
	const auto pin = vm.Retain(data);
	sgre::script::ScriptDebugSceneEntry entry;
	entry.Storage = "fixture.ks";
	entry.TargetLine = 120;
	entry.CheckpointLocation = Location();
	entry.CheckpointIdentity = "resource-identity";
	entry.Checkpoint = sgre::script::EncodeOriginalCheckpoint(vm, data, Location(), "resource-identity", "original-route-parent");
	std::size_t polls = 0;
	if (pending) {
		entry.CheckpointPoll = [bytes = std::move(entry.Checkpoint), &polls]() -> std::optional<std::vector<std::uint8_t>> {
			if (++polls < 3) {
				return {};
			}

			return bytes;
		};
		entry.Checkpoint.clear();
	}
	const auto application = vm.CreateClass();
	vm.NewSlot(application, std::string("getMainScene"), OriginalMethod(vm));
	vm.NewSlot(vm.RootTable(), std::string("Application"), application);
	sgre::script::InstallOriginalDebugSceneEntry(vm, entry);
	entry.Checkpoint.clear();
	const auto derived = vm.CreateClass(application);
	vm.NewSlot(vm.RootTable(), std::string("MyApplication"), derived);
	const auto instance = vm.CreateInstance(derived);
	const auto instance_pin = vm.Retain(instance);
	const auto method = vm.GetSlot(instance, std::string("getMainScene"));
	auto result = vm.Invoke(method, std::array<SquirrelValue, 1>{instance});
	std::size_t suspensions = 0;
	while (result.State == sgre::script::SquirrelRunState::Suspended && suspensions < 4) {
		++suspensions;
		static_cast<void>(vm.CollectGarbage());
		result = vm.ResumeInContext(vm.RootContext());
	}
	Require(result.State == sgre::script::SquirrelRunState::Completed && suspensions == (pending ? 2u : 0u), "pending full checkpoint failed to wait through ordinary application coroutine calls");
	const auto first = result.Value;
	const auto first_pin = vm.Retain(first);
	const auto flags = vm.GetSlot(first, std::string("flags"));
	Require(std::get<std::int32_t>(vm.GetSlot(first, std::string("point"))) == 9 && sgre::script::StringText(vm.GetSlot(first, std::string("target"))) == "*branch", "full scene hook lost original point/label restore semantics");
	vm.SetSlot(flags, std::string("phone-state"), std::int32_t{0});
	vm.ResetContext(vm.RootContext());
	static_cast<void>(vm.CollectGarbage());
	const auto second = vm.Invoke(method, std::array<SquirrelValue, 1>{instance}).Value;
	Require(std::get<std::int32_t>(vm.GetSlot(vm.GetSlot(second, std::string("flags")), std::string("phone-state"))) == -2147483647, "ordinary full scene hook reused mutated flags");
}

int main() {
	try {
		TestWholeDataAndSafety();
		TestRecorderOrdinaryCallsAndCancellation();
		TestFullSceneEntryWithoutHeadlessPolicy(false);
		TestFullSceneEntryWithoutHeadlessPolicy(true);
		std::cout << "complete checkpoint codec, bounds, exact primitive words, yielding producer and cancellation passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
