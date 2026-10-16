#include "sgre/text/vector_metrics.hpp"

#include <limits>
#include <stdexcept>

namespace sgre::text {

	VectorFontScale PrepareVectorFontScale(std::int32_t pixel_size, std::uint16_t units_per_em) {
		if (pixel_size <= 0 || units_per_em < 16 || units_per_em > 16384) {
			throw std::runtime_error("vector font scale requires a positive pixel size and valid units per em");
		}

		const auto character_size = std::int64_t(pixel_size) * 64;
		if (character_size > std::numeric_limits<std::int32_t>::max()) {
			throw std::runtime_error("vector font character size exceeds its signed 26.6 range");
		}

		const auto scale = (character_size * 65536 + units_per_em / 2) / units_per_em;
		if (scale > std::numeric_limits<std::int32_t>::max()) {
			throw std::runtime_error("vector font scale exceeds its signed 16.16 range");
		}

		VectorFontScale result;
		result.Scale16Dot16 = static_cast<std::int32_t>(scale);

		return result;
	}

	std::int32_t ScaleVectorGlyphAdvance26Dot6(const VectorFontScale& scale, std::uint16_t advance_units, TextDirection direction) {
		if (scale.Scale16Dot16 <= 0 || (direction != TextDirection::Horizontal && direction != TextDirection::Vertical)) {
			throw std::runtime_error("vector glyph advance scale or direction is invalid");
		}

		// The original fast path rounds into 16.16 first, then the shaping callback rounds to 26.6.
		const auto advance16 = (std::int64_t(advance_units) * scale.Scale16Dot16 + 32) / 64;
		if (advance16 > std::numeric_limits<std::int32_t>::max() - 512) {
			throw std::runtime_error("vector glyph advance exceeds its signed 16.16 rounding range");
		}

		const auto rounded = direction == TextDirection::Horizontal ? (advance16 + 512) >> 10 : (512 - advance16) >> 10;

		return static_cast<std::int32_t>(rounded);
	}

} // namespace sgre::text
