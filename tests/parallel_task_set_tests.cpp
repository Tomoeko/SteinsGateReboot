#include "sgre/scene/parallel_task_set.hpp"

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

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view expected) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(expected) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("parallel task fixture expected failure: " + std::string(expected));
}

static void TestActiveCompletePausedAndRetire() {
	sgre::scene::ParallelTaskSet tasks(4);
	std::vector<std::uint32_t> events;
	sgre::scene::ParallelTaskCallbacks first;
	first.Update = [&](auto task) {
		events.push_back(1);
		tasks.SetStatus(task, sgre::scene::TaskStatus::Complete);
	};
	first.Draw = [&](auto, auto argument) {
		Require(argument == 0xffffffffu, "native draw argument must retain all 32 bits");
		events.push_back(2);
	};
	first.Release = [&](auto task) {
		Require(tasks.Contains(task) && tasks.Count() == 1, "retirement callback must observe its valid task fields after list unlink");
		events.push_back(3);
	};
	const auto active = tasks.Append(std::move(first));
	tasks.SetDrawPaused(active, true);
	sgre::scene::ParallelTaskCallbacks second;
	second.Draw = [&](auto, auto) {
		events.push_back(4);
	};
	const auto idle = tasks.Append(std::move(second), {sgre::scene::TaskStatus::Complete, false});
	tasks.Update();
	tasks.Update();
	tasks.Draw(0xffffffffu);
	Require(events == std::vector<std::uint32_t>{1, 4}, "paused drawing must not pause update, while Complete remains owned and drawable without updating");
	tasks.SetDrawPaused(active, false);
	tasks.Draw(0xffffffffu);
	tasks.SetStatus(active, sgre::scene::TaskStatus::Retire);
	tasks.Draw(0xffffffffu);
	Require(events == std::vector<std::uint32_t>{1, 4, 2, 4, 4} && tasks.Count() == 2, "marked tasks skip drawing before the next update actually disposes them");
	tasks.Update();
	Require(events.back() == 3 && !tasks.Contains(active) && tasks.Contains(idle), "next parallel update must unlink and dispose a marked task exactly once");
	tasks.Clear();
}

static void TestStableOrderAndAppendDuringUpdate() {
	sgre::scene::ParallelTaskSet tasks(4);
	std::vector<std::uint32_t> events;
	sgre::scene::ParallelTaskCallbacks first;
	first.Update = [&](auto handle) {
		events.push_back(1);
		sgre::scene::ParallelTaskCallbacks child;
		child.Update = [&](auto) {
			events.push_back(3);
		};
		static_cast<void>(tasks.Append(std::move(child)));
		tasks.SetStatus(handle, sgre::scene::TaskStatus::Complete);
	};
	const auto tail = tasks.Append(std::move(first));
	sgre::scene::ParallelTaskCallbacks preceding;
	preceding.Update = [&](auto) {
		events.push_back(2);
	};
	const auto head = tasks.Prepend(std::move(preceding));
	tasks.Update();
	Require(events == std::vector<std::uint32_t>{2, 1, 3}, "tail append during a native update must become visible to that same traversal");
	std::array<sgre::scene::ParallelTaskHandle, 4> order{};
	Require(tasks.WriteOrder(order) == 3 && order[0] == head && order[1] == tail, "prepend and surviving callbacks must preserve the actual intrusive list order");
	tasks.Clear();
}

static void TestAppendDuringDrawAndEmptyRetirement() {
	sgre::scene::ParallelTaskSet tasks(3);
	std::vector<std::uint32_t> events;
	sgre::scene::ParallelTaskCallbacks source;
	source.Draw = [&](auto handle, auto argument) {
		Require(argument == 17, "draw forwarding changed its argument");
		events.push_back(1);
		sgre::scene::ParallelTaskCallbacks child;
		child.Draw = [&](auto, auto value) {
			Require(value == 17, "newly appended drawable must receive the same forwarded argument");
			events.push_back(2);
		};
		static_cast<void>(tasks.Append(std::move(child)));
		tasks.SetDrawPaused(handle, true);
	};
	static_cast<void>(tasks.Append(std::move(source)));
	tasks.Draw(17);
	Require(events == std::vector<std::uint32_t>{1, 2}, "draw must fetch the actual next link after a callback can append");
	tasks.SetRetireWhenEmpty(true);
	tasks.Clear();
	Require(tasks.Status() == sgre::scene::TaskStatus::Active, "native Clear does not itself mark the empty set for retirement");
	tasks.Update();
	Require(tasks.Status() == sgre::scene::TaskStatus::Retire, "native empty-retirement flag takes effect in update");
	static_cast<void>(tasks.Append({}));
	Require(tasks.Status() == sgre::scene::TaskStatus::Retire, "append does not silently reset a previously marked set status");
	tasks.Clear();
}

