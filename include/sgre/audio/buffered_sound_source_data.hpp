#pragma once

#include "sgre/audio/sound_playback.hpp"
#include "sgre/audio/voice_schedule.hpp"
#include "sgre/io/wrapped_psb_destination.hpp"
#include "sgre/resource/sound_definitions.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace sgre::audio {

	struct BufferedSoundSourceData {
		SoundVoiceType Pool = SoundVoiceType::Buffered32;
		resource::SoundChannel Description;
		SoundSubmission Submission;
		std::span<const std::uint8_t> FormatBytes;
		std::span<const std::uint8_t> EncodedBytes;
		std::span<const std::uint8_t> DecodedPacketBytes;
	};

	// Native3921a0 borrows fmt/data/dpds through the channel's actual PSB owner. Only preparation reads these bytes.
	// The archive file, definitions and acquired resource reference enclose every returned span and future source/callback use.
	// This prepares data only; it creates no voice, emits no audio and asserts no audio-service or playback readiness.
	[[nodiscard]] BufferedSoundSourceData ReadBufferedSoundSourceData(const io::PublishedPsbFile& archive_file, const resource::SoundArchiveVoiceDefinition& voice, std::size_t channel_index, std::int32_t requested_play_count);

} // namespace sgre::audio
