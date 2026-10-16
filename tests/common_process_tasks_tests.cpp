#include "sgre/scene/common_process_tasks.hpp"

#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <class Operation>
static void RequireError(Operation operation, std::string_view expected) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(expected) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("expected common process safety rejection");
}

static void TestEmbeddedKeepActiveAndEntryHeadGate() {
	sgre::scene::CommonProcessTasks process(8);
	std::vector<std::int32_t> events;
	std::size_t on_process_calls = 0;
	sgre::scene::ParallelTaskCallbacks parallel;
	parallel.Update = [&](auto) {
		events.push_back(3);
	};
	static_cast<void>(process.Parallel().Append(std::move(parallel)));
	const std::function<void()> on_process = [&] {
		++on_process_calls;
		events.push_back(1);
		sgre::scene::SerialTaskCallbacks serial;
		serial.Update = [&](auto& task) {
			events.push_back(2);
			task.SetStatus(sgre::scene::TaskStatus::Retire);
		};
		serial.Release = [&] {
			events.push_back(4);
		};
		static_cast<void>(process.Serial().Append(std::make_unique<sgre::scene::SerialTask>(std::move(serial))));
	};
	process.SetDrawPaused(sgre::scene::CommonProcessGroup::Serial, true);
	process.SetDrawPaused(sgre::scene::CommonProcessGroup::Parallel, true);
	process.Update(on_process);
	Require(events == std::vector<std::int32_t>{1, 2, 4, 3}, "onProcess can create a real serial task which runs before parallel in the same update");
	Require(process.Serial().Status() == sgre::scene::TaskStatus::Active && process.Parallel().Status() == sgre::scene::TaskStatus::Active, "actual embedded groups stay active when their owned lists become empty");

	sgre::scene::SerialTaskCallbacks head;
	head.Update = [&](auto& task) {
		events.push_back(5);
		task.SetStatus(sgre::scene::TaskStatus::Retire);
	};
	static_cast<void>(process.Serial().Append(std::make_unique<sgre::scene::SerialTask>(std::move(head))));
	events.clear();
	process.Update(on_process);
	Require(events == std::vector<std::int32_t>{5, 3} && on_process_calls == 1, "a head removed during update does not retroactively cause onProcess");
	process.Update(on_process);
	Require(on_process_calls == 2, "the following empty-entry update reaches onProcess");
	process.Clear();
	process.Update([&] {
		++on_process_calls;
	});
	Require(process.Serial().Status() == sgre::scene::TaskStatus::Active && process.Parallel().Status() == sgre::scene::TaskStatus::Active, "Clear preserves native embedded keep-active status");
}

static void TestReverseDrawAndSeparatePause() {
	sgre::scene::CommonProcessTasks process(8);
	std::vector<std::int32_t> events;
	sgre::scene::ParallelTaskCallbacks parallel;
	parallel.Draw = [&](auto, std::uint32_t argument) {
		Require(argument == 0xfedcba98, "parallel common draw preserves every argument bit");
		events.push_back(1);
	};
	sgre::scene::ParallelTaskState completed;
	completed.Status = sgre::scene::TaskStatus::Complete;
	static_cast<void>(process.Parallel().Append(std::move(parallel), completed));
	sgre::scene::SerialTaskCallbacks head;
	head.Draw = [&](auto&, std::uint32_t argument) {
		Require(argument == 0xfedcba98, "serial common draw restores every argument bit after parallel callbacks");
		events.push_back(2);
	};
	static_cast<void>(process.Serial().Append(std::make_unique<sgre::scene::SerialTask>(std::move(head))));
	process.Draw(0xfedcba98);
	Require(events == std::vector<std::int32_t>{1, 2}, "common draw visits parallel before the current serial head");
	process.SetDrawPaused(sgre::scene::CommonProcessGroup::Parallel, true);
	events.clear();
	process.Draw(0xfedcba98);
	Require(events == std::vector<std::int32_t>{2}, "embedded parallel draw pause does not suppress the serial head");
	process.SetDrawPaused(sgre::scene::CommonProcessGroup::Serial, true);
	events.clear();
	process.Draw(0xfedcba98);
	Require(events.empty(), "both native group draw pauses are independently observed");
}

