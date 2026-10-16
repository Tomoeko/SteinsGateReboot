#include "sgre/scene/serial_task_set.hpp"

#include <array>
#include <cstdint>
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
static void RequireError(Operation operation, std::string_view expected) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(expected) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("expected serial safety rejection");
}

static void TestHeadTraversalDrawAndSameUpdateTransfer() {
	using sgre::scene::SerialTask;
	using sgre::scene::SerialTaskCallbacks;
	using sgre::scene::TaskStatus;
	std::vector<std::int32_t> events;
	std::vector<std::unique_ptr<SerialTask>> completed;
	sgre::scene::SerialTaskSet tasks(8, [&](auto task) {
		completed.push_back(std::move(task));
	});
	std::size_t first_calls = 0;
	SerialTaskCallbacks first;
	first.Update = [&](auto& task) {
		events.push_back(10);
		if (++first_calls == 2) {
			task.SetStatus(TaskStatus::Complete);
		}
	};
	first.Draw = [&](auto&, std::uint32_t argument) {
		Require(argument == 0xfedcba98, "serial draw forwards the full original DWORD");
		events.push_back(11);
	};
	first.Release = [&] {
		events.push_back(12);
	};
	const auto first_handle = tasks.Append(std::make_unique<SerialTask>(std::move(first)));
	SerialTaskCallbacks second;
	second.Update = [&](auto& task) {
		events.push_back(20);
		task.SetStatus(TaskStatus::Retire);
	};
	second.Draw = [&](auto&, auto) {
		events.push_back(21);
	};
	second.Release = [&] {
		events.push_back(22);
	};
	static_cast<void>(tasks.Append(std::make_unique<SerialTask>(std::move(second))));

	tasks.Update();
	tasks.Draw(0xfedcba98);
	Require(events == std::vector<std::int32_t>{10, 11} && tasks.Count() == 2, "an Active head blocks later updates and is the only drawable");
	tasks.SetDrawPaused(first_handle, true);
	tasks.Draw(0);
	Require(events.size() == 2, "a paused head does not expose the next drawable");
	tasks.Update();
	Require(events == std::vector<std::int32_t>{10, 11, 10, 20, 22}, "Complete transfers without release and next Retire updates/releases in the same traversal");
	Require(tasks.Count() == 0 && tasks.Status() == TaskStatus::Retire && !tasks.Contains(first_handle), "default serial empty policy retires the group and invalidates transferred handles");
	Require(completed.size() == 1 && completed[0]->State().Status == TaskStatus::Complete && !completed[0]->IsClosed(), "the external completion receiver owns the still-live completed task");
	tasks.Clear();
	Require(events.back() == 22, "linked group clear must not dispose external completed owners");
	completed.clear();
	Require(events.back() == 12, "completed release happens only when its actual external owner retires");
}

static void TestTailAppendAndPersistentGroupStatus() {
	using sgre::scene::SerialTask;
	using sgre::scene::SerialTaskCallbacks;
	using sgre::scene::TaskStatus;
	std::vector<std::int32_t> events;
	sgre::scene::SerialTaskSet tasks(2);
	SerialTaskCallbacks original;
	original.Update = [&](auto& task) {
		events.push_back(1);
		SerialTaskCallbacks child;
		child.Update = [&](auto& appended) {
			events.push_back(2);
			appended.SetStatus(TaskStatus::Retire);
		};
		child.Release = [&] {
			events.push_back(3);
		};
		static_cast<void>(tasks.Append(std::make_unique<SerialTask>(std::move(child))));
		task.SetStatus(TaskStatus::Retire);
	};
	original.Release = [&] {
		events.push_back(4);
	};
	const auto old = tasks.Append(std::make_unique<SerialTask>(std::move(original)));
	tasks.Update();
	Require(events == std::vector<std::int32_t>{1, 4, 2, 3}, "tail-appended tasks are visible after head retirement in the same update");
	Require(tasks.Status() == TaskStatus::Retire && !tasks.Contains(old), "serial retirement is explicit persistent group state");
	const auto reused = tasks.Append(std::make_unique<SerialTask>(SerialTaskCallbacks{}));
	Require(tasks.Status() == TaskStatus::Retire && reused.Generation != old.Generation, "append does not reset retired group status and reused slots have fresh generations");
	tasks.Clear();
	Require(tasks.Status() == TaskStatus::Retire, "clear preserves the current native group status");
}

