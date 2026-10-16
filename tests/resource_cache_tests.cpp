#include "sgre/resource/resource_cache.hpp"

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
static void RequireError(Operation operation, std::string_view message) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(message) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("expected rejected cache operation: " + std::string(message));
}

static void TestOwnershipAndTransitions() {
	sgre::resource::ResourceCache cache(16);
	const auto loading = cache.Acquire("script/CaseSensitive.nut");
	Require(loading == cache.Acquire("script/CaseSensitive.nut") && !cache.Contains("script/casesensitive.nut"), "resource identity was normalized or duplicate acquisition created another entry");
	cache.Release(loading);
	Require(cache.State().LoadingEntryNum == 1 && cache.Info(loading).References == 1, "pending resource reference accounting differs");
	RequireError([&] {
		static_cast<void>(cache.Bytes(loading));
	},
	    "successful acquired");
	cache.Complete(loading, {1, 2, 3, 4}, 4);
	Require(cache.State().LoadingEntryNum == 1, "worker completion changed registry state before native manager advancement");
	cache.Advance();
	Require(cache.Bytes(loading).size() == 4 && cache.Bytes(loading)[2] == 3 && cache.State().TotalActiveSize == 4, "successful resource did not become an owned active payload");
	RequireError([&] {
		cache.Complete(loading, {}, 0);
	},
	    "pending load");
	cache.Release(loading);
	Require(cache.State().CacheEntryNum == 1 && cache.State().ActiveEntryNum == 0 && cache.State().TotalCacheSize == 4, "last active reference did not transfer into retained cache");
	RequireError([&] {
		static_cast<void>(cache.Bytes(loading));
	},
	    "successful acquired");
	const auto revived = cache.Acquire("script/CaseSensitive.nut");
	Require(revived == loading && cache.Bytes(revived)[0] == 1, "cached revival reloaded or replaced the existing resource");
	cache.ClearCache();
	Require(cache.State().ActiveEntryNum == 1 && cache.Bytes(revived).size() == 4, "cache clearing destroyed an active resource");
	cache.Release(revived);
	cache.ClearCache();
	RequireError([&] {
		static_cast<void>(cache.Info(loading));
	},
	    "stale");
	const auto replacement = cache.Acquire("new");
	Require(replacement.Index == loading.Index && replacement.Generation != loading.Generation, "recycled cache storage retained its old identity");
}

static void TestPinnedOwnership() {
	sgre::resource::ResourceCache cache(0);
	const auto handle = cache.Acquire("config/state.psb");
	RequireError([&] {
		static_cast<void>(cache.PinBytes(handle));
	},
	    "successful acquired");
	cache.Complete(handle, {9, 8, 7}, 3);
	cache.Advance();
	auto pin = cache.PinBytes(handle);
	auto shared_pin = pin;
	Require(cache.Info(handle).References == 2, "shared pin copies acquired extra native references");
	cache.Release(handle);
	cache.ClearCache();
	cache.Advance();
	Require(cache.Info(handle).References == 1 && cache.State().TotalActiveSize == 3 && (*pin)[1] == 8, "cache policy invalidated a live PSB backing pin");
	pin.reset();
	Require(cache.Contains("config/state.psb"), "one shared pin release destroyed a still-owned resource");
	shared_pin.reset();
	Require(!cache.Contains("config/state.psb"), "last pin did not release its native active charge through zero-limit eviction");

	std::shared_ptr<const std::vector<std::uint8_t>> surviving_pin;
	{
		sgre::resource::ResourceCache short_lived(0);
		const auto transient = short_lived.Acquire("transient");
		short_lived.Complete(transient, {4, 5}, 2);
		short_lived.Advance();
		surviving_pin = short_lived.PinBytes(transient);
		short_lived.Release(transient);
	}
	Require(surviving_pin && surviving_pin->size() == 2 && (*surviving_pin)[0] == 4, "backing pin did not preserve the cache implementation beyond its facade lifetime");
	surviving_pin.reset();
}

