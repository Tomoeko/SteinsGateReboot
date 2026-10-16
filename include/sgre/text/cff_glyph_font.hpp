#pragma once

#include "sgre/resource/cff.hpp"
#include "sgre/resource/cff_driver_hints.hpp"
#include "sgre/text/cff_glyph_metrics.hpp"
#include "sgre/text/cff_glyph_path.hpp"
#include "sgre/text/cff_hinted_metrics.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace sgre::text {

	struct CffPreparedGlyph {
		std::uint16_t GlyphIndex = 0;
		std::uint16_t PixelSize = 0;
		CffContourOutline Outline;
		CffHintedGlyphMetrics Metrics;
		// TrueType keeps its owned quadratic path instead of manufacturing cubic
		// CFF point tags. An absent path retains the existing CFF decomposition.
		std::optional<CffGlyphPath> PreparedPath;
	};

	struct CffUnhintedGlyph {
		std::uint16_t GlyphIndex = 0;
		std::uint16_t PixelSize = 0;
		CffScaledGlyph Outline;
	};

	class CffGlyphFont {
	public:
		// The immutable sfnt reader and its font bytes must outlive this owner.
		// Supports the observed normalized 1000-unit CFF fonts and default private driver settings.
		explicit CffGlyphFont(const resource::SfntFont& font);
		// CPU preparation returns owned points/contours. Cache it before ordinary rendering.
		[[nodiscard]] CffPreparedGlyph PrepareGlyph(std::uint32_t glyph_index, std::int32_t pixel_size) const;
		// Flags-2 shaping preparation uses raw design programs and no hinted/raster metrics.
		[[nodiscard]] CffUnhintedGlyph PrepareUnhintedGlyph(std::uint32_t glyph_index, std::int32_t pixel_size) const;

	private:
		const resource::SfntFont& m_Sfnt;
		resource::CffFont m_Font;
		std::vector<resource::CffDriverHintParameters> m_Hints;
	};

} // namespace sgre::text