static void TestCompletedOwnershipAndRetirementPhases() {
	std::vector<std::int32_t> events;
	std::vector<std::unique_ptr<sgre::scene::SerialTask>> completed;
	{
		sgre::scene::CommonProcessTasks process(8, [&](auto owner) {
			completed.push_back(std::move(owner));
		});
		sgre::scene::SerialTaskCallbacks complete;
		complete.Update = [](auto& task) {
			task.SetStatus(sgre::scene::TaskStatus::Complete);
		};
		complete.Release = [&] {
			events.push_back(1);
		};
		static_cast<void>(process.Serial().Append(std::make_unique<sgre::scene::SerialTask>(std::move(complete))));
		process.Update([] {
			throw std::runtime_error("nonempty serial must not reach onProcess");
		});
		Require(completed.size() == 1 && !completed.front()->IsClosed() && events.empty(), "status Complete transfers actual serial ownership without resource retirement");
		sgre::scene::SerialTaskCallbacks serial;
		serial.Release = [&] {
			events.push_back(2);
		};
		static_cast<void>(process.Serial().Append(std::make_unique<sgre::scene::SerialTask>(std::move(serial))));
		sgre::scene::ParallelTaskCallbacks parallel;
		parallel.Release = [&](auto) {
			events.push_back(3);
		};
		static_cast<void>(process.Parallel().Append(std::move(parallel)));
		process.ClearSerial();
		process.ClearParallel();
		Require(events == std::vector<std::int32_t>{2, 3}, "actual SQLayer can retire serial before its parallel/source graph");
	}
	Require(events == std::vector<std::int32_t>{2, 3} && !completed.front()->IsClosed(), "external completed owner survives the real common process lifetime");
	completed.front()->Close();
	Require(events == std::vector<std::int32_t>{2, 3, 1}, "the actual external completed owner retires at its chosen lifetime boundary exactly once");

	sgre::scene::CommonProcessTasks generic(8);
	sgre::scene::ParallelTaskCallbacks parallel;
	parallel.Release = [&](auto) {
		events.push_back(4);
	};
	static_cast<void>(generic.Parallel().Append(std::move(parallel)));
	sgre::scene::SerialTaskCallbacks serial;
	serial.Release = [&] {
		events.push_back(5);
	};
	static_cast<void>(generic.Serial().Append(std::make_unique<sgre::scene::SerialTask>(std::move(serial))));
	generic.Clear();
	Require(events == std::vector<std::int32_t>{2, 3, 1, 4, 5}, "generic MProcess retirement keeps its native parallel-before-serial order");
}

static void TestFailureAndReentrancyPreflight() {
	sgre::scene::CommonProcessTasks process(8);
	std::size_t releases = 0;
	process.Update([&] {
		Require(!process.CanClear(), "onProcess holds the composite traversal guard before callbacks can retire dependencies");
		RequireError([&] {
			process.Clear();
		},
		    "inactive owned groups");
		RequireError([&] {
			process.Draw(0);
		},
		    "cannot reenter");
	});
	Require(process.CanClear(), "common traversal guard is restored after rejected callback reentry");
	RequireError([&] {
		process.Update({});
	},
	    "concrete onProcess");
	RequireError([&] {
		process.Update([] {
			throw std::runtime_error("actual onProcess failure");
		});
	},
	    "actual onProcess failure");
	Require(process.CanClear(), "failing onProcess restores the common guard");
	sgre::scene::ParallelTaskCallbacks parallel;
	parallel.Release = [&](auto) {
		++releases;
		throw std::runtime_error("first native graph retirement failure");
	};
	static_cast<void>(process.Parallel().Append(std::move(parallel)));
	sgre::scene::SerialTaskCallbacks serial;
	serial.Release = [&] {
		++releases;
		throw std::runtime_error("later native graph retirement failure");
	};
	static_cast<void>(process.Serial().Append(std::make_unique<sgre::scene::SerialTask>(std::move(serial))));
	RequireError([&] {
		process.Clear();
	},
	    "first native graph retirement failure");
	Require(releases == 2 && process.Serial().Count() == 0 && process.Parallel().Count() == 0, "fallible explicit retirement attempts both real owned graphs and preserves its first error");
	process.Clear();
	Require(releases == 2, "retired callback resources cannot be released twice");
}

int main() {
	try {
		TestEmbeddedKeepActiveAndEntryHeadGate();
		TestReverseDrawAndSeparatePause();
		TestCompletedOwnershipAndRetirementPhases();
		TestFailureAndReentrancyPreflight();
		std::cout << "4 common process groups passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