static void TestCompletedOwnerOutlivesGroup() {
	std::unique_ptr<sgre::scene::SerialTask> completed;
	std::size_t releases = 0;
	{
		sgre::scene::SerialTaskSet tasks(1, [&](auto task) {
			completed = std::move(task);
		});
		sgre::scene::SerialTaskCallbacks callbacks;
		callbacks.Release = [&] {
			++releases;
		};
		sgre::scene::ParallelTaskState state;
		state.Status = sgre::scene::TaskStatus::Complete;
		static_cast<void>(tasks.Append(std::make_unique<sgre::scene::SerialTask>(std::move(callbacks), state)));
		tasks.Update();
	}

	Require(completed && releases == 0, "group destruction does not close a transferred Complete owner");
	completed->Close();
	completed->Close();
	completed.reset();
	Require(releases == 1, "external explicit close and destruction invoke its release exactly once");
}

static void TestClearFinishesAfterFailure() {
	std::vector<std::uint32_t> released;
	sgre::scene::SerialTaskSet tasks(3);
	for (std::uint32_t index = 0; index < 3; ++index) {
		sgre::scene::SerialTaskCallbacks callbacks;
		callbacks.Release = [&, index] {
			released.push_back(index);
			if (index == 0) {
				throw std::runtime_error("independent serial release failure");
			}
		};
		static_cast<void>(tasks.Append(std::make_unique<sgre::scene::SerialTask>(std::move(callbacks))));
	}

	RequireError([&] {
		tasks.Clear();
	},
	    "independent serial release failure");
	Require(released == std::vector<std::uint32_t>{0, 1, 2} && tasks.Count() == 0, "clear finishes original list-order cleanup before reporting the first failure");
	tasks.Clear();
	Require(released.size() == 3 && tasks.Status() == sgre::scene::TaskStatus::Active, "empty clear does not repeat releases or apply the update-only empty policy");
}

static void TestSafetyAndPrependOrder() {
	using sgre::scene::SerialTask;
	using sgre::scene::SerialTaskCallbacks;
	sgre::scene::SerialTaskSet tasks(2);
	const auto tail = tasks.Append(std::make_unique<SerialTask>(SerialTaskCallbacks{}));
	const auto head = tasks.Prepend(std::make_unique<SerialTask>(SerialTaskCallbacks{}));
	std::array<sgre::scene::SerialTaskHandle, 2> order{};
	Require(tasks.WriteOrder(order) == 2 && order[0] == head && order[1] == tail, "prepend preserves explicit head/tail linked order");
	RequireError([&] {
		static_cast<void>(tasks.Append(std::make_unique<SerialTask>(SerialTaskCallbacks{})));
	},
	    "capacity");
	RequireError([&] {
		static_cast<void>(tasks.WriteOrder(std::span(order).first(1)));
	},
	    "smaller");
	tasks.SetStatus(head, sgre::scene::TaskStatus::Complete);
	RequireError([&] {
		tasks.Update();
	},
	    "external ownership receiver");
	Require(tasks.Contains(head) && tasks.Count() == 2, "missing completed ownership rejects before unlinking or destroying a live task");
	tasks.Clear();
	RequireError([&] {
		static_cast<void>(tasks.State(head));
	},
	    "stale");
	tasks.SetRetireWhenEmpty(false);
	tasks.Update();
	Require(tasks.Status() == sgre::scene::TaskStatus::Active, "disabled auto-retirement preserves Active on an empty serial update");
}

int main() {
	try {
		TestHeadTraversalDrawAndSameUpdateTransfer();
		TestTailAppendAndPersistentGroupStatus();
		TestCompletedOwnerOutlivesGroup();
		TestClearFinishesAfterFailure();
		TestSafetyAndPrependOrder();
		std::cout << "native serial traversal and completed ownership transfer passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
