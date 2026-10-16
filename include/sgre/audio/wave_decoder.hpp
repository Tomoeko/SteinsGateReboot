#pragma once

#include "sgre/resource/sound.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace sgre::audio {

	struct DecodedAudio {
		std::uint32_t SamplesPerSecond = 0;
		std::uint16_t Channels = 0;
		// Interleaved native float amplitude; compressed decoding may exceed the nominal [-1, 1] range.
		std::vector<float> Samples;
	};

	[[nodiscard]] DecodedAudio DecodeWaveAudio(const resource::WaveFormat& format, std::span<const std::uint8_t> bytes, std::size_t max_decoded_bytes = 64 * 1024 * 1024);

} // namespace sgre::audio
