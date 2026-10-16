#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace sgre::audio {
	struct PcmOutputRingDescription {
		std::size_t CapacityFrames = 8192;
		std::size_t MaximumTransferFrames = 4096;
		std::uint32_t Channels = 2;
	};

	enum class PcmOutputRingStatus : std::uint8_t {
		Ready,
		Starved,
		Drained,
		InvalidExtent,
		Finished,
		CounterOverflow
	};

	struct PcmOutputRingRead {
		PcmOutputRingStatus Status = PcmOutputRingStatus::Ready;
		std::size_t CopiedFrames = 0;
		std::size_t SilentFrames = 0;
	};

	// One producer owns Write/Finish; one serialized consumer owns Read. Storage survives both.
	// Construction allocates; all transfers are bounded, nonblocking and allocation-free.
	class PcmOutputRing {
	public:
		explicit PcmOutputRing(PcmOutputRingDescription description);
		PcmOutputRing(const PcmOutputRing&) = delete;
		PcmOutputRing& operator=(const PcmOutputRing&) = delete;
		[[nodiscard]] PcmOutputRingDescription Description() const noexcept;
		// Producer-only observations: the consumer can only increase the available space.
		[[nodiscard]] std::size_t BufferedFrames() const noexcept;
		[[nodiscard]] std::size_t WritableFrames() const noexcept;
		[[nodiscard]] PcmOutputRingStatus Write(std::span<const float> interleaved) noexcept;
		void Finish() noexcept;
		// Valid output is always filled, including an explicit zero tail on starvation/drain.
		[[nodiscard]] PcmOutputRingRead Read(std::span<float> interleaved) noexcept;

	private:
		PcmOutputRingDescription m_Description;
		std::vector<float> m_Samples;
		alignas(64) std::atomic<std::uint64_t> m_WriteFrame = 0;
		alignas(64) std::atomic<std::uint64_t> m_ReadFrame = 0;
		alignas(64) std::atomic<bool> m_Finished = false;
	};
} // namespace sgre::audio
