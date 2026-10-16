#include "sgre/io/persistent_raw_queue.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
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
using sgre::io::FileReadCompletion;
using sgre::io::PersistentRawReadQueue;
using sgre::io::PreparedRawFileRead;
using sgre::io::RawReadRequestHandle;
using sgre::resource::ResourceCache;
using sgre::resource::ResourceCompletion;
using sgre::resource::ResourceResidency;
using sgre::scene::CommonProcessTasks;

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

	throw std::runtime_error("actual persistent raw queue fixture expected failure");
}

static PreparedRawFileRead Region(const std::filesystem::path& filename, std::uint64_t offset, std::uint32_t length = 1) {
	PreparedRawFileRead source;
	source.Filename = filename;
	source.Offset = offset;
	source.Length = length;
	source.MaxBytes = 32;

	return source;
}

static std::vector<RawReadRequestHandle> Order(PersistentRawReadQueue& queue) {
	std::vector<RawReadRequestHandle> result(queue.PendingCount());
	Require(queue.WritePendingOrder(result) == result.size(), "actual pending pointer-list order must fill its bounded view");

	return result;
}

static void DrainReadJob(PersistentRawReadQueue& queue, BackgroundCallManager& background) {
	std::promise<void> barrier;
	auto completed = barrier.get_future();
	BackgroundCallTask after_read(background, BackgroundCallLane::BelowNormal, [&]() {
		barrier.set_value();
	});
	after_read.Update();
	background.PostLoop();
	completed.get();
	queue.UpdatePreparedRequests();
	after_read.Close();
}

static void TestActualResortAndFrameSerialDispatch(const std::filesystem::path& filename) {
	ResourceCache cache(64);
	CommonProcessTasks processor;
	BackgroundCallManager background;
	std::vector<std::uint64_t> physical_order;
	PersistentRawReadQueue queue(cache, processor, background, [&](auto& destination, const auto& source) {
		physical_order.push_back(source.Offset);

		return destination.ReadSelected(source);
	});
	const auto a = cache.Prefetch("a");
	const auto b = cache.Acquire("b");
	const auto c = cache.Prefetch("c");
	const auto d = cache.Acquire("d");
	const auto ar = queue.QueuePrepared(a, "a", Region(filename, 0));
	const auto br = queue.QueuePrepared(b, "b", Region(filename, 1));
	const auto cr = queue.QueuePrepared(c, "c", Region(filename, 2));
	const auto dr = queue.QueuePrepared(d, "d", Region(filename, 3));
	Require(Order(queue) == std::vector<RawReadRequestHandle>{br, dr, ar, cr}, "actual new-request publication must stable-sort positive native Loading references before zero");
	Require(cache.Acquire("c") == c, "pending acquire must coalesce its actual receiving entry");
	Require(Order(queue) == std::vector<RawReadRequestHandle>{br, dr, cr, ar}, "old zero-to-positive reference crossing must resort the real pointer list");
	cache.Release(b);
	Require(Order(queue) == std::vector<RawReadRequestHandle>{dr, cr, br, ar}, "last pending release must resort without removing or cancelling that receiving entry");
	queue.UpdatePreparedRequests();
	Require(processor.Serial().Count() == 1 && queue.PendingCount() == 4 && physical_order.empty(), "native prepared onProcess must append/enqueue actual lane2 serial work without waking its idle worker");
	queue.UpdatePreparedRequests();
	Require(processor.Serial().Count() == 1 && physical_order.empty(), "linked serial child must suppress duplicate actual read-loop publication");
	DrainReadJob(queue, background);
	Require(physical_order == std::vector<std::uint64_t>{3, 2, 1, 0} && processor.Serial().Count() == 0 && queue.PendingCount() == 0, "real FileReader dispatch must follow the actual sorted queue and retire its concrete serial child");
	cache.Advance();
	Require(cache.Info(c).Residency == ResourceResidency::Active && cache.Bytes(c)[0] == 33 && cache.Info(a).Residency == ResourceResidency::Cached, "ordinary manager publication must consume the actual raw receiving entries without pins");
	queue.Close();
	Require(cache.CanRetireReadDestinations(), "actual queue teardown must detach its callback/lifetime relationship before manager root retirement");
	background.Close();
}

