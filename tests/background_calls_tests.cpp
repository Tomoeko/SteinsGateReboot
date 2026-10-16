#include "sgre/core/background_calls.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

using sgre::core::BackgroundCallLane;
using sgre::core::BackgroundCallManager;
using sgre::core::BackgroundCallTask;
using sgre::scene::TaskStatus;

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

	throw std::runtime_error("independent background fixture expected failure");
}

static void TestQueuedRetirementAndPostLoopWake() {
	BackgroundCallManager manager;
	std::atomic<std::int32_t> cancelled_calls = 0;
	std::atomic<std::int32_t> calls = 0;
	std::promise<void> executed;
	auto completed = executed.get_future();
	BackgroundCallTask cancelled(manager, BackgroundCallLane::BelowNormal, [&]() {
		++cancelled_calls;
	});
	cancelled.Update();
	cancelled.Update();
	Require(cancelled.Enqueued() && cancelled.Status() == TaskStatus::Active && cancelled_calls == 0, "Update must enqueue once without waking the initialized idle native lane");
	cancelled.Close();
	BackgroundCallTask live(manager, BackgroundCallLane::BelowNormal, [&]() {
		++calls;
		executed.set_value();
	});
	live.Update();
	Require(calls == 0 && !manager.CanClose(), "open concrete owners must keep the manager alive and wait for a real post-loop wake");
	manager.PostLoop();
	completed.get();
	live.Close();
	Require(calls == 1 && cancelled_calls == 0 && live.Status() == TaskStatus::Retire && live.IsClosed(), "post-loop must execute only the actual queued live task and publish retirement");
	RequireFailure([&]() {
		live.Update();
	});
	Require(manager.CanClose(), "closed physical jobs must release the manager's lifetime preflight");
	manager.Close();
}

static void TestFifoAndNoResubmission() {
	BackgroundCallManager manager;
	std::vector<std::int32_t> order;
	std::promise<void> drained;
	auto finished = drained.get_future();
	BackgroundCallTask first(manager, BackgroundCallLane::BelowNormal, [&]() {
		order.push_back(1);
	});
	BackgroundCallTask second(manager, BackgroundCallLane::BelowNormal, [&]() {
		order.push_back(2);
	});
	BackgroundCallTask third(manager, BackgroundCallLane::BelowNormal, [&]() {
		order.push_back(3);
		drained.set_value();
	});
	first.Update();
	second.Update();
	third.Update();
	second.Update();
	manager.PostLoop();
	finished.get();
	first.Update();
	second.Update();
	third.Update();
	first.Close();
	second.Close();
	third.Close();
	Require(order == std::vector<std::int32_t>{1, 2, 3}, "equal-lane tasks must keep actual FIFO order and completed tasks must never requeue");
	manager.Close();
}

static void TestBusyWorkerDrainsNewQueueWithoutAnotherWake() {
	BackgroundCallManager manager;
	std::promise<void> entered;
	auto started = entered.get_future();
	std::promise<void> release;
	auto released = release.get_future();
	std::promise<void> second_entered;
	auto second_started = second_entered.get_future();
	BackgroundCallTask first(manager, BackgroundCallLane::BelowNormal, [&]() {
		entered.set_value();
		released.get();
	});
	BackgroundCallTask second(manager, BackgroundCallLane::BelowNormal, [&]() {
		second_entered.set_value();
	});
	first.Update();
	manager.PostLoop();
	started.get();
	second.Update();
	release.set_value();
	second_started.get();
	first.Close();
	second.Close();
	Require(second.Status() == TaskStatus::Retire, "a running native lane must drain new FIFO work without an invented per-job post-loop gate");
	manager.Close();
}

static void TestFourLaneIsolation() {
	BackgroundCallManager manager;
	std::promise<void> entered;
	auto started = entered.get_future();
	std::promise<void> release;
	auto released = release.get_future();
	BackgroundCallTask blocked(manager, BackgroundCallLane::BelowNormal, [&]() {
		entered.set_value();
		released.get();
	});
	blocked.Update();
	manager.PostLoop();
	started.get();
	std::array<std::promise<void>, 3> signals;
	std::array<std::future<void>, 3> completions{signals[0].get_future(), signals[1].get_future(), signals[2].get_future()};
	BackgroundCallTask high(manager, BackgroundCallLane::AboveNormal, [&]() {
		signals[0].set_value();
	});
	BackgroundCallTask normal(manager, BackgroundCallLane::Normal, [&]() {
		signals[1].set_value();
	});
	BackgroundCallTask low(manager, BackgroundCallLane::Lowest, [&]() {
		signals[2].set_value();
	});
	high.Update();
	normal.Update();
	low.Update();
	manager.PostLoop();
	for (auto& completion : completions) {
		completion.get();
	}

	Require(blocked.Status() == TaskStatus::Active, "independent lanes must run while the actual below-normal callback remains held");
	release.set_value();
	blocked.Close();
	high.Close();
	normal.Close();
	low.Close();
	manager.Close();
}

