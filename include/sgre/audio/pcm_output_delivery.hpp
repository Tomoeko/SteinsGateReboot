#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace sgre::audio {
	struct PcmContinuousClockTicks {
		std::uint64_t Value = 0;
	};

	struct PcmRenderAbsoluteClockTicks {
		std::uint64_t Value = 0;
	};

	// Optional OS render representations. These are not the continuous callback-observation clock.
	struct PcmOutputRenderTimestamp {
		PcmRenderAbsoluteClockTicks HostTime;
		std::uint64_t SampleFrameBits = 0;
		std::uint64_t RateScalarBits = 0;
		bool HostTimeValid = false;
		bool SampleTimeValid = false;
		bool RateScalarValid = false;
	};

	struct PcmOutputDelivery {
		std::uint64_t Sequence = 0;
		// Copied master frames index the submitted PCM stream; underflow silence is excluded.
		std::uint64_t FirstMasterFrame = 0;
		std::uint32_t RequestedFrames = 0;
		std::uint32_t CopiedFrames = 0;
		std::uint32_t SilentFrames = 0;
		PcmContinuousClockTicks CallbackObserved;
		PcmOutputRenderTimestamp RenderTimestamp;
	};

	enum class PcmDeliveryPublishStatus {
		Ready,
		Full,
		InvalidRecord,
		CounterOverflow
	};

	// Fixed storage is allocated before callbacks. One callback producer and one serialized owner consumer
	// enclose its lifetime. Complete records pass release/acquire; no pointers or source owners cross it.
	class PcmOutputDeliveryQueue {
	public:
		explicit PcmOutputDeliveryQueue(std::size_t capacity);
		PcmOutputDeliveryQueue(const PcmOutputDeliveryQueue&) = delete;
		PcmOutputDeliveryQueue& operator=(const PcmOutputDeliveryQueue&) = delete;
		// Producer-only preflight. The sole consumer can only increase available space until Publish.
		[[nodiscard]] PcmDeliveryPublishStatus RequirePublishSpace() const noexcept;
		[[nodiscard]] PcmDeliveryPublishStatus Publish(const PcmOutputDelivery& record) noexcept;
		// Consumer-only. Empty leaves the caller's record unchanged.
		[[nodiscard]] bool TryRead(PcmOutputDelivery& record) noexcept;

	private:
		std::vector<PcmOutputDelivery> m_Records;
		alignas(64) std::atomic<std::uint64_t> m_Write = 0;
		alignas(64) std::atomic<std::uint64_t> m_Read = 0;
	};

	// Same floor projection as the actual host movie clock. This accepts continuous ticks only;
	// render-host absolute ticks need a separately admitted cross-clock calibration, not this conversion.
	[[nodiscard]] std::int64_t PcmContinuousTime100ns(PcmContinuousClockTicks counter, std::int64_t actual_frequency);
} // namespace sgre::audio