static void TestFailureCompletingClearAndStaleReuse() {
	sgre::scene::ParallelTaskSet tasks(2);
	std::vector<std::uint32_t> events;
	sgre::scene::ParallelTaskCallbacks first;
	first.Release = [&](auto) {
		events.push_back(1);
		throw std::runtime_error("prepared release failure");
	};
	const auto retired = tasks.Append(std::move(first));
	sgre::scene::ParallelTaskCallbacks second;
	second.Release = [&](auto) {
		events.push_back(2);
	};
	static_cast<void>(tasks.Append(std::move(second)));
	RequireFailure([&] {
		tasks.Clear();
	},
	    "prepared release failure");
	Require(events == std::vector<std::uint32_t>{1, 2} && tasks.Count() == 0 && !tasks.Contains(retired), "authored Clear must retire every task before reporting its first cleanup failure");
	const auto replacement = tasks.Append({});
	Require(replacement != retired && !tasks.Contains(retired), "a reused task slot must never resurrect a retired generation");
	RequireFailure([&] {
		static_cast<void>(tasks.State(retired));
	},
	    "stale");
	sgre::scene::ParallelTaskSet foreign(1);
	Require(!foreign.Contains(replacement), "borrowed task identity must remain scoped to its actual parent set");
	tasks.Clear();
}

static void TestTraversalAndRetirementGuards() {
	sgre::scene::ParallelTaskSet tasks(1);
	sgre::scene::ParallelTaskCallbacks callbacks;
	callbacks.Update = [&](auto task) {
		RequireFailure([&] {
			tasks.Clear();
		},
		    "reenter");
		RequireFailure([&] {
			static_cast<void>(tasks.Prepend({}));
		},
		    "stable traversal");
		tasks.SetStatus(task, sgre::scene::TaskStatus::Retire);
		throw std::runtime_error("prepared update failure");
	};
	callbacks.Release = [&](auto task) {
		RequireFailure([&] {
			tasks.SetStatus(task, sgre::scene::TaskStatus::Active);
		},
		    "reenter retirement");
		RequireFailure([&] {
			static_cast<void>(tasks.Append({}));
		},
		    "reenter retirement");
	};
	const auto task = tasks.Append(std::move(callbacks));
	RequireFailure([&] {
		tasks.Update();
	},
	    "prepared update failure");
	Require(tasks.Contains(task) && tasks.State(task).Status == sgre::scene::TaskStatus::Retire, "authored callback failure must leave its stored task state visible and traversal guards restored");
	tasks.Update();
	Require(!tasks.Contains(task) && tasks.Count() == 0, "later update must dispose a previously marked task without rerunning its failing callback");
	const auto replacement = tasks.Append({});
	RequireFailure([&] {
		static_cast<void>(tasks.Append({}));
	},
	    "capacity");
	RequireFailure([&] {
		tasks.SetStatus(replacement, static_cast<sgre::scene::TaskStatus>(7));
	},
	    "0..2");
	tasks.Clear();
}

struct CallbackOwner {
	std::size_t& Releases;
	std::size_t& Destructions;
	bool CanRelease = true;
	std::size_t Draws = 0;

	CallbackOwner(std::size_t& releases, std::size_t& destructions)
	    : Releases(releases), Destructions(destructions) {
	}

	~CallbackOwner() {
		++Destructions;
	}
};

static sgre::scene::ParallelTaskCallbacks OwningCallbacks(std::weak_ptr<CallbackOwner>& observed, std::size_t& releases, std::size_t& destructions) {
	auto owner = std::make_shared<CallbackOwner>(releases, destructions);
	observed = owner;
	sgre::scene::ParallelTaskCallbacks callbacks;
	callbacks.Draw = [owner](auto, auto) {
		++owner->Draws;
	};
	callbacks.CanRelease = [owner](auto) {
		return owner->CanRelease;
	};
	callbacks.Release = [owner](auto) {
		++owner->Releases;
	};
	return callbacks;
}