static void TestPhysicalQueuedCancellationAndClearCache(const std::filesystem::path& filename) {
	ResourceCache cache(64);
	CommonProcessTasks processor;
	BackgroundCallManager background;
	std::int32_t reads = 0;
	PersistentRawReadQueue queue(cache, processor, background, [&](auto& destination, const auto& source) {
		++reads;

		return destination.ReadSelected(source);
	});
	const auto discard = cache.Prefetch("discard");
	const auto active = cache.Acquire("active");
	const auto discarded_request = queue.QueuePrepared(discard, "discard", Region(filename, 0));
	const auto active_request = queue.QueuePrepared(active, "active", Region(filename, 1));
	cache.ClearCache();
	Require(queue.PendingCount() == 1 && Order(queue) == std::vector<RawReadRequestHandle>{active_request}, "ClearCache must pair native cancel3 with physical pending pointer removal for unreferenced Loading work");
	cache.Advance();
	Require(!cache.Contains(discard) && !queue.CancelPending(discarded_request), "cancelled request must not leave a dangling queued receiver after manager failure cleanup");
	queue.UpdatePreparedRequests();
	DrainReadJob(queue, background);
	cache.Advance();
	Require(reads == 1 && cache.Bytes(active)[0] == 22, "actual cancelled pending work must never execute its physical reader");
	queue.Close();
	background.Close();
}

static void TestSelectedBoundaryAndFutureRemoval(const std::filesystem::path& filename) {
	ResourceCache cache(64);
	CommonProcessTasks processor;
	BackgroundCallManager background;
	std::promise<void> read_complete;
	auto current_read = read_complete.get_future();
	std::promise<void> release;
	auto released = release.get_future();
	std::atomic<bool> self_rejected = false;
	PersistentRawReadQueue* current_queue = nullptr;
	std::vector<std::uint64_t> reads;
	PersistentRawReadQueue queue(cache, processor, background, [&](auto& destination, const auto& source) {
		const auto completion = destination.ReadSelected(source);
		reads.push_back(source.Offset);
		if (source.Offset == 0) {
			Require(!current_queue->CanClose(), "actual worker must reject its owner-thread cleanup before inspecting common state");
			try {
				current_queue->Close();
			} catch (const std::runtime_error&) {
				self_rejected = true;
			}

			read_complete.set_value();
			released.get();
		}

		return completion;
	});
	current_queue = &queue;
	const auto first = cache.Acquire("first");
	const auto future = cache.Prefetch("future");
	const auto tail = cache.Acquire("tail");
	const auto first_request = queue.QueuePrepared(first, "first", Region(filename, 0));
	static_cast<void>(queue.QueuePrepared(future, "future", Region(filename, 1)));
	static_cast<void>(queue.QueuePrepared(tail, "tail", Region(filename, 2)));
	queue.UpdatePreparedRequests();
	std::promise<void> barrier;
	auto complete = barrier.get_future();
	BackgroundCallTask after_read(background, BackgroundCallLane::BelowNormal, [&]() {
		barrier.set_value();
	});
	after_read.Update();
	background.PostLoop();
	current_read.get();
	Require(self_rejected && !queue.CancelPending(first_request) && queue.PendingCount() == 2, "selection must physically unlink only the current request while future nodes remain cancellable during its real read phase");
	Require(cache.Info(first).Completion == ResourceCompletion::Pending, "current successful bytes must stay unpublished until the real read phase returns and next selection occurs");
	cache.ClearCache();
	Require(queue.PendingCount() == 1, "ClearCache must remove an unreferenced future request while preserving selected current work");
	cache.Advance();
	Require(!cache.Contains(future) && cache.Contains(first), "cancelled future receiver may retire safely while selected receiver remains Loading");
	release.set_value();
	complete.get();
	queue.UpdatePreparedRequests();
	const std::array targets{first, tail};
	Require(!cache.ObserveLoadingTargets(targets) && reads == std::vector<std::uint64_t>{0, 2}, "actual selected and surviving future work must publish through the real target frontier");
	after_read.Close();
	queue.Close();
	background.Close();
}

