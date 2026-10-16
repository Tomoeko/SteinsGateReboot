#include "sgre/io/file_read_serial.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

using sgre::core::BackgroundCallLane;
using sgre::core::BackgroundCallManager;
using sgre::core::BackgroundCallTask;
using sgre::io::FileReadSerialAttachment;
using sgre::scene::CommonProcessTasks;
using sgre::scene::SerialTask;
using sgre::scene::SerialTaskCallbacks;
using sgre::scene::SerialTaskSet;
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

	throw std::runtime_error("independent read-serial fixture expected failure");
}

static void TestActualSerialPublicationAndPhaseGate() {
	BackgroundCallManager background;
	CommonProcessTasks processor;
	std::atomic<std::int32_t> reads = 0;
	std::int32_t phases = 0;
	FileReadSerialAttachment attachment(processor, background, [&]() {
		++reads;
	});
	sgre::scene::SerialTaskHandle handle;
	const auto phase = [&]() {
		++phases;
		if (phases == 1) {
			handle = attachment.AppendReadLoop();
		}
	};
	processor.Update(phase);
	Require(phases == 1 && processor.Serial().Count() == 1 && reads == 0, "actual onProcess must append/enqueue the background child into the genuine same-frame serial traversal");
	RequireFailure([&]() {
		static_cast<void>(attachment.AppendReadLoop());
	});
	processor.Update(phase);
	Require(phases == 1 && processor.Serial().Contains(handle) && processor.Serial().State(handle).Status == TaskStatus::Active, "linked background work must suppress later processor phases rather than fabricate a completed frame");
	std::promise<void> barrier;
	auto drained = barrier.get_future();
	BackgroundCallTask after_read(background, BackgroundCallLane::BelowNormal, [&]() {
		barrier.set_value();
	});
	after_read.Update();
	background.PostLoop();
	drained.get();
	processor.Update(phase);
	Require(phases == 1 && reads == 1 && !processor.Serial().Contains(handle) && processor.Serial().Count() == 0, "actual worker status2 must retire the linked serial child while keeping onProcess suppressed for that visit");
	processor.Update(phase);
	Require(phases == 2, "native processor finish/pending logic may resume only on a later empty-serial visit");
	after_read.Close();
	attachment.Close();
	background.Close();
}

static void TestQueuedCancelAndRetirementBeforeDependentOwnerDrop() {
	BackgroundCallManager background;
	CommonProcessTasks processor;
	std::atomic<std::int32_t> reads = 0;
	auto owner = std::make_shared<std::int32_t>(53);
	std::weak_ptr<std::int32_t> weak = owner;
	FileReadSerialAttachment attachment(processor, background, [owner, &reads]() {
		Require(*owner == 53, "actual read owner must enclose its queued callback");
		++reads;
	});
	owner.reset();
	static_cast<void>(attachment.AppendReadLoop());
	processor.Update([]() {
		throw std::runtime_error("queued serial child must suppress this phase");
	});
	attachment.RequireCanRetire();
	attachment.Close();
	Require(attachment.IsClosed() && processor.Serial().Count() == 0 && reads == 0 && weak.expired(), "forced owner retirement must unlink the actual queued worker before dropping reader resources");
	background.PostLoop();
	background.Close();
}

static void TestRunningFenceAndOwnerThreadPreflight() {
	BackgroundCallManager background;
	CommonProcessTasks processor;
	FileReadSerialAttachment* current = nullptr;
	std::promise<void> entered;
	auto started = entered.get_future();
	std::promise<void> release;
	auto released = release.get_future();
	std::promise<void> close_requested;
	auto closing = close_requested.get_future();
	std::atomic<bool> self_rejected = false;
	std::atomic<bool> finished = false;
	auto owner = std::make_shared<std::int32_t>(59);
	std::weak_ptr<std::int32_t> weak = owner;
	FileReadSerialAttachment attachment(processor, background, [owner, &current, &entered, &released, &self_rejected, &finished]() {
		Require(!current->CanClose(), "actual worker must fail its processor owner-thread preflight");
		try {
			current->Close();
		} catch (const std::runtime_error&) {
			self_rejected = true;
		}

		Require(!current->IsClosed(), "self-retirement rejection must precede any processor or serial mutation");
		entered.set_value();
		released.get();
		Require(*owner == 59, "running reader resources must survive through the actual read-loop return");
		finished = true;
	});
	current = &attachment;
	owner.reset();
	const auto handle = attachment.AppendReadLoop();
	processor.Update([]() {
		throw std::runtime_error("active read serial child must suppress this phase");
	});
	background.PostLoop();
	started.get();
	Require(self_rejected && processor.Serial().Contains(handle), "worker rejection must retain the actual receiving serial node");
	std::thread gate([&]() {
		closing.get();
		Require(!weak.expired() && !attachment.IsClosed(), "forced main-owner cleanup must retain reader state until its worker fence");
		release.set_value();
	});
	close_requested.set_value();
	attachment.Close();
	gate.join();
	Require(finished && weak.expired() && attachment.IsClosed() && processor.Serial().Count() == 0, "reader serial retirement must finish the actual callback before dropping its dependent owner");
	background.Close();
}

static void TestRejectedTaskDestructorFailsFast() {
	std::set_terminate([]() {
		std::_Exit(0);
	});
	SerialTaskCallbacks callbacks;
	callbacks.CanRelease = []() {
		return false;
	};
	SerialTask task(std::move(callbacks));
}

