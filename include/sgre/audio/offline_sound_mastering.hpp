#pragma once

#include "sgre/audio/buffered_sound_cues.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace sgre::audio {
	struct OfflineSoundMasteringDescription {
		// Explicit caller-known format; native default mastering/device format is not inferred here.
		std::uint32_t SamplesPerSecond = 0;
		std::uint8_t Channels = 0;
		std::uint32_t MaximumFrames = 1024;
	};

	class OfflineSoundMastering {
	public:
		// Actual cue/source/configuration owners enclose this serialized stable batch through Close.
		// Selected cue IDs/slots stay allocated throughout the batch; Close precedes cue/source retirement.
		// The real system engine operates offline, before any device node access. No audio device or script readiness.
		OfflineSoundMastering(BufferedSoundCues& actual_sources, std::span<const BufferedSoundCueId> actual_cues, OfflineSoundMasteringDescription description);
		~OfflineSoundMastering();
		OfflineSoundMastering(const OfflineSoundMastering&) = delete;
		OfflineSoundMastering& operator=(const OfflineSoundMastering&) = delete;
		OfflineSoundMastering(OfflineSoundMastering&&) = delete;
		OfflineSoundMastering& operator=(OfflineSoundMastering&&) = delete;
		// Caller-owned interleaved float output; one actual system render, with no retries or decoding.
		// MaximumFrames must also fit the actual cue owner's configured consume bound.
		// Foreign/C++ operation failures retain the first error and real partial consumption; Close still releases the graph.
		[[nodiscard]] std::size_t Render(std::span<float> output);
		[[nodiscard]] OfflineSoundMasteringDescription Description() const;
		[[nodiscard]] std::uint64_t RenderedFrames() const;
		// Close and destruction stay on the preparation/consumption owner thread.
		void Close() noexcept;

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};
} // namespace sgre::audio
