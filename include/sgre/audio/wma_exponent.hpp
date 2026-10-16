#pragma once

#include "sgre/audio/wma_packet.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace sgre::audio {

	// Includes the zero boundary and final coefficient count. This stage covers the supplied 44.1–48 kHz Standard profiles.
	[[nodiscard]] std::vector<std::uint32_t> WmaExponentBandEnds(std::uint32_t sample_rate, std::uint32_t block_coefficients);

	struct WmaExponentValues {
		std::vector<std::int32_t> Bands;
		std::int32_t MaximumExponent = 0;
	};

	// Version-two VLC exponents start at 36; an entire block commits only after every band decodes.
	[[nodiscard]] WmaExponentValues DecodeWmaExponentValues(WmaBitReader& reader, const WmaHuffmanDecoder& decoder, std::size_t band_count);

} // namespace sgre::audio
