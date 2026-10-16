#include "sgre/resource/resource_cache.hpp"

#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <vector>

using sgre::resource::CachedResourceHandle;
using sgre::resource::ResourceCache;
using sgre::resource::ResourceCacheLimits;
using sgre::resource::ResourceCompletion;
using sgre::resource::ResourceResidency;

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

	throw std::runtime_error("resource loading frontier fixture expected failure");
}

static void TestTargetOrderPublicationStopsAtFirstPending() {
	ResourceCache cache(64);
	const auto first = cache.Acquire("z-first.bin");
	const auto pending = cache.Acquire("m-pending.bin");
	const auto later = cache.Acquire("a-later.bin");
	cache.Complete(first, {1, 2}, 2);
	cache.Complete(later, {3, 4, 5}, 3);
	const std::array targets{first, pending, later};
	Require(cache.ObserveLoadingTargets(targets), "target list must stop at its first pending request");
	Require(cache.Info(first).Residency == ResourceResidency::Active && cache.Info(pending).Residency == ResourceResidency::Loading && cache.Info(later).Residency == ResourceResidency::Loading, "poll must promote only preceding completed targets in declared order, not manager name order");
	Require(cache.State().TotalActiveSize == 2 && cache.State().LoadingEntryNum == 2, "partial poll publication must update actual manager membership and accounting");
	RequireFailure([&]() {
		static_cast<void>(cache.Bytes(later));
	});
	const auto bytes = cache.Bytes(first);
	Require(bytes.size() == 2 && bytes[0] == 1 && bytes[1] == 2, "poll publication must expose the actual prepared payload");
	cache.Complete(pending, {6}, 1);
	Require(!cache.ObserveLoadingTargets(targets), "all completed targets must leave loading after the next poll");
	Require(cache.State().TotalActiveSize == 6 && cache.State().ActiveEntryNum == 3, "later completion must activate actual targets without adding references");
	Require(cache.Info(first).References == 1 && cache.Info(pending).References == 1 && cache.Info(later).References == 1, "loading observation must not fabricate payload pins");
	Require(!cache.ObserveLoadingTargets(targets), "existing active target observation must be idempotent");
}

static void TestManagerAdvanceIsIndependentFromTargetFrontier() {
	ResourceCache cache(64);
	const auto pending = cache.Acquire("z-pending");
	const auto later = cache.Acquire("a-later");
	cache.Complete(later, {31, 32}, 2);
	const std::array targets{pending, later};
	Require(cache.ObserveLoadingTargets(targets) && cache.Info(later).Residency == ResourceResidency::Loading, "an earlier pending target must leave a later completed target unpublished by this poll");
	cache.Advance();
	Require(cache.Info(later).Residency == ResourceResidency::Active && cache.Info(pending).Residency == ResourceResidency::Loading, "ordinary native manager update independently publishes later completed map entries");
	Require(cache.ObserveLoadingTargets(targets), "an independently activated future target must not bypass the earlier pending target");
	cache.Fail(pending, "independent read failure");
	Require(!cache.ObserveLoadingTargets(targets) && cache.Info(pending).Residency == ResourceResidency::Active && cache.Info(pending).Completion == ResourceCompletion::Failed, "native nonzero failed completion must leave loading through the same target frontier");
}

static void TestPerTargetTrimAndDuplicateReferences() {
	ResourceCache cache(5);
	const auto retained = cache.Prefetch("retained");
	cache.Complete(retained, {11, 12, 13, 14}, 4);
	cache.Advance();
	const auto first = cache.Acquire("first");
	const auto again = cache.Acquire("first");
	const auto pending = cache.Acquire("pending");
	cache.Complete(first, {21, 22}, 2);
	const std::array targets{first, again, pending};
	Require(cache.ObserveLoadingTargets(targets), "duplicate active target must preserve the following pending frontier");
	Require(!cache.Contains(retained) && cache.State().TotalActiveSize == 2 && cache.State().TotalCacheSize == 0, "completed target promotion must trim retained resources before reaching the pending target");
	Require(cache.Info(first).References == 2, "duplicate target observation must preserve only the real acquired references");
	cache.Release(first);
	cache.Release(again);
	Require(cache.Info(first).Residency == ResourceResidency::Cached, "poll must not add an invisible root that prevents real last-reference retention");
}

static void TestWholeTargetPreflightAndEmptyDomain() {
	ResourceCache cache(64);
	ResourceCache foreign(64);
	const auto first = cache.Acquire("first");
	const auto other = foreign.Acquire("foreign");
	cache.Complete(first, {7}, 1);
	const std::array foreign_targets{first, other};
	RequireFailure([&]() {
		static_cast<void>(cache.ObserveLoadingTargets(foreign_targets));
	});
	Require(cache.Info(first).Residency == ResourceResidency::Loading, "authored ownership preflight must precede partial publication for an invalid future target");
	const auto unowned = cache.Prefetch("not-acquired");
	const std::array unowned_targets{first, unowned};
	RequireFailure([&]() {
		static_cast<void>(cache.ObserveLoadingTargets(unowned_targets));
	});
	Require(cache.Info(first).Residency == ResourceResidency::Loading, "missing actual target references must reject before any prior transition");
	const auto evicted = cache.Prefetch("evicted");
	cache.Complete(evicted, {8}, 1);
	cache.Advance();
	cache.ClearCache();
	const std::array stale_targets{first, evicted};
	RequireFailure([&]() {
		static_cast<void>(cache.ObserveLoadingTargets(stale_targets));
	});
	Require(!cache.ObserveLoadingTargets({}), "an empty native Resource target list is not loading");
}

static void TestBoundedTargetWork() {
	ResourceCacheLimits limits;
	limits.MaxEntries = 2;
	ResourceCache cache(64, limits);
	const auto target = cache.Acquire("target");
	cache.Complete(target, {9}, 1);
	const std::vector<CachedResourceHandle> too_many(65537, target);
	RequireFailure([&]() {
		static_cast<void>(cache.ObserveLoadingTargets(too_many));
	});
	Require(cache.Info(target).Residency == ResourceResidency::Loading, "work-bound rejection must preserve actual manager membership");
	const std::array duplicates{target, target, target};
	Require(!cache.ObserveLoadingTargets(duplicates) && cache.Info(target).References == 1, "target count and distinct cache-entry capacity are separate bounds");
}

int main() {
	try {
		TestTargetOrderPublicationStopsAtFirstPending();
		TestManagerAdvanceIsIndependentFromTargetFrontier();
		TestPerTargetTrimAndDuplicateReferences();
		TestWholeTargetPreflightAndEmptyDomain();
		TestBoundedTargetWork();
		std::cout << "five independent actual loading target frontier groups pass\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
