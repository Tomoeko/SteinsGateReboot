#include "sgre/core/looper_frame.hpp"
#include "sgre/input/frame_sample.hpp"
#include "sgre/script/input_bindings.hpp"

#include "input_frame_native_vectors.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <class Operation>
static void RequireFailure(Operation operation, std::string_view message) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(message) != std::string_view::npos, error.what());
		return;
	}
	throw std::runtime_error("missing expected actual input phase rejection");
}

class ActualInputFrameCleanup {
public:
	ActualInputFrameCleanup(sgre::core::LooperFrame& looper, sgre::core::ThreadTaskFrame& workers) : m_Looper(looper), m_Workers(workers) {
	}
	~ActualInputFrameCleanup() {
		try {
			m_Looper.ReleaseTask();
			m_Looper.Close();
			m_Workers.Close();
		} catch (...) {
			// Explicit success cleanup reports failures; unwind preserves the original fixture assertion.
		}
	}

private:
	sgre::core::LooperFrame& m_Looper;
	sgre::core::ThreadTaskFrame& m_Workers;
};

static void TestOriginalKeyboardOnlyDeviceFrames() {
	for (std::size_t index = 0; index != kNativeKeyboardFrame.size(); ++index) {
		const auto& expected = kNativeKeyboardFrame[index];
		sgre::input::KeyboardSample keyboard;
		keyboard.Available = expected[0] != 0;
		for (std::size_t key = 0; key != keyboard.ScanKeys.size(); ++key) {
			keyboard.ScanKeys[key] = (key + index % 64) % 7 == 0 ? 0x80 : (key + index % 64) % 11 == 0 ? 1
			                                                                                           : 0;
		}
		sgre::input::ConvertedKeyboard converted;
		converted.LogicalButtons = expected[1];
		converted.ControllerButtons = expected[2];
		const auto frame = sgre::input::ResolveKeyboardOnlyFrame(keyboard, converted);
		for (std::size_t device = 0; device != frame.Devices.size(); ++device) {
			const auto& actual = frame.Devices[device];
			Require(actual.Buttons == (device == 0 ? expected[3] : 0), "actual keyboard-only button conversion differs from native at case " + std::to_string(index));
			Require(std::bit_cast<std::uint32_t>(actual.LeftTrigger) == (device == 0 ? expected[4] : 0) && std::bit_cast<std::uint32_t>(actual.RightTrigger) == (device == 0 ? expected[5] : 0), "actual logical trigger byte30/255 rounding differs");
			Require(actual.Connected == (device == 0 && expected[6] != 0), "only designated keyboard slot is connected in actual no-controller domain");
			for (std::size_t key = 0; key != 256; ++key) {
				const auto alt_return = key == 0x1c && ((keyboard.ScanKeys[0x38] | keyboard.ScanKeys[0xb8]) & 0x80) != 0;
				const auto expected_key = keyboard.Available && !alt_return ? keyboard.ScanKeys[key] : 0;
				Require(actual.PhysicalKeys[key] == expected_key, "native scan copy/AltReturn gate applies to every actual device, not only connected slot0");
			}
		}
	}
}

