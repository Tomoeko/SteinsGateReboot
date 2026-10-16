#include "sgre/core/thread_task_frame.hpp"

#include "sgre/io/file_reader.hpp"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <span>
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

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view text) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(text) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("unsupported thread frame operation was accepted");
}

static void TestConcreteOrdering() {
	const std::array<std::uint64_t, 12> samples = {100, 180, 200, 216, 300, 340, 500, 516, 600, 616, 700, 716};
	std::size_t sample = 0;
	std::array<sgre::core::ThreadTaskReceiver*, 6> dispatched_receivers{};
	sgre::core::ThreadTaskFrame frame([&] {
		Require(sample < samples.size(), "worker sampled outside actual counter inputs");
		const auto actual = dispatched_receivers[sample / 2]->State();
		Require(actual.Executing && actual.UpdatePending == (sample % 2 == 0), "first counter sample precedes native pending-clear thunk and second follows actual update");

		return samples[sample++];
	},
	    1, 8);
	std::vector<std::uint32_t> order;
	order.reserve(12);
	std::array<std::unique_ptr<sgre::core::ThreadTaskReceiver>, 3> receivers;
	for (std::size_t index = 0; index < receivers.size(); ++index) {
		receivers[index] = std::make_unique<sgre::core::ThreadTaskReceiver>(frame, [&, index](auto& receiver) {
			Require(!receiver.State().UpdatePending && receiver.State().Executing, "native pending update clears before actual callback");
			order.push_back(static_cast<std::uint32_t>(index));
			receiver.QueuePost();
		},
		    [&, index](auto& receiver) {
			    Require(!receiver.State().PostPending && receiver.State().Executing, "native post pending flag clears before actual callback");
			    Require(!receiver.CanClose(), "active post callback must preserve its actual borrowed owner");
			    order.push_back(static_cast<std::uint32_t>(index + 10));
		    });
		receivers[index]->QueueUpdate();
	}
	dispatched_receivers = {receivers[0].get(), receivers[1].get(), receivers[2].get(), receivers[0].get(), receivers[2].get(), receivers[1].get()};
	Require(!frame.CanClose() && !receivers[0]->CanClose(), "pending physical work must reject retirement");
	RequireFailure([&] {
		receivers[0]->Close();
	},
	    "finish actual queued/running phases");
	frame.RunFrame();
	Require(order == std::vector<std::uint32_t>({0, 1, 2, 10, 11, 12}), "equal native costs retain queued order and posts follow all updates");
	Require(receivers[0]->State().Cost == 10 && receivers[1]->State().Cost == 2 && receivers[2]->State().Cost == 5, "original raw counter differences must produce actual DWORD cost estimates");
	order.clear();
	receivers[1]->QueueUpdate();
	receivers[0]->QueueUpdate();
	receivers[2]->QueueUpdate();
	frame.RunFrame();
	Require(order == std::vector<std::uint32_t>({0, 2, 1, 10, 12, 11}), "cost sort must run before real workers dequeue and post order must retain callback insertion");
	for (auto& receiver : receivers) {
		receiver->Close();
	}
	frame.Close();
}

static void TestWorkerFence() {
	std::atomic<std::uint64_t> counter = 0;
	sgre::core::ThreadTaskFrame frame([&] {
		return counter.fetch_add(8);
	},
	    2, 4);
	std::mutex mutex;
	std::condition_variable started;
	std::condition_variable release;
	std::size_t waiting = 0;
	bool released = false;
	std::atomic<std::uint32_t> updates = 0;
	std::atomic<std::uint32_t> posts = 0;
	auto actual_update = [&](auto& receiver) {
		std::unique_lock lock(mutex);
		++waiting;
		started.notify_one();
		release.wait(lock, [&] {
			return released;
		});
		lock.unlock();
		++updates;
		receiver.QueuePost();
	};
	auto actual_post = [&](auto&) {
		Require(updates == 2, "post delivery cannot precede the complete actual worker fence");
		++posts;
	};
	sgre::core::ThreadTaskReceiver first(frame, actual_update, actual_post);
	sgre::core::ThreadTaskReceiver second(frame, actual_update, actual_post);
	first.QueueUpdate();
	second.QueueUpdate();
	std::thread observer([&] {
		std::unique_lock lock(mutex);
		started.wait(lock, [&] {
			return waiting == 2;
		});
		Require(posts == 0 && !first.CanClose() && !frame.CanClose(), "selected workers retain real receiver owners before post service");
		released = true;
		lock.unlock();
		release.notify_all();
	});
	frame.RunFrame();
	observer.join();
	Require(posts == 2, "complete fence must deliver both actual pending posts");
	first.Close();
	second.Close();
	frame.Close();
}

