#include "sgre/resource/psb_jobs.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
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

	throw std::runtime_error("expected PSB job rejection: " + std::string(message));
}

static void WriteWord(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t word) {
	for (std::size_t index = 0; index < 4; ++index) {
		bytes[offset + index] = static_cast<std::uint8_t>(word >> (index * 8));
	}
}

static std::vector<std::uint8_t> ScalarFixture() {
	// Independent PSB v3: empty names/strings/chunks and one signed-byte root atom.
	std::vector<std::uint8_t> bytes(44);
	bytes[0] = 'P';
	bytes[1] = 'S';
	bytes[2] = 'B';
	bytes[4] = 3;
	WriteWord(bytes, 8, 44);
	WriteWord(bytes, 12, 44);
	for (std::size_t array = 0; array < 2; ++array) {
		bytes.insert(bytes.end(), {13, 1, 13, 0});
	}
	bytes.insert(bytes.end(), {13, 0, 13});
	WriteWord(bytes, 36, static_cast<std::uint32_t>(bytes.size()));
	bytes.insert(bytes.end(), {5, 42});
	WriteWord(bytes, 16, static_cast<std::uint32_t>(bytes.size()));
	bytes.insert(bytes.end(), {13, 0, 13});
	WriteWord(bytes, 20, static_cast<std::uint32_t>(bytes.size()));
	WriteWord(bytes, 24, static_cast<std::uint32_t>(bytes.size()));
	bytes.insert(bytes.end(), {13, 0, 13});
	WriteWord(bytes, 28, static_cast<std::uint32_t>(bytes.size()));
	bytes.insert(bytes.end(), {13, 0, 13});
	WriteWord(bytes, 32, static_cast<std::uint32_t>(bytes.size()));
	std::uint32_t first = 1;
	std::uint32_t second = 0;
	for (std::size_t index = 8; index < 40; ++index) {
		first += bytes[index];
		second += first;
	}
	WriteWord(bytes, 40, (second << 16) | first);

	return bytes;
}

