#pragma once

#include "sgre/text/glyph_raster_cells.hpp"

#include <cstdint>
#include <span>

namespace sgre::text {
	enum class GlyphRasterFillRule {
		NonZero,
		EvenOdd
	};

	// Borrows prepared area cells and a contiguous premultiplied BGRA32 surface.
	// Reproduces the original static analytic SrcCopy kernel; source and surface use native packed channel order.
	void CompositeGlyphRasterSolidCopy(const GlyphRasterAreaCells& cells, std::span<std::uint32_t> surface, std::uint16_t height_pixels, std::uint32_t premultiplied_bgra32 = 0xffffffff, std::uint8_t opacity = 255, GlyphRasterFillRule fill_rule = GlyphRasterFillRule::NonZero);
	// The byte surface may have padding between rows and starts at an already allocated glyph interior.
	void CompositeGlyphRasterSolidCopyBytes(const GlyphRasterAreaCells& cells, std::span<std::uint8_t> surface, std::uint16_t height_pixels, std::uint32_t row_pitch_bytes, std::uint32_t premultiplied_bgra32 = 0xffffffff, std::uint8_t opacity = 255, GlyphRasterFillRule fill_rule = GlyphRasterFillRule::NonZero);

} // namespace sgre::text
