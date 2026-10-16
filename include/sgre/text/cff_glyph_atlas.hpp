#pragma once

#include "sgre/text/cff_glyph_font.hpp"
#include "sgre/text/cff_glyph_path.hpp"
#include "sgre/text/glyph_atlas.hpp"
#include "sgre/text/glyph_raster_cells.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace sgre::text {

	struct CffAtlasGlyph {
		GlyphAtlasAllocation Allocation;
		std::int32_t LeftPixel = 0;
		std::int32_t TopTranslationPixel = 0;
	};

	class CffGlyphStrokeInput {
	public:
		// Native089110 translates the original path into its padded bitmap before expanding the stroke.
		[[nodiscard]] static std::optional<CffGlyphStrokeInput> Prepare(const CffPreparedGlyph& glyph, std::int32_t edge_width_pixels);
		[[nodiscard]] const CffGlyphBitmapPlan& BitmapPlan() const;
		[[nodiscard]] std::span<const CffGlyphPathCommand> Commands() const;
		[[nodiscard]] double WidthPixels() const;

	private:
		CffGlyphStrokeInput(CffGlyphBitmapPlan plan, CffGlyphPath path, std::int32_t edge_width_pixels);
		CffGlyphBitmapPlan m_Plan;
		CffGlyphPath m_Path;
		std::int32_t m_EdgeWidthPixels = 0;
	};

	class PreparedCffGlyphRaster {
	public:
		// Owns immutable, supported CPU coverage prepared before mapping an atlas.
		[[nodiscard]] static std::optional<PreparedCffGlyphRaster> Prepare(const CffPreparedGlyph& glyph);
		// Consumes the real stroker's owned, already-translated closed contour commands.
		// This raster consumer does not expand an outline or substitute a fill for a stroke.
		[[nodiscard]] static PreparedCffGlyphRaster PrepareStroke(const CffGlyphStrokeInput& input, std::vector<CffGlyphPathCommand> contours);
		[[nodiscard]] const CffGlyphBitmapPlan& BitmapPlan() const;
		[[nodiscard]] const GlyphRasterClip& Clip() const;
		[[nodiscard]] const GlyphRasterAreaCells& Cells() const;
		// Checks moved-from storage before the containing atlas may allocate, map or advance a row.
		void RequireValid() const;

	private:
		[[nodiscard]] static PreparedCffGlyphRaster PrepareContours(CffGlyphBitmapPlan plan, std::span<const CffGlyphPathCommand> contours);
		PreparedCffGlyphRaster(CffGlyphBitmapPlan plan, GlyphRasterClip clip, GlyphRasterAreaCells cells);
		CffGlyphBitmapPlan m_Plan;
		GlyphRasterClip m_Clip;
		GlyphRasterAreaCells m_Cells;
	};

	[[nodiscard]] std::optional<CffAtlasGlyph> RasterizePreparedCffGlyphInAtlas(const PreparedCffGlyphRaster& raster, GlyphAtlasPacking& packing, GlyphAtlasCpuSurface& surface);

	// Prepares and fills the default, unstroked CFF glyph directly into borrowed BGRA atlas storage.
	// Empty glyphs and unavailable atlas mappings return no allocation. Cache before ordinary frames.
	[[nodiscard]] std::optional<CffAtlasGlyph> RasterizeDefaultCffGlyphInAtlas(const CffGlyphFont& font, std::uint32_t glyph_index, std::int32_t pixel_size, GlyphAtlasPacking& packing, GlyphAtlasCpuSurface& surface);

} // namespace sgre::text