static void TestRetentionAndSoftLimit() {
	sgre::resource::ResourceCache cache(8);
	const auto first = cache.Prefetch("a");
	const auto second = cache.Prefetch("b");
	cache.Complete(first, {1, 1, 1, 1}, 4);
	cache.Complete(second, {2, 2, 2, 2}, 4);
	cache.Advance();
	static_cast<void>(cache.Prefetch("a"));
	Require(!cache.Info(first).Discardable, "re-prefetching a cached resource did not promote its retention group");
	const auto active = cache.Acquire("c");
	cache.Complete(active, {3, 3, 3, 3}, 4);
	cache.Advance();
	Require(cache.Contains("a") && !cache.Contains("b") && cache.Contains("c"), "tail eviction did not preserve the promoted resource and active owner");
	Require(cache.State().TotalActiveSize == 4 && cache.State().TotalCacheSize == 4, "eviction charge did not include active resources");
	cache.SetLimit(0);
	Require(cache.Contains("a"), "native cache limit setter trimmed synchronously");
	cache.Advance();
	Require(!cache.Contains("a") && cache.Contains("c") && cache.Bytes(active).size() == 4, "zero soft limit evicted a referenced active resource");
	cache.Release(active);
	Require(!cache.Contains("c"), "unreferenced resource survived zero-limit trimming");
	const auto empty = cache.Prefetch("zero-sized");
	cache.Complete(empty, {}, 0);
	cache.Advance();
	Require(cache.State().CacheEntryNum == 1 && cache.State().TotalCacheSize == 0, "native zero-charge cache early return changed");
	cache.ClearCache();
	Require(!cache.Contains("zero-sized"), "explicit cache clearing retained a zero-charge entry");
}

static void TestFailuresAndLimits() {
	sgre::resource::ResourceCacheLimits limits;
	limits.MaxEntries = 2;
	limits.MaxNameBytes = 8;
	limits.MaxEntryBytes = 4;
	limits.MaxOwnedBytes = 6;
	sgre::resource::ResourceCache cache(6, limits);
	sgre::resource::ResourceCache another(6, limits);
	const auto first = cache.Acquire("error");
	cache.Fail(first, "missing original file");
	cache.Advance();
	Require(cache.Info(first).Completion == sgre::resource::ResourceCompletion::Failed && cache.Info(first).Error == "missing original file" && cache.State().ActiveEntryNum == 1, "load failure lost its acquired original diagnostic");
	RequireError([&] {
		static_cast<void>(cache.Bytes(first));
	},
	    "successful acquired");
	cache.Release(first);
	Require(!cache.Contains("error"), "failed unreferenced resource was retained as successful data");
	const auto pending = cache.Prefetch("pending");
	cache.Fail(pending, "bad format");
	cache.Advance();
	Require(!cache.Contains("pending"), "failed prefetch survived manager advancement");
	const auto one = cache.Acquire("one");
	const auto two = cache.Prefetch("two");
	RequireError([&] {
		static_cast<void>(cache.Acquire("three"));
	},
	    "entry count");
	RequireError([&] {
		static_cast<void>(cache.Acquire("too-long-name"));
	},
	    "name");
	RequireError([&] {
		another.Release(one);
	},
	    "another owner");
	RequireError([&] {
		cache.Release(two);
	},
	    "no acquired reference");
	RequireError([&] {
		cache.Complete(one, {1, 2, 3, 4, 5}, 5);
	},
	    "allocation");
	cache.Complete(one, {1, 2, 3, 4}, 4);
	RequireError([&] {
		cache.Complete(two, {1, 2, 3}, 3);
	},
	    "allocation");
	cache.Complete(two, {1, 2}, 2);
	cache.Advance();
	Require(cache.State().TotalActiveSize == 4 && cache.State().TotalCacheSize == 2, "allocation failure partially committed a payload or charge");
}

int main() {
	try {
		TestOwnershipAndTransitions();
		TestPinnedOwnership();
		TestRetentionAndSoftLimit();
		TestFailuresAndLimits();
		std::cout << "resource ownership, completion, retention priority, eviction, and limit fixtures passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