static void TestRejectedTaskSetDestructorFailsFast() {
	std::set_terminate([]() {
		std::_Exit(0);
	});
	SerialTaskSet tasks;
	SerialTaskCallbacks callbacks;
	callbacks.CanRelease = []() {
		return false;
	};
	static_cast<void>(tasks.Append(std::make_unique<SerialTask>(std::move(callbacks))));
}

static void TestRetireHeadRejectsBeforeUnlinkAndFailedClearDoesNotLoop() {
	SerialTaskSet tasks;
	bool permitted = false;
	std::int32_t released = 0;
	SerialTaskCallbacks callbacks;
	callbacks.Update = [](SerialTask& task) {
		task.SetStatus(TaskStatus::Retire);
	};
	callbacks.CanRelease = [&]() {
		return permitted;
	};
	callbacks.Release = [&]() {
		++released;
	};
	const auto handle = tasks.Append(std::make_unique<SerialTask>(std::move(callbacks)));
	RequireFailure([&]() {
		tasks.Update();
	});
	Require(tasks.Contains(handle) && tasks.Count() == 1 && tasks.State(handle).Status == TaskStatus::Retire && released == 0, "retired head owner rejection must occur before actual unlink or callback loss");
	permitted = true;
	tasks.Update();
	Require(tasks.Count() == 0 && released == 1, "later supported owner preflight must allow actual retired-head cleanup");
	SerialTaskSet changed;
	std::int32_t checks = 0;
	bool restored = false;
	SerialTaskCallbacks unstable;
	unstable.CanRelease = [&]() {
		return restored || ++checks == 1;
	};
	const auto unstable_handle = changed.Append(std::make_unique<SerialTask>(std::move(unstable)));
	RequireFailure([&]() {
		changed.Clear();
	});
	Require(changed.Contains(unstable_handle) && changed.Count() == 1 && checks == 2, "a changed preflight must fail in bounded work while preserving the still-linked owner");
	restored = true;
	changed.Clear();
}

static void TestSelectedWorkerFailureCompletesSerialCleanup() {
	BackgroundCallManager background;
	CommonProcessTasks processor;
	FileReadSerialAttachment attachment(processor, background, []() {
		throw std::runtime_error("independent actual read-loop service failure");
	});
	static_cast<void>(attachment.AppendReadLoop());
	processor.Update([]() {
		throw std::runtime_error("linked worker must suppress phase");
	});
	std::promise<void> barrier;
	auto drained = barrier.get_future();
	BackgroundCallTask after_read(background, BackgroundCallLane::BelowNormal, [&]() {
		barrier.set_value();
	});
	after_read.Update();
	background.PostLoop();
	drained.get();
	bool reported = false;
	try {
		processor.Update([]() {
			throw std::runtime_error("failed linked worker must suppress phase");
		});
	} catch (const std::runtime_error& error) {
		reported = std::string(error.what()) == "independent actual read-loop service failure";
	}

	Require(reported && processor.Serial().Count() == 0, "selected read-loop exception must report after actual serial/worker retirement");
	after_read.Close();
	attachment.Close();
	background.Close();
}

static void TestWholeListPreflightSafePath() {
	SerialTaskSet tasks;
	std::vector<std::int32_t> released;
	bool permitted = false;
	SerialTaskCallbacks prior_callbacks;
	prior_callbacks.Release = [&]() {
		released.push_back(1);
	};
	const auto prior = tasks.Append(std::make_unique<SerialTask>(std::move(prior_callbacks)));
	SerialTaskCallbacks future_callbacks;
	future_callbacks.CanRelease = [&]() {
		return permitted;
	};
	future_callbacks.Release = [&]() {
		released.push_back(2);
	};
	const auto future = tasks.Append(std::make_unique<SerialTask>(std::move(future_callbacks)));
	RequireFailure([&]() {
		tasks.Clear();
	});
	Require(tasks.Count() == 2 && tasks.Contains(prior) && tasks.Contains(future) && released.empty(), "future-owner preflight must preserve every prior node and cleanup callback");
	permitted = true;
	tasks.Clear();
	Require(released == std::vector<std::int32_t>{1, 2}, "safe whole-list retirement must preserve native serial cleanup order");
	bool direct_permitted = false;
	SerialTaskCallbacks direct_callbacks;
	direct_callbacks.CanRelease = [&]() {
		if (!direct_permitted) {
			throw std::runtime_error("independent direct preflight failure");
		}

		return true;
	};
	SerialTask direct(std::move(direct_callbacks));
	Require(!direct.CanClose(), "throwing direct preflight must remain a safe noexcept observation");
	RequireFailure([&]() {
		direct.Close();
	});
	Require(!direct.IsClosed(), "preflight failure must precede direct task closure");
	direct_permitted = true;
	direct.Close();
}

int main(int argc, char** argv) {
	if (argc == 2 && std::string_view(argv[1]) == "--rejected-task-destruction") {
		TestRejectedTaskDestructorFailsFast();

		return 1;
	}

	if (argc == 2 && std::string_view(argv[1]) == "--rejected-set-destruction") {
		TestRejectedTaskSetDestructorFailsFast();

		return 1;
	}

	try {
		TestActualSerialPublicationAndPhaseGate();
		TestQueuedCancelAndRetirementBeforeDependentOwnerDrop();
		TestRunningFenceAndOwnerThreadPreflight();
		TestWholeListPreflightSafePath();
		TestRetireHeadRejectsBeforeUnlinkAndFailedClearDoesNotLoop();
		TestSelectedWorkerFailureCompletesSerialCleanup();
		std::cout << "six independent actual read-serial lifetime fixture groups pass\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