static void TestGuardsAndLivePosts() {
	std::atomic<std::uint64_t> counter = 0;
	sgre::core::ThreadTaskFrame frame([&] {
		return counter.fetch_add(8);
	},
	    1, 3);
	std::size_t updates = 0;
	std::size_t posts = 0;
	sgre::core::ThreadTaskReceiver receiver(frame, [&](auto&) {
		++updates;
	},
	    [&](auto& actual) {
		    ++posts;
		    Require(!actual.State().PostPending, "duplicate native post rows still clear the pending byte before each call");
		    RequireFailure([&] {
			    frame.RunFrame();
		    },
		        "cannot reenter");
		    RequireFailure([&] {
			    actual.QueueUpdate();
		    },
		        "idle bounded frame");
		    if (posts == 1) {
			    actual.QueuePost();
		    }
	    });
	receiver.QueuePost();
	receiver.QueuePost();
	frame.RunFrame();
	Require(posts == 3, "actual live post list must visit a bounded appended callback without a snapshot substitute");
	receiver.QueueUpdate();
	RequireFailure([&] {
		receiver.QueueUpdate();
	},
	    "live unqueued receiver");
	frame.RunFrame();
	Require(updates == 1, "rejected duplicate work must leave one actual queued update");
	receiver.QueuePost();
	receiver.QueuePost();
	receiver.QueuePost();
	RequireFailure([&] {
		receiver.QueuePost();
	},
	    "bounded pending storage");
	frame.RunFrame();
	receiver.Close();
	frame.Close();
	RequireFailure([&] {
		receiver.QueueUpdate();
	},
	    "live serialized owner thread");
}

static void TestFailuresAndRequiredOwners() {
	RequireFailure([] {
		sgre::core::ThreadTaskFrame missing({}, 1);
	},
	    "actual counter source");
	std::atomic<std::uint64_t> counter = 0;
	sgre::core::ThreadTaskFrame frame([&] {
		return counter.fetch_add(8);
	},
	    1, 4);
	RequireFailure([&] {
		sgre::core::ThreadTaskReceiver missing(frame, {}, [](auto&) {
			throw std::runtime_error("unreachable incomplete receiver");
		});
	},
	    "both actual update and post operations");
	std::size_t posts = 0;
	sgre::core::ThreadTaskReceiver failure(frame, [&](auto& actual) {
		actual.QueuePost();
		throw std::runtime_error("actual worker failure");
	},
	    [&](auto&) {
		    ++posts;
	    });
	failure.QueueUpdate();
	RequireFailure([&] {
		frame.RunFrame();
	},
	    "actual worker failure");
	Require(posts == 0 && failure.State().PostPending && !failure.State().Executing && !failure.State().UpdatePending && !failure.CanClose(), "worker failure must fence execution and retain real pending posts without replaying update");
	frame.RunFrame();
	Require(posts == 1 && failure.CanClose(), "retry may drain only remaining actual post ownership");
	failure.Close();
	frame.Close();
}