static void TestCloseFencesRealReadAndDropsRequiredCaptureLast(const std::filesystem::path& filename) {
	ResourceCache cache(64);
	CommonProcessTasks processor;
	BackgroundCallManager background;
	std::promise<void> entered;
	auto running = entered.get_future();
	std::promise<void> release;
	auto released = release.get_future();
	std::promise<void> closing;
	auto close_started = closing.get_future();
	auto owner = std::make_shared<std::int32_t>(73);
	std::weak_ptr<std::int32_t> weak = owner;
	PersistentRawReadQueue queue(cache, processor, background, [owner, &entered, &released](auto& destination, const auto& source) {
		const auto completion = destination.ReadSelected(source);
		entered.set_value();
		released.get();
		Require(*owner == 73, "actual read backend ownership must survive the worker fence");

		return completion;
	});
	owner.reset();
	const auto first = cache.Acquire("selected");
	const auto future = cache.Acquire("future");
	static_cast<void>(queue.QueuePrepared(first, "selected", Region(filename, 0)));
	static_cast<void>(queue.QueuePrepared(future, "future", Region(filename, 1)));
	queue.UpdatePreparedRequests();
	background.PostLoop();
	running.get();
	std::thread gate([&]() {
		close_started.get();
		while (queue.PendingCount() != 0) {
			std::this_thread::yield();
		}
		Require(!weak.expired() && !queue.IsClosed(), "selected read callback capture must outlive actual queue cleanup and serial fence");
		release.set_value();
	});
	closing.set_value();
	queue.Close();
	gate.join();
	Require(queue.IsClosed() && weak.expired() && queue.PendingCount() == 0 && processor.Serial().Count() == 0, "forced authored close must cancel pending pointers and fence selected work before dropping the real read capture");
	cache.Advance();
	Require(cache.Info(first).Completion == ResourceCompletion::Succeeded && cache.Info(future).Completion == ResourceCompletion::Failed, "forced queued cancellation and actual selected success must retain distinct receiving outcomes");
	background.Close();
}

static void TestPreflightCapacityNamesAndCurrentCommonTraversal(const std::filesystem::path& filename) {
	ResourceCache cache(64);
	CommonProcessTasks processor;
	BackgroundCallManager background;
	sgre::io::RawReadQueueLimits limits;
	limits.MaxRequests = 1;
	PersistentRawReadQueue queue(cache, processor, background, limits);
	const auto a = cache.Acquire("a");
	const auto ar = queue.QueuePrepared(a, "a.m", Region(filename, 0));
	const auto b = cache.Acquire("b");
	RequireFailure([&]() {
		static_cast<void>(queue.QueuePrepared(b, "b", Region(filename, 1)));
	});
	Require(queue.PendingCount() == 1 && Order(queue) == std::vector<RawReadRequestHandle>{ar}, "capacity rejection must precede receiving cell/pointer publication");
	cache.Fail(b, "actual queue admission rejected before a read destination was assigned");
	Require(queue.CancelPending(ar), "actual admitted queued request must remain cancellable after failed future admission");
	const auto ambiguous = cache.Acquire("x.m.y");
	RequireFailure([&]() {
		static_cast<void>(queue.QueuePrepared(ambiguous, "x.m.y.m", Region(filename, 2)));
	});
	cache.Fail(ambiguous, "native first-occurrence .m key does not resolve to this Loading entry");
	sgre::scene::ParallelTaskCallbacks callbacks;
	callbacks.Update = [&](sgre::scene::ParallelTaskHandle) {
		RequireFailure([&]() {
			queue.Close();
		});
		Require(!queue.IsClosed(), "current actual common traversal must reject queue/root cleanup before detaching callbacks");
	};
	static_cast<void>(processor.Parallel().Append(std::move(callbacks)));
	queue.UpdatePreparedRequests();
	cache.Advance();
	queue.Close();
	processor.ClearParallel();
	background.Close();
}

