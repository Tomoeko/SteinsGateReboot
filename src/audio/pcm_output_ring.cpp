#include "sgre/audio/pcm_output_ring.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace sgre::audio {
	static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
	static_assert(std::atomic<bool>::is_always_lock_free);

	PcmOutputRing::PcmOutputRing(PcmOutputRingDescription description) : m_Description(description) {
		if (description.Channels == 0 || description.Channels > 8 || description.CapacityFrames == 0 || description.CapacityFrames > 262144 || description.MaximumTransferFrames == 0 || description.MaximumTransferFrames > description.CapacityFrames || description.CapacityFrames > std::numeric_limits<std::size_t>::max() / description.Channels) {
			throw std::runtime_error("PCM output ring requires bounded channel/frame geometry");
		}
		m_Samples.resize(description.CapacityFrames * description.Channels);
	}

	PcmOutputRingDescription PcmOutputRing::Description() const noexcept {
		return m_Description;
	}

	std::size_t PcmOutputRing::BufferedFrames() const noexcept {
		const auto write = m_WriteFrame.load(std::memory_order_relaxed);
		const auto read = m_ReadFrame.load(std::memory_order_acquire);
		return static_cast<std::size_t>(write - read);
	}

	std::size_t PcmOutputRing::WritableFrames() const noexcept {
		return m_Description.CapacityFrames - BufferedFrames();
	}

	PcmOutputRingStatus PcmOutputRing::Write(std::span<const float> interleaved) noexcept {
		const auto channels = m_Description.Channels;
		if (interleaved.size() % channels != 0 || interleaved.size() / channels > m_Description.MaximumTransferFrames) {
			return PcmOutputRingStatus::InvalidExtent;
		}
		if (m_Finished.load(std::memory_order_relaxed)) {
			return PcmOutputRingStatus::Finished;
		}
		const auto frames = interleaved.size() / channels;
		const auto write = m_WriteFrame.load(std::memory_order_relaxed);
		const auto read = m_ReadFrame.load(std::memory_order_acquire);
		if (frames > m_Description.CapacityFrames - static_cast<std::size_t>(write - read)) {
			return PcmOutputRingStatus::Starved;
		}
		if (frames > std::numeric_limits<std::uint64_t>::max() - write) {
			return PcmOutputRingStatus::CounterOverflow;
		}
		for (std::size_t frame = 0; frame < frames; ++frame) {
			const auto destination = static_cast<std::size_t>((write + frame) % m_Description.CapacityFrames) * channels;
			std::copy_n(interleaved.data() + frame * channels, channels, m_Samples.data() + destination);
		}
		m_WriteFrame.store(write + frames, std::memory_order_release);
		return PcmOutputRingStatus::Ready;
	}

	void PcmOutputRing::Finish() noexcept {
		m_Finished.store(true, std::memory_order_release);
	}

	PcmOutputRingRead PcmOutputRing::Read(std::span<float> interleaved) noexcept {
		PcmOutputRingRead result;
		const auto channels = m_Description.Channels;
		if (interleaved.size() % channels != 0 || interleaved.size() / channels > m_Description.MaximumTransferFrames) {
			result.Status = PcmOutputRingStatus::InvalidExtent;
			return result;
		}
		const auto frames = interleaved.size() / channels;
		const auto read = m_ReadFrame.load(std::memory_order_relaxed);
		const auto finished = m_Finished.load(std::memory_order_acquire);
		const auto write = m_WriteFrame.load(std::memory_order_acquire);
		const auto copied = std::min(frames, static_cast<std::size_t>(write - read));
		for (std::size_t frame = 0; frame < copied; ++frame) {
			const auto source = static_cast<std::size_t>((read + frame) % m_Description.CapacityFrames) * channels;
			std::copy_n(m_Samples.data() + source, channels, interleaved.data() + frame * channels);
		}
		std::fill(interleaved.begin() + static_cast<std::ptrdiff_t>(copied * channels), interleaved.end(), 0.0f);
		m_ReadFrame.store(read + copied, std::memory_order_release);
		result.CopiedFrames = copied;
		result.SilentFrames = frames - copied;
		if (copied != frames) {
			result.Status = finished ? PcmOutputRingStatus::Drained : PcmOutputRingStatus::Starved;
		}
		return result;
	}
} // namespace sgre::audio