static void TestRealVmInstancesOwnSeparateVSyncClocks() {
	sgre::core::ThreadTaskFrame workers([] {
		return std::uint64_t{100};
	},
	    1);
	sgre::scene::ParallelTaskSet actual_driver_parallel;
	sgre::core::LooperTaskState root_state;
	sgre::core::LooperTaskOperations root;
	std::size_t root_updates = 0;
	root.ReadState = [&] {
		return root_state;
	};
	root.Update = [&] {
		actual_driver_parallel.Update();
		++root_updates;
	};
	root.Draw = [](auto) {
	};
	root.CanRetire = [] {
		return true;
	};
	root.Retire = [] {
	};
	std::uint64_t fixture_milliseconds = 0;
	sgre::core::LooperFrame looper(workers, std::move(root), [&](auto& actual) {
		static_cast<void>(actual.Clock().Advance(fixture_milliseconds += 17));
	},
	    0);
	ActualInputFrameCleanup cleanup(looper, workers);
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	sgre::script::SquirrelVm vm(limits);
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::input::KeyMappings mappings;
	auto bindings = std::make_unique<sgre::script::ScriptInputBindings>(vm, objects, mappings);
	std::size_t hub_reads = 0;
	std::size_t mouse_reads = 0;
	bool reject_reentry = false;
	sgre::input::KeyboardSample keyboard;
	keyboard.Available = true;
	keyboard.ScanKeys[0x2c] = 0x80;
	sgre::script::SquirrelValue device;
	bindings->ConfigureFrameOwners(looper, actual_driver_parallel, [&](const auto& hub, const auto& current_mappings) {
		Require(hub.DeviceCount() == 8 && root_updates == hub_reads + 1, "real root Update must precede this Hub's VSync input producer");
		if (reject_reentry) {
			RequireFailure([&] {
				bindings->ReleaseInstance(device);
			},
			    "outside actual sampling");
		}
		++hub_reads;
		return sgre::input::ResolveKeyboardOnlyFrame(keyboard, current_mappings.Convert(keyboard));
	},
	    [&] {
		    ++mouse_reads;
		    sgre::input::MouseSample sample;
		    sample.Client = {0, 0, 100, 80};
		    sample.ClientX = 20 + static_cast<std::int32_t>(mouse_reads);
		    sample.ClientY = 40;
		    sample.TargetWindowMatches = true;
		    sample.EnterTracking = true;
		    return sample;
	    });
	const std::array<sgre::script::SquirrelValue, 1> root_receiver{vm.RootTable()};
	const auto hub = vm.Invoke(bindings->InputHubClass(), root_receiver).Value;
	const auto hub_pin = vm.Retain(hub);
	const std::array<sgre::script::SquirrelValue, 2> first_arguments{hub, std::int32_t{0}};
	device = vm.Invoke(vm.GetSlot(hub, std::string("inputAt")), first_arguments).Value;
	const auto device_pin = vm.Retain(device);
	const std::array<sgre::script::SquirrelValue, 2> last_arguments{hub, std::int32_t{7}};
	const auto last = vm.Invoke(vm.GetSlot(hub, std::string("inputAt")), last_arguments).Value;
	const auto last_pin = vm.Retain(last);
	auto& vsync = looper.Group(sgre::core::LooperFrameGroup::VSync);
	Require(vsync.Count() == 3 && !vsync.CanClear(), "actual Hub and both independent mouse wrappers each own one live VSync registration");
	Require(bindings->Device(device).Buttons() == 0 && hub_reads == 0 && mouse_reads == 0, "constructors and getters must not manufacture initial OS samples");
	Require(looper.RunIteration() && hub_reads == 1 && mouse_reads == 2, "actual Looper performs the first three native input clocks after root Update");
	Require(bindings->Device(device).Buttons() == 1 && bindings->Device(device).Pressed(1) == 0 && bindings->Device(device).Connected(), "first device's genuine double-advance primes initial keyboard edge");
	Require(!bindings->Device(last).Connected() && bindings->Device(last).PhysicalKeys()[0x2c] == 0x80, "disconnected device still publishes actual physical keyboard bytes");
	Require(bindings->Mouse(device).Position().X != bindings->Mouse(last).Position().X, "each inputAt owns an independently sampled mouse, not a shared substituted snapshot");
	reject_reentry = true;
	keyboard.ScanKeys[0x2c] = 0;
	keyboard.ScanKeys[0x2d] = 0x80;
	Require(looper.RunIteration(), "second actual input frame remains active");
	Require(bindings->Device(device).Pressed(2) == 2 && bindings->Device(device).Released(1) == 1, "next actual VSync resolves keyboard conversion before real device edge processing");
	Require(actual_driver_parallel.Count() == 1, "Hub owns one real Driver Common.parallel task; Input mouse wrappers do not add tasks there");
	actual_driver_parallel.Draw(123);
	Require(hub_reads == 2 && mouse_reads == 4, "the actual Hub task Draw is RET and cannot sample or double-advance input");
	bindings->ReleaseInstance(hub);
	std::array<sgre::scene::ParallelTaskHandle, 1> pending_tasks;
	Require(actual_driver_parallel.WriteOrder(pending_tasks) == 1 && actual_driver_parallel.State(pending_tasks[0]).Status == sgre::scene::TaskStatus::Retire, "native wrapper release marks the real Hub task Retire without deleting it");
	Require(vsync.Count() == 3 && bindings->Device(device).Pressed(2) == 2, "Hub state and VSync registration remain borrowed until physical Driver retirement");
	Require(looper.RunIteration() && hub_reads == 2 && mouse_reads == 6, "next Driver traversal retires the Hub before VSync while its independent mouse wrappers continue");
	Require(vsync.Count() == 2 && actual_driver_parallel.Count() == 0, "physical Hub retirement removes its exact VSync hook and task");
	RequireFailure([&] {
		static_cast<void>(bindings->Device(device));
	},
	    "hub has expired");
	const auto second_hub = vm.Invoke(bindings->InputHubClass(), root_receiver).Value;
	const auto second_hub_pin = vm.Retain(second_hub);
	Require(actual_driver_parallel.Count() == 1 && vsync.Count() == 3, "later real Hub construction gets a distinct physical task owner");
	actual_driver_parallel.Clear();
	RequireFailure([&] {
		static_cast<void>(bindings->Hub(second_hub));
	},
	    "no live native state");
	Require(vsync.Count() == 2, "containing Driver clear retires the real Hub even while its VM wrapper is alive");
	vm.Close();
	vm.RethrowReleaseFailure();
	Require(vsync.Count() == 0 && actual_driver_parallel.Count() == 0, "VM native retirement removes independent mouse clocks and tolerates already retired Hub task");
	bindings.reset();
	looper.ReleaseTask();
	looper.Close();
	workers.Close();
}

