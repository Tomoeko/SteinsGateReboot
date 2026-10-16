#include "sgre/core/looper_frame.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

template <typename Operation>
static void RequireFailure(Operation operation, const char* fragment) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string(error.what()).find(fragment) != std::string::npos, error.what());

		return;
	}

	throw std::runtime_error("missing actual containing phase rejection");
}

static void TestActualPhases() {
	sgre::core::ThreadTaskFrame workers([] {
		return static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
	},
	    1, 2);
	sgre::core::LooperFrame* active = nullptr;
	std::size_t phase_calls = 0;
	std::size_t worker_calls = 0;
	std::size_t post_calls = 0;
	std::size_t release_calls = 0;
	std::size_t release_preflights = 0;
	bool fail_update = false;
	sgre::core::LooperTaskState task_state;
	sgre::core::ThreadTaskReceiver writer(workers, [&](auto&) {
		RequireFailure([&] {
			active->RequireIdleContainingPhase();
		},
		    "live serialized containing thread");
		++worker_calls;
	},
	    [&](auto&) {
		    RequireFailure([&] {
			    active->RequireIdleContainingPhase();
		    },
		        "cannot reenter worker or post delivery");
		    ++post_calls;
	    });
	sgre::core::LooperTaskOperations task;
	task.ReadState = [&] {
		return task_state;
	};
	task.Update = [&] {
		RequireFailure([&] {
			active->RequireIdleContainingPhase();
		},
		    "inactive iteration and wait phase");
		++phase_calls;
		if (fail_update) {
			writer.QueueUpdate();
			throw std::runtime_error("actual authored root failed after queuing its writer");
		}
	};
	task.Draw = [&](std::uint32_t argument) {
		Require(argument == 0, "actual Looper Draw changed its argument");
		RequireFailure([&] {
			active->RequireIdleContainingPhase();
		},
		    "inactive iteration and wait phase");
		++phase_calls;
	};
	task.CanRetire = [&] {
		return writer.CanClose();
	};
	task.Retire = [&] {
		task_state.Status = sgre::scene::TaskStatus::Retire;
	};
	sgre::core::LooperFrame looper(workers, std::move(task), [&](auto& actual) {
		Require(actual.InWait(), "actual wait phase did not enclose its pump");
		RequireFailure([&] {
			actual.RequireIdleContainingPhase();
		},
		    "inactive iteration and wait phase");
		++phase_calls;
	},
	    0, 2);
	active = &looper;
	const std::array<sgre::scene::ParallelTaskSet*, 5> groups{&looper.Hooks().Group(sgre::scene::LooperHookPhase::PreLoop), &looper.Hooks().Group(sgre::scene::LooperHookPhase::PostLoop), &looper.Group(sgre::core::LooperFrameGroup::VSync), &looper.Group(sgre::core::LooperFrameGroup::AfterUpdate), &looper.Group(sgre::core::LooperFrameGroup::AfterWait)};
	bool outside_iteration = false;
	for (auto* group : groups) {
		sgre::scene::ParallelTaskCallbacks clock;
		clock.Update = [&](auto) {
			RequireFailure([&] {
				looper.RequireIdleContainingPhase();
			},
			    outside_iteration ? "inactive actual group traversal and release" : "inactive iteration and wait phase");
			++phase_calls;
		};
		clock.Release = [&](auto) {
			RequireFailure([&] {
				looper.RequireIdleContainingPhase();
			},
			    "inactive actual group traversal and release");
			++release_calls;
		};
		clock.CanRelease = [&](auto) {
			++release_preflights;

			return true;
		};
		static_cast<void>(group->Append(std::move(clock)));
	}

	const auto preflights_before = release_preflights;
	looper.RequireIdleContainingPhase();
	Require(release_preflights == preflights_before, "containing phase query invoked unrelated retirement callbacks");
	Require(!looper.CanClose(), "registered actual clocks were removed to admit their containing owner");
	Require(looper.RunIteration() && phase_calls == 8, "actual eight containing phases were not all rejected");
	looper.RequireIdleContainingPhase();
	outside_iteration = true;
	for (auto* group : groups) {
		group->Update();
		looper.RequireIdleContainingPhase();
	}
	Require(phase_calls == 13, "independent actual group traversal did not enforce containment");

	writer.QueuePost();
	workers.RunFrame();
	Require(post_calls == 1, "actual Post delivery outside Looper was not observed");
	looper.RequireIdleContainingPhase();

	// This admission proves inactive execution only. Actual pool writer binding must also reject pending rows.
	outside_iteration = false;
	fail_update = true;
	RequireFailure([&] {
		static_cast<void>(looper.RunIteration());
	},
	    "failed after queuing its writer");
	looper.RequireIdleContainingPhase();
	Require(!writer.CanClose() && writer.State().UpdatePending, "pending writer was fabricated quiescent by containing phase admission");
	workers.RunFrame();
	Require(worker_calls == 1 && writer.CanClose(), "real worker fence failed to finish the pending authored callback");
	looper.RequireIdleContainingPhase();

	std::exception_ptr foreign_failure;
	std::thread foreign([&] {
		try {
			looper.RequireIdleContainingPhase();
		} catch (...) {
			foreign_failure = std::current_exception();
		}
	});
	foreign.join();
	Require(foreign_failure != nullptr, "foreign thread obtained containing admission");
	RequireFailure([&] {
		std::rethrow_exception(foreign_failure);
	},
	    "live serialized containing thread");

	for (auto* group : groups) {
		group->Clear();
	}
	Require(release_calls == 5, "actual release phase did not enforce all five containing traversals");
	looper.ReleaseTask();
	looper.RequireIdleContainingPhase();
	looper.Close();
	RequireFailure([&] {
		looper.RequireIdleContainingPhase();
	},
	    "live serialized containing thread");
	writer.Close();
	workers.Close();
}

int main() {
	try {
		TestActualPhases();
		std::cout << "PASS real Looper phase/list/worker ownership; registered clocks retained, pending writer remains independently nonquiescent; no native or GPU execution\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
