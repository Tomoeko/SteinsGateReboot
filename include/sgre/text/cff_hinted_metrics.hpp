#pragma once

#include "sgre/text/cff_contour_outline.hpp"

#include <cstdint>

namespace sgre::text {

	struct CffHintedGlyphMetrics {
		std::int32_t Width26Dot6 = 0;
		std::int32_t Height26Dot6 = 0;
		std::int32_t HorizontalBearingX26Dot6 = 0;
		std::int32_t HorizontalBearingY26Dot6 = 0;
		std::int32_t HorizontalAdvance26Dot6 = 0;
		std::int32_t VerticalBearingX26Dot6 = 0;
		// The native driver treats the original unscaled vmtx bearing as 26.6 before pixel rounding.
		std::int32_t VerticalBearingY26Dot6 = 0;
		std::int32_t VerticalAdvance26Dot6 = 0;
		std::int32_t LinearHorizontalAdvance16Dot16 = 0;
		std::int32_t LinearVerticalAdvance16Dot16 = 0;
		std::int32_t AdvanceX26Dot6 = 0;
		std::int32_t AdvanceY26Dot6 = 0;
		std::uint32_t OutlineFlags = 0;
	};

	// Measures normalized, already hinted native 26.6 points under the original horizontal flag-8 load.
	// Requires real hmtx/vmtx metrics; font transforms, missing vertical tables and rasterization are separate.
	[[nodiscard]] CffHintedGlyphMetrics MeasureCffHintedGlyph(const CffContourOutline& outline, VectorFontScale horizontal_scale, VectorFontScale vertical_scale, const CffGlyphFontMetrics& font_metrics, std::uint16_t vertical_pixels_per_em);

} // namespace sgre::text