static void TestExactDriverTaskRollbackAndAdapterRetirement() {
	sgre::core::ThreadTaskFrame workers([] {
		return std::uint64_t{100};
	},
	    1);
	sgre::core::LooperTaskOperations root;
	root.ReadState = [] {
		return sgre::core::LooperTaskState{};
	};
	root.Update = [] {
	};
	root.Draw = [](auto) {
	};
	root.CanRetire = [] {
		return true;
	};
	root.Retire = [] {
	};
	sgre::core::LooperFrame looper(workers, std::move(root), [](auto&) {
	},
	    0);
	ActualInputFrameCleanup cleanup(looper, workers);
	sgre::scene::ParallelTaskSet actual_driver_parallel(2);
	std::size_t foreign_releases = 0;
	sgre::scene::ParallelTaskCallbacks foreign;
	foreign.Release = [&](auto) {
		++foreign_releases;
	};
	const auto first_foreign = actual_driver_parallel.Append(foreign);
	const auto second_foreign = actual_driver_parallel.Append(foreign);
	sgre::script::SquirrelVm vm;
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::input::KeyMappings mappings;
	auto bindings = std::make_unique<sgre::script::ScriptInputBindings>(vm, objects, mappings);
	bindings->ConfigureFrameOwners(looper, actual_driver_parallel, [](const auto&, const auto&) {
		return sgre::input::HubFrameSample{};
	},
	    [] {
		    return sgre::input::MouseSample{};
	    });
	const std::array<sgre::script::SquirrelValue, 1> root_receiver{vm.RootTable()};
	RequireFailure([&] {
		static_cast<void>(vm.Invoke(bindings->InputHubClass(), root_receiver));
	},
	    "capacity");
	Require(looper.Group(sgre::core::LooperFrameGroup::VSync).Count() == 0 && actual_driver_parallel.Count() == 2, "failed actual Driver append rolls back only newly constructed input VSync and native wrapper");
	auto released = actual_driver_parallel.Extract(second_foreign);
	released.Release();
	const auto hub = vm.Invoke(bindings->InputHubClass(), root_receiver).Value;
	const auto pin = vm.Retain(hub);
	Require(actual_driver_parallel.Count() == 2 && foreign_releases == 1, "real Hub task joins the same existing Driver group without replacing other tasks");
	bindings.reset();
	Require(actual_driver_parallel.Count() == 1 && actual_driver_parallel.Contains(first_foreign) && foreign_releases == 1 && looper.Group(sgre::core::LooperFrameGroup::VSync).Count() == 0, "adapter retirement extracts exactly its own physical task and clock, preserving foreign actual Driver task");
	vm.Close();
	vm.RethrowReleaseFailure();
	actual_driver_parallel.Clear();
	Require(foreign_releases == 2, "foreign task retires only when its actual containing owner clears it");
	looper.ReleaseTask();
	looper.Close();
	workers.Close();
}

int main() {
	try {
		TestOriginalKeyboardOnlyDeviceFrames();
		TestRealVmInstancesOwnSeparateVSyncClocks();
		TestExactDriverTaskRollbackAndAdapterRetirement();
		std::cout << "Original1024 keyboard-device vectors and genuine Hub/mouse VM VSync ownership passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
