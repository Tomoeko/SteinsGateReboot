#include "sgre/scene/parallel_task_set.hpp"

#include <iostream>
#include <stdexcept>
#include <utility>

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
	throw std::runtime_error("expected target extraction rejection");
}

static void TestExactTarget() {
	sgre::scene::ParallelTaskSet group(3);
	sgre::scene::ParallelTaskSet foreign(1);
	std::size_t target_checks = 0;
	std::size_t unrelated_checks = 0;
	std::size_t target_releases = 0;
	bool unrelated_ready = false;
	bool target_throws = false;
	sgre::scene::ParallelTaskCallbacks unrelated;
	unrelated.CanRelease = [&](auto) {
		++unrelated_checks;
		return unrelated_ready;
	};
	const auto other = group.Append(std::move(unrelated));
	sgre::scene::ParallelTaskCallbacks target;
	target.CanRelease = [&](auto) {
		++target_checks;
		if (target_throws) {
			throw std::runtime_error("authored target preflight failure");
		}
		return true;
	};
	target.Update = [&](auto exact) {
		const auto before = target_checks;
		Require(!group.CanExtract(exact) && target_checks == before, "active Update invoked retirement preflight or admitted extraction");
	};
	target.Draw = [&](auto exact, auto) {
		const auto before = target_checks;
		Require(!group.CanExtract(exact) && target_checks == before, "active Draw invoked retirement preflight or admitted extraction");
	};
	target.Release = [&](auto) {
		++target_releases;
	};
	const auto exact = group.Append(std::move(target));
	const auto foreign_handle = foreign.Append({});
	Require(!group.CanExtract(foreign_handle) && target_checks == 0 && unrelated_checks == 0, "foreign target invoked an unrelated parent");
	Require(group.CanExtract(exact) && target_checks == 1 && unrelated_checks == 0, "exact target was blocked by an unrelated live borrowed parent");
	Require(!group.CanClear() && unrelated_checks == 1, "whole-set Clear lost its all-parent borrow policy");
	RequireFailure([&] {
		group.Clear();
	});
	group.Update();
	group.Draw(9);
	target_throws = true;
	Require(!group.CanExtract(exact), "throwing target preflight escaped noexcept admission");
	RequireFailure([&] {
		static_cast<void>(group.Extract(exact));
	});
	Require(group.Contains(exact) && group.Contains(other) && group.Count() == 2, "failed target preflight changed list ownership");
	target_throws = false;
	const auto unrelated_before = unrelated_checks;
	auto lease = group.Extract(exact);
	Require(!group.Contains(exact) && group.Contains(other) && group.Count() == 1 && unrelated_checks == unrelated_before && target_releases == 0, "exact extraction released or consulted unrelated ownership");
	Require(!group.CanExtract(exact), "stale target remained extractable");
	lease.Release();
	Require(target_releases == 1 && group.Count() == 1, "detached actual target did not retire exactly once");
	const auto reused = group.Append({});
	Require(reused.Index == exact.Index && reused.Generation != exact.Generation && !group.CanExtract(exact), "actual generation reuse accepted a stale handle");
	unrelated_ready = true;
	group.Clear();
	foreign.Clear();
}

static void TestReleaseBarrier() {
	sgre::scene::ParallelTaskSet group(2);
	sgre::scene::ParallelTaskHandle exact;
	std::size_t preflights = 0;
	sgre::scene::ParallelTaskCallbacks target;
	target.CanRelease = [&](auto) {
		++preflights;
		return true;
	};
	target.Release = [&](auto) {
		const auto before = preflights;
		Require(!group.CanExtract(exact) && preflights == before, "Release reentry invoked target preflight or admitted extraction");
	};
	exact = group.Append(std::move(target));
	group.Clear();
	Require(group.Count() == 0 && !group.CanExtract(exact), "Clear's actual release barrier left an owned target");
}

int main() {
	try {
		TestExactTarget();
		TestReleaseBarrier();
		std::cout << "PASS target-only actual extraction, unrelated borrowed parent retained, whole-set Clear policy, active Update/Draw/Release rejection and throw/stale/foreign safety; no native execution\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
