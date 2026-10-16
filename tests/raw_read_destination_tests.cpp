#include "sgre/resource/resource_cache.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <vector>

using sgre::io::FileReadCompletion;
using sgre::io::PreparedRawFileRead;
using sgre::resource::ResourceCache;
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

	throw std::runtime_error("actual raw read destination fixture expected failure");
}

static PreparedRawFileRead Region(const std::filesystem::path& filename, std::uint64_t offset = 0, std::uint32_t length = 0) {
	PreparedRawFileRead source;
	source.Filename = filename;
	source.Offset = offset;
	source.Length = length;
	source.MaxBytes = 32;

	return source;
}

static void TestActualBytesPublishThroughTargetFrontier(const std::filesystem::path& filename) {
	ResourceCache cache(64);
	const auto first = cache.Acquire("z-first");
	const auto pending = cache.Acquire("m-pending");
	const auto later = cache.Acquire("a-later");
	auto& first_destination = cache.CreateRawReadDestination(first);
	auto& pending_destination = cache.CreateRawReadDestination(pending);
	auto& later_destination = cache.CreateRawReadDestination(later);
	first_destination.MarkQueued();
	pending_destination.MarkQueued();
	later_destination.MarkQueued();
	const auto first_source = Region(filename, 1, 3);
	const auto later_source = Region(filename, 4, 2);
	std::thread worker([&]() {
		first_destination.Select();
		first_destination.ProduceSelected(first_source);
		later_destination.Select();
		later_destination.ProduceSelected(later_source);
	});
	worker.join();
	Require(first_destination.Completion() == FileReadCompletion::Succeeded && later_destination.Completion() == FileReadCompletion::Succeeded, "actual FileReader region production must publish completion after its owned bytes");
	const std::array targets{first, pending, later};
	Require(cache.ObserveLoadingTargets(targets), "actual raw destinations must stop publication at the first pending target");
	const auto bytes = cache.Bytes(first);
	Require(bytes.size() == 3 && bytes[0] == 12 && bytes[1] == 13 && bytes[2] == 14, "worker payload must enter actual Active cache bytes through target observation");
	Require(cache.Info(first).ChargedBytes == 3 && cache.Info(later).Residency == ResourceResidency::Loading && cache.Info(later).Completion == ResourceCompletion::Pending, "target-order publication must defer the later real payload and its manager charge");
	cache.Advance();
	Require(cache.Info(later).Residency == ResourceResidency::Active && cache.Bytes(later).size() == 2 && cache.Bytes(later)[0] == 15, "independent manager Advance must consume the same real destination and transfer its map entry");
	Require(pending_destination.CancelQueued(), "actual pending queue owner may publish cancel3 before removing its request");
	Require(!cache.ObserveLoadingTargets(targets) && cache.Info(pending).Completion == ResourceCompletion::Failed && cache.Info(pending).Residency == ResourceResidency::Active, "cancel3 must use the native nonzero-completion target map transition");
	Require(cache.Info(first).References == 1 && cache.Info(later).References == 1 && cache.Info(pending).References == 1, "destination ownership must not add resource or script roots");
	cache.RequireCanRetireReadDestinations();
}

static void TestSelectedCannotCancelAndOwnerRejectsBeforeMutation(const std::filesystem::path& filename) {
	ResourceCache cache(64);
	const auto handle = cache.Acquire("selected");
	auto& destination = cache.CreateRawReadDestination(handle);
	destination.MarkQueued();
	std::promise<void> selected;
	auto selected_future = selected.get_future();
	std::promise<void> released;
	auto release = released.get_future();
	std::atomic<bool> foreign_rejected = false;
	const auto source = Region(filename);
	std::thread worker([&]() {
		destination.Select();
		Require(!cache.CanRetireReadDestinations(), "foreign worker preflight must reject before touching manager slots");
		try {
			cache.Advance();
		} catch (const std::runtime_error&) {
			foreign_rejected = true;
		}

		selected.set_value();
		release.get();
		destination.ProduceSelected(source);
	});
	selected_future.get();
	Require(!destination.CancelQueued() && !cache.CanRetireReadDestinations(), "selected request must retain its actual destination and reject queued-only cancellation");
	RequireFailure([&]() {
		cache.RequireCanRetireReadDestinations();
	});
	Require(cache.Contains(handle) && cache.Info(handle).Completion == ResourceCompletion::Pending, "retirement preflight must preserve the actual receiving Loading entry");
	released.set_value();
	worker.join();
	Require(foreign_rejected && cache.CanRetireReadDestinations(), "real producer completion must retire only the worker borrow");
	cache.Advance();
	Require(cache.Bytes(handle).size() == 6 && cache.Bytes(handle)[5] == 16, "native zero length selects the complete physical file size");
}

static void TestPendingReleaseAndClearPreserveReceivingOwner(const std::filesystem::path& filename) {
	ResourceCache cache(64);
	const auto handle = cache.Acquire("abandoned");
	auto& destination = cache.CreateRawReadDestination(handle);
	destination.MarkQueued();
	cache.Release(handle);
	cache.ClearCache();
	Require(cache.Contains(handle) && !cache.CanRetireReadDestinations() && destination.Completion() == FileReadCompletion::Pending, "last pending release/host cache clear must retain actual borrowed output cells before an explicit real queue cancellation");
	destination.Select();
	destination.ProduceSelected(Region(filename, 2, 2));
	cache.Advance();
	Require(cache.Contains(handle) && cache.Info(handle).Residency == ResourceResidency::Cached && cache.Info(handle).References == 0, "unreferenced real read success must follow native cache retention after worker release");
	cache.ClearCache();
	Require(!cache.Contains(handle), "retired actual destination may be destroyed with its evicted receiving entry");
}