static void TestPreparedReadAndFailurePipeline(const std::filesystem::path& filename) {
	ResourceCache cache(64);
	CommonProcessTasks processor;
	BackgroundCallManager background;
	std::vector<FileReadCompletion> actual_results;
	PersistentRawReadQueue queue(cache, processor, background, [&](auto& destination, const auto& source) {
		const auto result = destination.ReadSelected(source);
		actual_results.push_back(result);

		return result;
	});
	const auto failed = cache.Acquire("failed");
	const auto good = cache.Acquire("good");
	static_cast<void>(queue.QueuePrepared(failed, "failed", Region(filename.parent_path() / "missing-native-region.bin", 0)));
	static_cast<void>(queue.QueuePrepared(good, "good", Region(filename, 4)));
	queue.UpdatePreparedRequests();
	DrainReadJob(queue, background);
	const std::array targets{failed, good};
	Require(!cache.ObserveLoadingTargets(targets) && actual_results == std::vector<FileReadCompletion>{FileReadCompletion::ReadFailed, FileReadCompletion::Succeeded}, "real read failure2 must publish and continue the native persistent queue without a fake successful payload");
	Require(cache.Info(failed).Completion == ResourceCompletion::Failed && cache.Bytes(good)[0] == 55, "actual failed and successful destinations must follow their own map transitions");
	queue.Close();
	background.Close();
}

struct ReadCaptureReleaseProbe {
	std::function<void()> Release;
	~ReadCaptureReleaseProbe() noexcept {
		Release();
	}
};

static void TestRequiredCaptureCleanupReentry() {
	ResourceCache cache(0);
	CommonProcessTasks processor;
	BackgroundCallManager background;
	PersistentRawReadQueue* current = nullptr;
	std::int32_t released = 0;
	auto owner = std::make_shared<ReadCaptureReleaseProbe>();
	owner->Release = [&]() {
		Require(current->IsClosed() && cache.CanRetireReadDestinations(), "actual read capture destruction must follow both queue closure and cache detachment");
		current->Close();
		RequireFailure([&]() {
			current->ResortPending();
		});
		++released;
	};
	PersistentRawReadQueue queue(cache, processor, background, [owner](auto& destination, const auto& source) {
		Require(static_cast<bool>(owner), "actual read service has lost its owned backend capture");

		return destination.ReadSelected(source);
	});
	current = &queue;
	owner.reset();
	queue.Close();
	Require(released == 1, "actual required read capture must drop once after its last worker/callback access");
	background.Close();
}

static void TestAttachedManagerDestructorRejected() {
	std::set_terminate([]() {
		std::_Exit(0);
	});
	auto cache = std::make_unique<ResourceCache>(0);
	CommonProcessTasks processor;
	BackgroundCallManager background;
	PersistentRawReadQueue queue(*cache, processor, background);
	cache.reset();
}

static void TestThrownReadProviderRetirementRejected(const std::filesystem::path& filename) {
	ResourceCache cache(64);
	CommonProcessTasks processor;
	BackgroundCallManager background;
	PersistentRawReadQueue queue(cache, processor, background, [](auto& destination, const auto& source) -> FileReadCompletion {
		static_cast<void>(destination.ReadSelected(source));
		throw std::runtime_error("actual provider contract failed after read, before returning the real completion");
	});
	const auto handle = cache.Acquire("failed-provider");
	static_cast<void>(queue.QueuePrepared(handle, "failed-provider", Region(filename, 0)));
	queue.UpdatePreparedRequests();
	std::promise<void> barrier;
	auto done = barrier.get_future();
	BackgroundCallTask after(background, BackgroundCallLane::BelowNormal, [&]() {
		barrier.set_value();
	});
	after.Update();
	background.PostLoop();
	done.get();
	RequireFailure([&]() {
		queue.UpdatePreparedRequests();
	});
	Require(!queue.CanClose() && !queue.IsClosed() && cache.Info(handle).Completion == ResourceCompletion::Pending, "unsupported throwing producer must retain the actual owner and reject invented completion/retirement");
	after.Close();
	std::set_terminate([]() {
		std::_Exit(0);
	});
}