static void TestRunningRetirementKeepsExecuteOwnerUntilReturn() {
	BackgroundCallManager manager;
	auto lifetime = std::make_shared<std::int32_t>(41);
	std::weak_ptr<std::int32_t> weak = lifetime;
	std::promise<void> entered;
	auto started = entered.get_future();
	std::promise<void> release;
	auto released = release.get_future();
	std::promise<void> close_requested;
	auto closing = close_requested.get_future();
	std::atomic<bool> callback_finished = false;
	std::atomic<bool> observed_owner = false;
	BackgroundCallTask task(manager, BackgroundCallLane::BelowNormal, [lifetime, &entered, &released, &callback_finished]() {
		entered.set_value();
		released.get();
		Require(*lifetime == 41, "real Execute lifetime owner must survive until its worker callback returns");
		callback_finished = true;
	});
	lifetime.reset();
	task.Update();
	manager.PostLoop();
	started.get();
	std::thread releasing([&]() {
		closing.get();
		observed_owner = !weak.expired() && !task.IsClosed();
		release.set_value();
	});
	close_requested.set_value();
	task.Close();
	releasing.join();
	Require(observed_owner && callback_finished && weak.expired() && task.IsClosed(), "running retirement must wait on the actual job lock before dropping its captured owner");
	manager.Close();
}

static void TestWorkerSelfCleanupAndFailures() {
	BackgroundCallManager manager;
	BackgroundCallTask* actual = nullptr;
	std::promise<void> inspected;
	auto complete = inspected.get_future();
	std::atomic<bool> self_rejected = false;
	std::atomic<bool> manager_rejected = false;
	BackgroundCallTask task(manager, BackgroundCallLane::BelowNormal, [&]() {
		Require(!actual->CanClose() && !manager.CanClose(), "worker self cleanup must fail its lifetime preflight");
		try {
			actual->Close();
		} catch (const std::runtime_error&) {
			self_rejected = true;
		}

		try {
			manager.Close();
		} catch (const std::runtime_error&) {
			manager_rejected = true;
		}

		Require(!actual->IsClosed(), "self-close rejection must occur before structural retirement");
		inspected.set_value();
	});
	actual = &task;
	task.Update();
	manager.PostLoop();
	complete.get();
	task.Close();
	Require(self_rejected && manager_rejected && task.Status() == TaskStatus::Retire, "caught self-cleanup rejection must leave the real callback completion intact");
	std::promise<void> failing_entered;
	auto failure_started = failing_entered.get_future();
	BackgroundCallTask selected_failure(manager, BackgroundCallLane::BelowNormal, [&]() {
		failing_entered.set_value();
		throw std::runtime_error("independent selected worker failure");
	});
	selected_failure.Update();
	manager.PostLoop();
	failure_started.get();
	bool selected_reported = false;
	try {
		selected_failure.Close();
	} catch (const std::runtime_error& error) {
		selected_reported = std::string(error.what()) == "independent selected worker failure";
	}

	Require(selected_reported && selected_failure.IsClosed() && selected_failure.Status() == TaskStatus::Retire && manager.CanClose(), "actual selected worker failure must report only after its task and Execute owners retire");
	manager.Close();
}

static void TestBoundsAndManagerLifetime() {
	RequireFailure([]() {
		BackgroundCallManager invalid(0);
	});
	BackgroundCallManager manager(1);
	RequireFailure([&]() {
		BackgroundCallTask absent(manager, BackgroundCallLane::BelowNormal, {});
	});
	RequireFailure([&]() {
		BackgroundCallTask invalid(manager, static_cast<BackgroundCallLane>(4), []() {
			throw std::runtime_error("invalid task must never execute");
		});
	});
	BackgroundCallTask pending(manager, BackgroundCallLane::BelowNormal, []() {
		throw std::runtime_error("unscheduled fixture owner must never execute");
	});
	RequireFailure([&]() {
		BackgroundCallTask full(manager, BackgroundCallLane::Normal, []() {
			throw std::runtime_error("full task must never execute");
		});
	});
	RequireFailure([&]() {
		manager.Close();
	});
	Require(!pending.IsClosed(), "manager preflight must reject before retiring an actual unqueued owner");
	pending.Close();
	manager.Close();
	RequireFailure([&]() {
		manager.PostLoop();
	});
}

static void TerminateWithFixtureExit() {
	std::_Exit(0);
}

static void TestTaskDestructorFailsFastFromItsWorker() {
	std::set_terminate(TerminateWithFixtureExit);
	BackgroundCallManager manager;
	std::unique_ptr<BackgroundCallTask> task;
	task = std::make_unique<BackgroundCallTask>(manager, BackgroundCallLane::BelowNormal, [&]() {
		task.reset();
	});
	task->Update();
	manager.PostLoop();
	std::promise<void> never;
	never.get_future().wait();
}

static void TestManagerDestructorFailsFastWithLiveOwners() {
	std::set_terminate(TerminateWithFixtureExit);
	auto manager = std::make_unique<BackgroundCallManager>();
	BackgroundCallTask task(*manager, BackgroundCallLane::BelowNormal, []() {
		throw std::runtime_error("unsubmitted fixture must never execute");
	});
	manager.reset();
}

int main(int argc, char** argv) {
	if (argc == 2 && std::string_view(argv[1]) == "--self-task-destruction") {
		TestTaskDestructorFailsFastFromItsWorker();
		return 1;
	}
	if (argc == 2 && std::string_view(argv[1]) == "--live-manager-destruction") {
		TestManagerDestructorFailsFastWithLiveOwners();
		return 1;
	}
	try {
		TestQueuedRetirementAndPostLoopWake();
		TestFifoAndNoResubmission();
		TestBusyWorkerDrainsNewQueueWithoutAnotherWake();
		TestFourLaneIsolation();
		TestRunningRetirementKeepsExecuteOwnerUntilReturn();
		TestWorkerSelfCleanupAndFailures();
		TestBoundsAndManagerLifetime();
		std::cout << "seven deterministic background call ownership fixture groups pass\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
