#pragma once

#include "sgre/resource/psb.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace sgre::resource {

	enum class WaveEncoding : std::uint16_t {
		Pcm = 1,
		MicrosoftAdpcm = 2,
		WmaStandard = 0x161,
		WmaProfessional = 0x162
	};

	struct WaveFormat {
		WaveEncoding Encoding = WaveEncoding::Pcm;
		std::uint16_t Channels = 0;
		std::uint32_t SamplesPerSecond = 0;
		std::uint32_t AverageBytesPerSecond = 0;
		std::uint16_t BlockAlignment = 0;
		std::uint16_t BitsPerSample = 0;
		std::vector<std::uint8_t> Extra;
	};

	struct SampleLoop {
		std::uint32_t Begin = 0;
		std::uint32_t Length = 0;
	};

	struct SoundChannel {
		WaveFormat Format;
		PsbChunkRange EncodedData;
		std::vector<std::uint32_t> DecodedPacketCumulativeBytes;
		std::optional<SampleLoop> Loop;
		std::array<float, 8> Pan = {1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
		bool Background = false;
		bool DisableSampleRateConversion = false;
	};

	struct SoundEntry {
		std::string Name;
		std::int32_t DefaultLoopCount = 0;
		std::int32_t Device = 0;
		std::int32_t Quality = 0;
		std::int32_t Type = 0;
		float Volume = 1.0f;
		float PitchOctaves = 0.0f;
		float Priority = 0.0f;
		std::string Group;
		std::string Uniqueness;
		std::vector<SoundChannel> Channels;
	};

	struct SoundArchive {
		std::vector<SoundEntry> Entries;
	};

	[[nodiscard]] WaveFormat ReadWaveFormat(std::span<const std::uint8_t> bytes);
	// EncodedData ranges borrow the caller's PSB storage; all interpreted metadata is owned.
	[[nodiscard]] SoundArchive ReadSoundArchive(const PsbDocument& document, std::span<const std::uint8_t> psb_bytes);

} // namespace sgre::resource