static void Publish(sgre::resource::PsbResourceJobs& jobs) {
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (!jobs.PublishCompleted()) {
		if (std::chrono::steady_clock::now() >= deadline) {
			throw std::runtime_error("bounded worker fixture did not publish before its deadline");
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}

struct WorkerPermit {
	std::atomic<bool>& Flag;
	~WorkerPermit() {
		Flag = true;
	}
};

static void TestWorkerOwnershipAndPublication() {
	sgre::resource::ResourceCache cache(0);
	std::atomic<bool> permit_read = false;
	std::atomic<std::size_t> reads = 0;
	const auto owner_thread = std::this_thread::get_id();
	sgre::resource::PsbResourceJobs jobs(cache);
	WorkerPermit permit_guard{permit_read};
	const auto handle = cache.Acquire("config/Exact.psb");
	jobs.Queue(handle, [&] {
		Require(std::this_thread::get_id() != owner_thread, "reader ran in owner/frame evaluation");
		while (!permit_read.load()) {
			std::this_thread::yield();
		}
		++reads;

		return ScalarFixture();
	});
	Require(!jobs.PublishCompleted() && cache.Info(handle).Completion == sgre::resource::ResourceCompletion::Pending, "publication blocked or completed before worker decoding");
	RequireError([&] {
		jobs.Queue(handle, ScalarFixture);
	},
	    "already queued");
	permit_read = true;
	Publish(jobs);
	Require(cache.Info(handle).Completion == sgre::resource::ResourceCompletion::Succeeded, cache.Info(handle).Error);
	const auto document = jobs.Document(handle);
	Require(reads == 1 && jobs.OutstandingJobs() == 0 && std::get<std::int64_t>(document->Root.Data) == 42, "worker result did not retain its original typed root");
	Require(cache.Info(handle).ChargedBytes == ScalarFixture().size() && cache.Info(handle).Residency == sgre::resource::ResourceResidency::Active, "PSB publication did not use complete native file-length charge");
	auto pin = cache.PinBytes(handle);
	cache.Release(handle);
	cache.ClearCache();
	jobs.CollectEvictedDocuments();
	Require(jobs.Document(handle) == document && pin->size() == 66, "cache maintenance destroyed an active native document owner");
	pin.reset();
	jobs.CollectEvictedDocuments();
	Require(!cache.Contains(handle) && std::get<std::int64_t>(document->Root.Data) == 42, "evicted backing invalidated independently owned parsed metadata");
	RequireError([&] {
		static_cast<void>(jobs.Document(handle));
	},
	    "stale");
}

static void TestFailuresAndQueueBounds() {
	sgre::resource::ResourceCacheLimits cache_limits;
	cache_limits.MaxEntries = 4;
	cache_limits.MaxEntryBytes = 66;
	cache_limits.MaxOwnedBytes = 66;
	sgre::resource::ResourceCache cache(66, cache_limits);
	sgre::resource::PsbJobLimits limits;
	limits.MaxOutstandingJobs = 1;
	limits.MaxDocumentSlots = 4;
	limits.MaxDecodedBytes = 66;
	limits.MaxMetadataBytes = 66;
	sgre::resource::PsbResourceJobs jobs(cache, limits);
	const auto malformed = cache.Acquire("malformed");
	jobs.Queue(malformed, [] {
		return std::vector<std::uint8_t>{1, 2, 3};
	});
	const auto waiting = cache.Acquire("waiting");
	RequireError([&] {
		jobs.Queue(waiting, ScalarFixture);
	},
	    "queue is full");
	Publish(jobs);
	Require(cache.Info(malformed).Completion == sgre::resource::ResourceCompletion::Failed && !cache.Info(malformed).Error.empty(), "malformed worker input remained pending or lost its diagnostic");
	cache.Release(malformed);
	jobs.Queue(waiting, ScalarFixture);
	Publish(jobs);
	const auto over_budget = cache.Acquire("over-budget");
	jobs.Queue(over_budget, ScalarFixture);
	Publish(jobs);
	Require(cache.Info(over_budget).Completion == sgre::resource::ResourceCompletion::Failed && cache.Info(over_budget).Error.find("allocation") != std::string::npos, "cache allocation rejection lost a completed job or remained pending");
	cache.Release(over_budget);
	RequireError([&] {
		jobs.Queue(waiting, ScalarFixture);
	},
	    "pending cache handle");
	cache.Release(waiting);
	cache.ClearCache();
	jobs.CollectEvictedDocuments();
	const auto oversized = cache.Acquire("oversized");
	jobs.Queue(oversized, [] {
		return std::vector<std::uint8_t>(67);
	});
	Publish(jobs);
	Require(cache.Info(oversized).Completion == sgre::resource::ResourceCompletion::Failed && cache.Info(oversized).Error.find("job allocation") != std::string::npos, "job decoding limit was not enforced");
	cache.Release(oversized);
}

static void TestShutdownCompletion() {
	sgre::resource::ResourceCache cache(128);
	const auto handle = cache.Acquire("shutdown");
	{
		sgre::resource::PsbResourceJobs jobs(cache);
		jobs.Queue(handle, ScalarFixture);
	}
	Require(cache.Info(handle).Completion == sgre::resource::ResourceCompletion::Failed && cache.Info(handle).Error.find("shut down") != std::string::npos, "service shutdown abandoned a pending acquired resource");
	cache.Release(handle);
	Require(!cache.Contains(handle), "failed shutdown resource survived its last reference");
}

int main() {
	try {
		TestWorkerOwnershipAndPublication();
		TestFailuresAndQueueBounds();
		TestShutdownCompletion();
		std::cout << "background PSB decoding, owner-thread publication, retention, bounds, and shutdown fixtures passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