static sgre::scene::ParallelTaskLease OwningLease(std::weak_ptr<CallbackOwner>& observed, std::size_t& releases, std::size_t& destructions) {
	sgre::scene::ParallelTaskSet tasks(1);
	const auto handle = tasks.Append(OwningCallbacks(observed, releases, destructions));
	return tasks.Extract(handle);
}

static void TestExtractedCallbackOwnership() {
	std::size_t releases = 0;
	std::size_t destructions = 0;
	std::weak_ptr<CallbackOwner> observed;
	sgre::scene::ParallelTaskSet tasks(1);
	const auto handle = tasks.Append(OwningCallbacks(observed, releases, destructions));
	auto lease = tasks.Extract(handle);
	Require(tasks.Count() == 0 && !tasks.Contains(handle) && !observed.expired() && releases == 0, "extraction must preserve the detached payload without calling retirement");
	lease.Release();
	Require(lease.IsEmpty() && releases == 1 && destructions == 1 && observed.expired(), "extracted callback owner must retire while its old free record and empty lease remain alive");
	const auto reused = tasks.Append({});
	Require(reused != handle && releases == 1 && destructions == 1, "slot reuse must neither retain nor retire the former payload again");
	tasks.Clear();
}

static void TestMovedLeaseCallbackOwnership() {
	std::size_t releases = 0;
	std::size_t destructions = 0;
	std::weak_ptr<CallbackOwner> observed;
	auto source = OwningLease(observed, releases, destructions);
	sgre::scene::ParallelTaskLease destination(std::move(source));
	Require(source.IsEmpty() && !destination.IsEmpty() && !observed.expired() && releases == 0, "lease move construction must transfer the unretired actual owner");
	destination.Release();
	Require(source.IsEmpty() && releases == 1 && destructions == 1 && observed.expired(), "moved-from lease must hold no callback owner after destination release");

	std::size_t incoming_releases = 0;
	std::size_t incoming_destructions = 0;
	std::weak_ptr<CallbackOwner> incoming;
	auto incoming_lease = OwningLease(incoming, incoming_releases, incoming_destructions);
	std::size_t previous_releases = 0;
	std::size_t previous_destructions = 0;
	std::weak_ptr<CallbackOwner> previous;
	auto assigned = OwningLease(previous, previous_releases, previous_destructions);
	assigned = std::move(incoming_lease);
	Require(incoming_lease.IsEmpty() && previous.expired() && previous_releases == 1 && previous_destructions == 1 && !incoming.expired(), "lease move assignment must retire the old owner and transfer the new one once");
	assigned.Release();
	Require(incoming.expired() && incoming_releases == 1 && incoming_destructions == 1, "move-assigned source must hold no callbacks after actual destination release");
}

static void TestReattachedCallbackOwnership() {
	std::size_t releases = 0;
	std::size_t destructions = 0;
	std::weak_ptr<CallbackOwner> observed;
	auto lease = OwningLease(observed, releases, destructions);
	sgre::scene::ParallelTaskSet tasks(1);
	const auto handle = tasks.AppendLease(std::move(lease));
	Require(lease.IsEmpty() && tasks.Contains(handle) && releases == 0 && !observed.expired(), "reattachment must move a live owner without retiring its payload");
	tasks.Draw(0);
	Require(observed.lock()->Draws == 1, "reattached actual callback must still execute in its new task set");
	tasks.Clear();
	Require(observed.expired() && releases == 1 && destructions == 1, "empty reattached lease must not retain callback ownership after the new task retires");
}

int main() {
	try {
		TestActiveCompletePausedAndRetire();
		TestStableOrderAndAppendDuringUpdate();
		TestAppendDuringDrawAndEmptyRetirement();
		TestFailureCompletingClearAndStaleReuse();
		TestTraversalAndRetirementGuards();
		TestExtractedCallbackOwnership();
		TestMovedLeaseCallbackOwnership();
		TestReattachedCallbackOwnership();
		std::cout << "Native parallel task ownership and traversal fixtures passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
