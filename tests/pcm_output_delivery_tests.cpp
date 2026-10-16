#include "sgre/audio/pcm_output_delivery.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <type_traits>

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

template <typename Operation>
static void RequireThrows(Operation operation, const char* message) {
	bool threw = false;
	try {
		operation();
	} catch (const std::runtime_error&) {
		threw = true;
	}
	Require(threw, message);
}

static sgre::audio::PcmOutputDelivery Record(std::uint64_t sequence) {
	sgre::audio::PcmOutputDelivery record;
	record.Sequence = sequence;
	record.FirstMasterFrame = (sequence - 1) * 3;
	record.RequestedFrames = 5;
	record.CopiedFrames = 3;
	record.SilentFrames = 2;
	record.CallbackObserved.Value = sequence * 24;
	record.RenderTimestamp.HostTimeValid = true;
	record.RenderTimestamp.HostTime.Value = sequence + 1000000;
	record.RenderTimestamp.SampleTimeValid = true;
	record.RenderTimestamp.SampleFrameBits = sequence ^ 0x0123456789abcdefULL;

	return record;
}

static void TestExactRecordAndFullQueue() {
	sgre::audio::PcmOutputDeliveryQueue queue(2);
	auto output = Record(97);
	Require(!queue.TryRead(output) && output.Sequence == 97, "a submitted/produced clock cannot create a delivery record");
	Require(queue.Publish(Record(1)) == sgre::audio::PcmDeliveryPublishStatus::Ready && queue.Publish(Record(2)) == sgre::audio::PcmDeliveryPublishStatus::Ready, "two exact actual callback records fit");
	Require(queue.RequirePublishSpace() == sgre::audio::PcmDeliveryPublishStatus::Full && queue.Publish(Record(3)) == sgre::audio::PcmDeliveryPublishStatus::Full, "a full receipt queue cannot overwrite/lossily coalesce an actual event");
	Require(queue.TryRead(output) && output.Sequence == 1 && output.FirstMasterFrame == 0 && output.CallbackObserved.Value == 24 && output.RenderTimestamp.HostTime.Value == 1000001 && output.RenderTimestamp.SampleFrameBits == (1 ^ 0x0123456789abcdefULL), "different clock domains and exact immutable payload survive publication");
	Require(queue.Publish(Record(3)) == sgre::audio::PcmDeliveryPublishStatus::Ready && queue.TryRead(output) && output.Sequence == 2 && queue.TryRead(output) && output.Sequence == 3 && !queue.TryRead(output), "fixed-capacity wrap retains original event order and empty retirement");
}

static void TestInvalidRecordAndBounds() {
	RequireThrows([] {
		const sgre::audio::PcmOutputDeliveryQueue queue(0);
	},
	    "zero receipt capacity must reject");
	RequireThrows([] {
		const sgre::audio::PcmOutputDeliveryQueue queue(65537);
	},
	    "unbounded receipt allocation must reject");
	sgre::audio::PcmOutputDeliveryQueue queue(1);
	auto record = Record(1);
	record.CopiedFrames = 6;
	Require(queue.Publish(record) == sgre::audio::PcmDeliveryPublishStatus::InvalidRecord, "copied master frames cannot exceed the callback request");
	record = Record(1);
	record.SilentFrames = 1;
	Require(queue.Publish(record) == sgre::audio::PcmDeliveryPublishStatus::InvalidRecord, "silence must exactly cover the remaining callback extent");
	record = Record(1);
	record.FirstMasterFrame = std::numeric_limits<std::uint64_t>::max() - 1;
	Require(queue.Publish(record) == sgre::audio::PcmDeliveryPublishStatus::InvalidRecord, "master frame-end arithmetic must be checked before publication");
	record = Record(1);
	record.Sequence = 0;
	Require(queue.Publish(record) == sgre::audio::PcmDeliveryPublishStatus::InvalidRecord && queue.RequirePublishSpace() == sgre::audio::PcmDeliveryPublishStatus::Ready, "invalid records cannot consume receipt capacity");
}