static void TestThrownReadProviderCloseRaceRejected(const std::filesystem::path& filename) {
	ResourceCache cache(64);
	CommonProcessTasks processor;
	BackgroundCallManager background;
	std::promise<void> entered;
	auto running = entered.get_future();
	std::promise<void> release;
	auto released = release.get_future();
	std::promise<void> closing;
	auto close_started = closing.get_future();
	auto owner = std::make_shared<std::int32_t>(91);
	std::weak_ptr<std::int32_t> weak = owner;
	PersistentRawReadQueue queue(cache, processor, background, [owner, &entered, &released](auto& destination, const auto& source) -> FileReadCompletion {
		static_cast<void>(destination.ReadSelected(source));
		entered.set_value();
		released.get();
		Require(*owner == 91, "raced Close must preserve the real backend owner through its running fence");
		throw std::runtime_error("actual provider failed inside raced serial Close");
	});
	owner.reset();
	const auto first = cache.Acquire("raced-selected");
	const auto future = cache.Prefetch("raced-future");
	static_cast<void>(queue.QueuePrepared(first, "raced-selected", Region(filename, 0)));
	static_cast<void>(queue.QueuePrepared(future, "raced-future", Region(filename, 1)));
	queue.UpdatePreparedRequests();
	background.PostLoop();
	running.get();
	Require(queue.CanClose() && queue.PendingCount() == 1, "blocked real provider must admit Close before its failure and leave a future node for the structural barrier");
	std::thread gate([&]() {
		close_started.get();
		while (queue.PendingCount() != 0) {
			std::this_thread::yield();
		}

		// Only Close removes this node, so the provider cannot fail until Close has passed preflight.
		release.set_value();
	});
	closing.set_value();
	std::exception_ptr failure;
	try {
		queue.Close();
	} catch (...) {
		failure = std::current_exception();
	}

	gate.join();
	Require(static_cast<bool>(failure), "raced Close must report the real provider failure");
	try {
		std::rethrow_exception(failure);
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()) == "actual provider failed inside raced serial Close", "raced Close must preserve the original provider diagnostic");
	}

	Require(!queue.IsClosed() && !queue.CanClose() && !weak.expired(), "raced provider failure must retain the Read capture and reject retirement after the serial fence");
	Require(queue.PendingCount() == 0 && processor.Serial().Count() == 0, "raced Close must retain owner failure despite completed queued cancellation and physical serial retirement");
	Require(!cache.CanRetireReadDestinations() && cache.Info(first).Completion == ResourceCompletion::Pending, "failed selected receiver must keep its actual cache owner and unpublished completion");
	RequireFailure([&]() {
		static_cast<void>(cache.Acquire("raced-future"));
	});
	Require(cache.Info(future).References == 1, "retained actual cache attachment must route the zero-to-positive Loading crossing to the failed queue");
	RequireFailure([&]() {
		queue.Close();
	});
	std::set_terminate([]() {
		std::_Exit(0);
	});
}

int main(int argc, char** argv) {
	if (argc == 2 && std::string_view(argv[1]) == "--attached-manager-destruction") {
		TestAttachedManagerDestructorRejected();

		return 1;
	}

	try {
		if (argc < 2 || argc > 3) {
			throw std::runtime_error("persistent raw queue fixture requires an ignored fixture directory and optional death mode");
		}

		const std::filesystem::path directory(argv[1]);
		std::filesystem::create_directories(directory);
		const auto filename = directory / "persistent-raw-queue-fixture.bin";
		{
			std::ofstream output(filename, std::ios::binary | std::ios::trunc);
			const std::array<char, 6> bytes{11, 22, 33, 44, 55, 66};
			output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
			Require(static_cast<bool>(output), "actual raw queue synthetic file write failed");
		}

		if (argc == 3 && std::string_view(argv[2]) == "--throwing-provider-destruction") {
			TestThrownReadProviderRetirementRejected(filename);

			return 1;
		}

		if (argc == 3 && std::string_view(argv[2]) == "--throwing-provider-close-race") {
			TestThrownReadProviderCloseRaceRejected(filename);

			return 1;
		}

		TestActualResortAndFrameSerialDispatch(filename);
		TestPhysicalQueuedCancellationAndClearCache(filename);
		TestSelectedBoundaryAndFutureRemoval(filename);
		TestCloseFencesRealReadAndDropsRequiredCaptureLast(filename);
		TestPreflightCapacityNamesAndCurrentCommonTraversal(filename);
		TestPreparedReadAndFailurePipeline(filename);
		TestRequiredCaptureCleanupReentry();
		std::filesystem::remove(filename);
		std::cout << "seven independent actual persistent raw request queue groups pass\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
