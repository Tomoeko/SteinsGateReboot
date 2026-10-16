#pragma once

#include "sgre/text/layout.hpp"

#include <cstdint>

namespace sgre::text {

	struct VectorFontScale {
		std::int32_t Scale16Dot16 = 0;
	};

	// Native 72-dpi sizing for a positive integral pixel size and the font's units per em.
	[[nodiscard]] VectorFontScale PrepareVectorFontScale(std::int32_t pixel_size, std::uint16_t units_per_em);
	// Reproduces the unhinted fast-advance callback before shaping adjusts marks or positions.
	// Vertical advance is negative in the native shaping coordinate system.
	[[nodiscard]] std::int32_t ScaleVectorGlyphAdvance26Dot6(const VectorFontScale& scale, std::uint16_t advance_units, TextDirection direction);

} // namespace sgre::text