static void TestContainingAdmission() {
	std::atomic<std::uint64_t> counter = 0;
	std::atomic<std::uint32_t> containing_mutations = 0;
	sgre::core::ThreadTaskFrame frame([&] {
		return counter.fetch_add(8);
	},
	    2, 4);
	auto actual_containing_entry = [&] {
		frame.RequireCanRunFrame();
		++containing_mutations;
	};
	std::size_t posts = 0;
	sgre::core::ThreadTaskReceiver receiver(frame, [&](auto& actual) {
		RequireFailure(actual_containing_entry, "live serialized owner thread");
		actual.QueuePost();
	},
	    [&](auto&) {
		    ++posts;
		    RequireFailure(actual_containing_entry, "cannot reenter worker or post delivery");
	    });
	receiver.QueueUpdate();
	const auto before = receiver.State();
	frame.RequireCanRunFrame();
	const auto after = receiver.State();
	Require(before.Cost == after.Cost && before.UpdatePending == after.UpdatePending && before.PostPending == after.PostPending && before.Executing == after.Executing && counter == 0, "owner preflight must not consume queued work or sample its counter");
	std::exception_ptr foreign_failure;
	std::thread foreign([&] {
		try {
			RequireFailure(actual_containing_entry, "live serialized owner thread");
			RequireFailure([&] {
				frame.RunFrame();
			},
			    "live serialized owner thread");
		} catch (...) {
			foreign_failure = std::current_exception();
		}
	});
	foreign.join();
	if (foreign_failure) {
		std::rethrow_exception(foreign_failure);
	}
	frame.RunFrame();
	Require(posts == 1 && containing_mutations == 0, "foreign and actual non-Idle preflight rejection must precede containing mutation");
	actual_containing_entry();
	Require(containing_mutations == 1, "the exact live owner may enter only after complete worker/post delivery");
	receiver.Close();
	frame.Close();
	RequireFailure(actual_containing_entry, "live serialized owner thread");
	RequireFailure([&] {
		frame.RunFrame();
	},
	    "live serialized owner thread");
	Require(containing_mutations == 1, "closed-owner preflight rejection must leave containing mutation unchanged");
}

static void TestExactCostWords() {
	Require(sgre::core::UpdateThreadTaskCost(0, 100, 116) == 2, "raw counter samples must retain their original units");
	Require(sgre::core::UpdateThreadTaskCost(0, 0xfffffff0u, 0x10u) == 4, "counter difference must wrap before unsigned shift");
	Require(sgre::core::UpdateThreadTaskCost(0xffffffffu, 0, 0) == 0x1fffffffu, "multiplication wraps before logical right shift");
	Require(sgre::core::UpdateThreadTaskCost(0, 1, 0) == 0x1fffffffu, "backward samples preserve DWORD arithmetic rather than signed division");
}

static void TestNativeCostFixture(const char* path) {
	sgre::io::FileReader file(path);
	Require(file.Size() >= 8 && file.Size() <= 64 * 1024, "native worker fixture exceeds its compact storage bound");
	const auto bytes = file.Read(0, static_cast<std::size_t>(file.Size()));
	std::size_t offset = 0;
	auto read = [&]() {
		Require(offset <= bytes.size() && 4 <= bytes.size() - offset, "short native worker word");
		std::uint32_t word = 0;
		for (std::size_t index = 0; index < 4; ++index) {
			word |= static_cast<std::uint32_t>(bytes[offset++]) << (index * 8);
		}

		return word;
	};
	Require(read() == 0x31434654u, "native worker fixture magic must be TFC1");
	const auto count = read();
	Require(count > 0 && count <= 4096 && bytes.size() == 8 + static_cast<std::size_t>(count) * 16, "native worker count or exact length exceeds its bounded domain");
	for (std::size_t index = 0; index < count; ++index) {
		const auto previous = read();
		const auto before = read();
		const auto after = read();
		const auto expected = read();
		Require(sgre::core::UpdateThreadTaskCost(previous, before, after) == expected, "original worker cost word differs");
	}
	std::cout << count << " original worker-cost records match\n";
}

int main(int argc, char** argv) {
	try {
		Require(argc == 1 || argc == 2, "usage: thread_task_frame_tests [native-worker-cost.bin]");
		TestConcreteOrdering();
		TestWorkerFence();
		TestGuardsAndLivePosts();
		TestFailuresAndRequiredOwners();
		TestContainingAdmission();
		TestExactCostWords();
		if (argc == 2) {
			TestNativeCostFixture(argv[1]);
		}
		std::cout << "Six concrete thread-frame owner groups PASS\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