static void TestRealReadFailuresAndNoRemainingLengthGuess(const std::filesystem::path& filename) {
	ResourceCache cache(64);
	const auto bad_region = cache.Acquire("bad-range");
	auto& destination = cache.CreateRawReadDestination(bad_region);
	destination.MarkQueued();
	destination.Select();
	destination.ProduceSelected(Region(filename, 1, 0));
	Require(destination.Completion() == FileReadCompletion::ReadFailed, "zero native length with nonzero offset must not invent a remaining-byte range");
	cache.Advance();
	Require(cache.Info(bad_region).Completion == ResourceCompletion::Failed && !cache.Info(bad_region).Error.empty(), "real checked reader failure must publish a bounded actual failure diagnostic");
	const auto missing = cache.Acquire("missing");
	auto& missing_destination = cache.CreateRawReadDestination(missing);
	missing_destination.MarkQueued();
	missing_destination.Select();
	missing_destination.ProduceSelected(Region(filename.parent_path() / "does-not-exist.bin"));
	cache.Advance();
	Require(cache.Info(missing).Completion == ResourceCompletion::Failed && cache.State().TotalActiveSize == 0, "physical open failure must not fabricate data or charge");
}

static void TestCompletionAllocationPreflightRetainsPreparedBytes(const std::filesystem::path& filename) {
	sgre::resource::ResourceCacheLimits limits;
	limits.MaxEntryBytes = 4;
	limits.MaxOwnedBytes = 4;
	ResourceCache cache(4, limits);
	const auto occupied = cache.Acquire("occupied");
	cache.Complete(occupied, {31, 32}, 2);
	cache.Advance();
	const auto next = cache.Acquire("next");
	auto& destination = cache.CreateRawReadDestination(next);
	destination.MarkQueued();
	destination.Select();
	destination.ProduceSelected(Region(filename, 0, 3));
	RequireFailure([&]() {
		cache.Advance();
	});
	Require(cache.Info(next).Completion == ResourceCompletion::Pending && destination.Completion() == FileReadCompletion::Succeeded, "cache allocation rejection must preserve the unconsumed prepared destination");
	cache.Release(occupied);
	cache.ClearCache();
	cache.Advance();
	Require(cache.Info(next).Residency == ResourceResidency::Active && cache.Bytes(next).size() == 3 && cache.Bytes(next)[2] == 13, "later supported capacity must import the same actual prepared payload once");
}

static void TestExclusiveDestinationAndQueuedCancel() {
	ResourceCache cache(64);
	const auto handle = cache.Prefetch("cancelled");
	auto& destination = cache.CreateRawReadDestination(handle);
	RequireFailure([&]() {
		static_cast<void>(cache.CreateRawReadDestination(handle));
	});
	destination.MarkQueued();
	RequireFailure([&]() {
		destination.MarkQueued();
	});
	RequireFailure([&]() {
		cache.Complete(handle, {1}, 1);
	});
	RequireFailure([&]() {
		cache.Fail(handle, "substitute completion");
	});
	Require(destination.CancelQueued() && !destination.CancelQueued(), "actual pending cancellation must publish3 only once");
	RequireFailure([&]() {
		destination.Select();
	});
	cache.Advance();
	Require(!cache.Contains(handle), "unreferenced cancel3 must be destroyed after its actual manager completion transition");
}

static void TestRejectedDestinationDestructor(bool selected) {
	std::set_terminate([]() {
		std::_Exit(0);
	});
	ResourceCache cache(0);
	const auto handle = cache.Acquire("unsafe");
	auto& destination = cache.CreateRawReadDestination(handle);
	destination.MarkQueued();
	if (selected) {
		destination.Select();
	}
}

int main(int argc, char** argv) {
	if (argc == 2 && std::string_view(argv[1]) == "--queued-destination-destruction") {
		TestRejectedDestinationDestructor(false);

		return 1;
	}

	if (argc == 2 && std::string_view(argv[1]) == "--selected-destination-destruction") {
		TestRejectedDestinationDestructor(true);

		return 1;
	}

	try {
		if (argc != 2) {
			throw std::runtime_error("raw destination fixture requires one ignored build/research directory");
		}

		const std::filesystem::path directory(argv[1]);
		std::filesystem::create_directories(directory);
		const auto filename = directory / "raw-destination-fixture.bin";
		{
			std::ofstream file(filename, std::ios::binary | std::ios::trunc);
			const std::array<char, 6> bytes{11, 12, 13, 14, 15, 16};
			file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
			if (!file) {
				throw std::runtime_error("synthetic raw destination fixture write failed");
			}
		}

		TestActualBytesPublishThroughTargetFrontier(filename);
		TestSelectedCannotCancelAndOwnerRejectsBeforeMutation(filename);
		TestPendingReleaseAndClearPreserveReceivingOwner(filename);
		TestRealReadFailuresAndNoRemainingLengthGuess(filename);
		TestCompletionAllocationPreflightRetainsPreparedBytes(filename);
		TestExclusiveDestinationAndQueuedCancel();
		std::filesystem::remove(filename);
		std::cout << "six independent real raw read destination groups pass\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
