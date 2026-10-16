#pragma once

#include "sgre/resource/cff_geometry.hpp"
#include "sgre/text/vector_metrics.hpp"

#include <cstdint>
#include <vector>

namespace sgre::text {

	struct CffScaledPoint {
		std::int32_t X26Dot6 = 0;
		std::int32_t Y26Dot6 = 0;
		resource::CffPointKind Kind = resource::CffPointKind::OnCurve;
	};

	struct CffGlyphFontMetrics {
		std::uint16_t HorizontalAdvance = 0;
		std::uint16_t VerticalAdvance = 0;
		std::int16_t VerticalSideBearing = 0;
	};

	struct CffGlyphMetrics {
		std::int32_t Width26Dot6 = 0;
		std::int32_t Height26Dot6 = 0;
		std::int32_t HorizontalBearingX26Dot6 = 0;
		std::int32_t HorizontalBearingY26Dot6 = 0;
		std::int32_t HorizontalAdvance26Dot6 = 0;
		std::int32_t VerticalBearingX26Dot6 = 0;
		// Native no-hint loading retains the font's unscaled vmtx side bearing here.
		std::int32_t VerticalSideBearingFontUnits = 0;
		std::int32_t VerticalAdvance26Dot6 = 0;
		std::int32_t VerticalOriginX26Dot6 = 0;
		std::int32_t VerticalOriginY26Dot6 = 0;
	};

	struct CffScaledGlyph {
		std::vector<CffScaledPoint> Points;
		std::vector<std::uint16_t> ContourEnds;
		CffGlyphMetrics Metrics;
	};

	// Reproduces the native no-hint point scaling/control-bounds phase with real vertical metrics.
	// Input points already include any font-matrix transform; hinting and rasterization are separate.
	[[nodiscard]] CffScaledGlyph ScaleCffGlyphOutline26Dot6(const resource::CffDesignOutline& outline, VectorFontScale horizontal_scale, VectorFontScale vertical_scale, const CffGlyphFontMetrics& font_metrics);

} // namespace sgre::text
