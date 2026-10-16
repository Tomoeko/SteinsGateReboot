#pragma once

#include "sgre/resource/truetype_outline.hpp"
#include "sgre/text/vector_metrics.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace sgre::text {

	struct TrueTypeGlyphMetrics {
		std::int32_t Width26Dot6 = 0;
		std::int32_t Height26Dot6 = 0;
		std::int32_t HorizontalBearingX26Dot6 = 0;
		std::int32_t HorizontalBearingY26Dot6 = 0;
		std::int32_t HorizontalAdvance26Dot6 = 0;
		std::int32_t VerticalBearingX26Dot6 = 0;
		std::int32_t VerticalBearingY26Dot6 = 0;
		std::int32_t VerticalAdvance26Dot6 = 0;
		std::int32_t LinearHorizontalAdvance16Dot16 = 0;
		std::int32_t LinearVerticalAdvance16Dot16 = 0;
		std::int32_t VerticalOriginX26Dot6 = 0;
		std::int32_t VerticalOriginY26Dot6 = 0;
	};

	struct TrueTypeUnhintedGlyph {
		std::uint16_t GlyphIndex = 0;
		// Owned native 26.6 control points retain quadratic tags, not CFF cubic tags.
		std::vector<resource::TrueTypePoint> Points;
		std::vector<std::uint16_t> ContourEnds;
		resource::TrueTypeBounds ControlBounds;
		// PP1/PP2 horizontal and PP3/PP4 vertical; excluded from the visible outline.
		std::array<std::array<std::int32_t, 2>, 4> Phantoms{};
		TrueTypeGlyphMetrics Metrics;
	};

	class TrueTypeGlyphFont {
	public:
		// The actual immutable SFNT reader and bytes outlive this owner.
		explicit TrueTypeGlyphFont(const resource::SfntFont& font);
		// Explicit positive native TT scales; no lazy size, hinting, shaping or raster admission.
		// Translation-only XY composites are supported; matrix and point attachment remain guarded.
		[[nodiscard]] TrueTypeUnhintedGlyph PrepareUnhintedGlyph(std::uint32_t glyph_index, VectorFontScale horizontal_scale, VectorFontScale vertical_scale) const;

	private:
		const resource::SfntFont& m_Sfnt;
		resource::TrueTypeFont m_Font;
	};

} // namespace sgre::text
