#pragma once

#include "sgre/resource/sound.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace sgre::audio {

	struct SoundSubmission {
		std::uint32_t EncodedBytes = 0;
		std::uint32_t LoopBegin = 0;
		std::uint32_t LoopLength = 0;
		std::uint32_t LoopRepeats = 0;
		std::uint32_t WmaPackets = 0;
		bool EndOfStream = true;
		bool DisableRateConversion = false;
	};

	// An explicit resource loop makes a zero requested count infinite, as in the original loader.
	[[nodiscard]] SoundSubmission PlanSoundSubmission(const resource::SoundChannel& channel, std::int32_t requested_play_count);

	struct SoundPan {
		std::array<float, 8> Destinations = {1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
	};

	struct SoundMixParameters {
		float VoiceVolume = 1.0f;
		float ResourceVolume = 1.0f;
		float GroupVolume = 1.0f;
		float ArchiveVolume = 1.0f;
		float MasterVolume = 1.0f;
		float MuteVolume = 1.0f;
		float VoicePitchOctaves = 0.0f;
		float ResourcePitchOctaves = 0.0f;
		SoundPan Pan;
		std::array<SoundPan, 8> SourcePan;
		std::array<float, 8> ResourcePan = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
		bool UseSourcePan = false;
		// MSound's ordinary update enables this; the lower-level backend also supports an individual mono row.
		bool FoldStereoPanForMono = true;

		SoundMixParameters();
	};

	struct SoundMix {
		std::uint8_t SourceChannels = 0;
		std::uint8_t DestinationChannels = 0;
		// The source index varies first: Destination * SourceChannels + Source.
		std::array<float, 64> Matrix = {};
		std::optional<float> FrequencyRatio;
	};

	[[nodiscard]] SoundMix EvaluateSoundMix(const SoundMixParameters& parameters, std::uint8_t source_channels, std::uint8_t destination_channels, bool disable_rate_conversion);
	// Buffers contain equal frame counts, are interleaved, and have distinct storage. Output is accumulated without clipping.
	void AccumulateSoundMix(const SoundMix& mix, std::span<const float> input, std::span<float> output);

} // namespace sgre::audio
