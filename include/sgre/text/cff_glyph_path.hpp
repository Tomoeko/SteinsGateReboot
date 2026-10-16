#pragma once

#include "sgre/text/cff_glyph_metrics.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace sgre::text {

	struct CffGlyphPathPoint {
		double X = 0;
		double Y = 0;
	};

	enum class CffGlyphPathCommandKind {
		MoveTo,
		LineTo,
		CubicTo,
		QuadraticTo,
		Close
	};

	struct CffGlyphPathCommand {
		CffGlyphPathCommandKind Kind = CffGlyphPathCommandKind::MoveTo;
		// Move/line use one point, quadratic two, cubic three, and close none.
		std::array<CffGlyphPathPoint, 3> Points{};
	};

	struct CffGlyphPathBounds {
		double Left = 0;
		double Top = 0;
		double Right = 0;
		double Bottom = 0;
	};

	struct CffGlyphPath {
		std::vector<CffGlyphPathCommand> Commands;
		CffGlyphPathBounds Bounds;
	};

	struct CffGlyphBitmapPlan {
		std::int32_t LeftPixel = 0;
		std::int32_t TopPixel = 0;
		std::int32_t WidthPixels = 0;
		std::int32_t HeightPixels = 0;
		CffGlyphPathPoint PathTranslationPixels;
	};

	// Decomposes prepared cubic outlines into downward-positive pixel paths and native curve extrema.
	// Glyph loading/hinting and bitmap rasterization remain separate; source point storage is borrowed.
	[[nodiscard]] CffGlyphPath PrepareCffGlyphPath(const CffScaledGlyph& glyph);
	// Borrows only prepared geometry; no glyph metrics or font owner is required.
	[[nodiscard]] CffGlyphPath PrepareCffGlyphPath(std::span<const CffScaledPoint> points, std::span<const std::uint16_t> contour_ends);
	// Native floor/ceil bounds are checked for empty extents before border padding is added.
	// This plans CPU bitmap storage and translation; it does not allocate an atlas or rasterize pixels.
	[[nodiscard]] std::optional<CffGlyphBitmapPlan> PlanCffGlyphBitmap(const CffGlyphPathBounds& bounds, std::int32_t border_width_pixels = 0);

} // namespace sgre::text