static_assert(!std::is_invocable_v<decltype(&sgre::audio::PcmContinuousTime100ns), sgre::audio::PcmRenderAbsoluteClockTicks, std::int64_t>);

static void TestContinuousProjection() {
	struct Case {
		std::uint64_t Counter;
		std::int64_t Frequency;
		std::int64_t Expected;
	};
	const std::array<Case, 8> cases{{{0, 24000000, 0}, {24, 24000000, 10}, {24000000, 24000000, 10000000}, {24000001, 24000000, 10000000}, {1, 3, 3333333}, {2, 3, 6666666}, {3, 3, 10000000}, {10000, 1000000000, 100}}};
	for (const auto& item : cases) {
		Require(sgre::audio::PcmContinuousTime100ns({item.Counter}, item.Frequency) == item.Expected, "continuous projection must preserve floor rounding independently of absolute timestamp epoch");
	}
	RequireThrows([] {
		static_cast<void>(sgre::audio::PcmContinuousTime100ns({0}, 0));
	},
	    "invalid continuous frequency must reject");
	RequireThrows([] {
		static_cast<void>(sgre::audio::PcmContinuousTime100ns({0}, -1));
	},
	    "negative continuous frequency must reject");
	RequireThrows([] {
		static_cast<void>(sgre::audio::PcmContinuousTime100ns({std::numeric_limits<std::uint64_t>::max()}, 1));
	},
	    "unsigned counter outside actual host signed domain must reject");
	RequireThrows([] {
		static_cast<void>(sgre::audio::PcmContinuousTime100ns({static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())}, 1));
	},
	    "100ns projection overflow must reject");
}

class ProducerJoin {
public:
	ProducerJoin(std::thread& thread, std::atomic<bool>& cancel) : m_Thread(thread), m_Cancel(cancel) {
	}
	~ProducerJoin() noexcept {
		m_Cancel.store(true, std::memory_order_release);
		if (m_Thread.joinable()) {
			m_Thread.join();
		}
	}
	ProducerJoin(const ProducerJoin&) = delete;
	ProducerJoin& operator=(const ProducerJoin&) = delete;

private:
	std::thread& m_Thread;
	std::atomic<bool>& m_Cancel;
};

static void TestActualSpscPublication() {
	constexpr std::uint64_t kRecords = 10000;
	sgre::audio::PcmOutputDeliveryQueue queue(3);
	std::atomic<bool> cancel = false;
	std::atomic<bool> failed = false;
	std::thread producer([&] {
		for (std::uint64_t sequence = 1; sequence <= kRecords && !cancel.load(std::memory_order_acquire);) {
			const auto status = queue.Publish(Record(sequence));
			if (status == sgre::audio::PcmDeliveryPublishStatus::Ready) {
				++sequence;
			} else if (status == sgre::audio::PcmDeliveryPublishStatus::Full) {
				std::this_thread::yield();
			} else {
				failed.store(true, std::memory_order_release);
				break;
			}
		}
	});
	const ProducerJoin join(producer, cancel);
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	for (std::uint64_t sequence = 1; sequence <= kRecords;) {
		Require(!failed.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline, "bounded two-owner receipt transfer failed or stalled");
		sgre::audio::PcmOutputDelivery observed;
		if (!queue.TryRead(observed)) {
			std::this_thread::yield();
			continue;
		}
		Require(observed.Sequence == sequence && observed.FirstMasterFrame == (sequence - 1) * 3 && observed.CallbackObserved.Value == sequence * 24 && observed.RenderTimestamp.HostTime.Value == sequence + 1000000 && observed.RenderTimestamp.SampleFrameBits == (sequence ^ 0x0123456789abcdefULL), "acquire must see a complete ordered record through repeated queue wrap");
		++sequence;
	}
}

int main() {
	try {
		TestExactRecordAndFullQueue();
		TestInvalidRecordAndBounds();
		TestContinuousProjection();
		TestActualSpscPublication();
		std::cout << "four bounded PCM delivery owner groups passed; no device or source progress inferred\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
