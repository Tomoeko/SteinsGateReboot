#pragma once

#include "sgre/text/glyph_raster_edges.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace sgre::text {
	struct GlyphRasterAreaCells {
		std::uint16_t WidthPixels = 0;
		std::uint16_t FirstRowPixel = 0;
		std::uint16_t RowCount = 0;
		// Each row has WidthPixels + 1 modulo-32-bit signed area deltas.
		std::vector<std::uint32_t> Deltas;
		// One flag per four-pixel group; the final boundary cell is included.
		std::vector<std::uint8_t> ActiveGroups;
	};

	[[nodiscard]] GlyphRasterAreaCells AccumulateGlyphRasterCells(std::span<const GlyphRasterEdge> edges, const GlyphRasterClip& clip);
} // namespace sgre::text
