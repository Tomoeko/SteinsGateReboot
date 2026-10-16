#pragma once

#include "sgre/audio/wma_coefficient.hpp"
#include "sgre/audio/wma_stream.hpp"

#include <cstdint>
#include <vector>

namespace sgre::audio {

	struct WmaBlockHistory {
		std::uint32_t PreviousCoefficients = 0;
		std::uint32_t CurrentCoefficients = 0;
	};

	[[nodiscard]] WmaBlockHistory ReadWmaBlockHistory(WmaBitReader& reader, const WmaStandardParameters& parameters);
	[[nodiscard]] std::uint32_t ReadWmaNextBlockSize(WmaBitReader& reader, const WmaStandardParameters& parameters);

	struct WmaPlacedCoefficient {
		std::uint32_t Position = 0;
		WmaCoefficient Value;
	};

	// Sparse values preserve explicit signs, including zero. Skipped positions are implicit zero coefficients.
	[[nodiscard]] std::vector<WmaPlacedCoefficient> DecodeWmaCoefficientValues(WmaBitReader& reader, const WmaCoefficientDecoder& decoder, WmaCoefficientEscapeWidths escape_widths, std::uint32_t coefficient_count);

} // namespace sgre::audio
