#pragma once

#include "sgre/audio/wave_decoder.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace sgre::audio {

	struct WmaDecodeLimits {
		std::size_t MaxPackets = 262144;
		std::size_t MaxDecodedBytes = 64 * 1024 * 1024;
	};

	// The recovered profile is explicit because the supplied xWMA headers have no codec extension.
	[[nodiscard]] DecodedAudio DecodeWmaStandardAudio(const resource::WaveFormat& format, std::span<const std::uint8_t> encoded, std::span<const std::uint32_t> decoded_packet_cumulative_bytes, std::uint16_t encode_options, WmaDecodeLimits limits = {});

} // namespace sgre::audio
