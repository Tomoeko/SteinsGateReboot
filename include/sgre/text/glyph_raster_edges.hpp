#pragma once

#include "sgre/text/cff_glyph_path.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace sgre::text {

	struct GlyphRasterPoint {
		std::int32_t X24Dot8 = 0;
		std::int32_t Y24Dot8 = 0;
	};

	struct GlyphRasterEdge {
		std::uint32_t BandIndex = 0;
		bool ReverseWinding = false;
		// Native monotone chains use nondecreasing Y, retaining trailing horizontal and duplicate points.
		std::vector<GlyphRasterPoint> Points;
	};

	struct GlyphRasterClip {
		std::uint16_t WidthPixels = 0;
		std::uint16_t HeightPixels = 0;
		std::uint8_t BandHeightPixels = 64;
	};

	// Native automatic closure and monotone chains for finite interior polylines.
	// Outside clipping, curve flattening and coverage are separate and are never silently substituted.
	[[nodiscard]] std::vector<GlyphRasterEdge> BuildGlyphRasterPolylineEdges(std::span<const CffGlyphPathPoint> points, const GlyphRasterClip& clip);

	// Prepares one cubic contour with interior extrema and its native automatic closing edge.
	[[nodiscard]] std::vector<GlyphRasterEdge> BuildGlyphRasterCubicEdges(std::span<const CffGlyphPathPoint, 4> points, const GlyphRasterClip& clip);

	// Native three-point subdivision and interior extrema, with its automatic closing edge.
	[[nodiscard]] std::vector<GlyphRasterEdge> BuildGlyphRasterQuadraticEdges(std::span<const CffGlyphPathPoint, 3> points, const GlyphRasterClip& clip);

	// Borrows move/line/quadratic/cubic paths; contours close explicitly or before the next move.
	[[nodiscard]] std::vector<GlyphRasterEdge> BuildGlyphRasterPathEdges(std::span<const CffGlyphPathCommand> commands, const GlyphRasterClip& clip);

} // namespace sgre::text
