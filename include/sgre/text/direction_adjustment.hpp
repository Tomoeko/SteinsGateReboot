#pragma once

#include "sgre/text/initial_clusters.hpp"

#include <cstdint>
#include <span>

namespace sgre::text {

	enum class GlyphShapingDirection : std::uint32_t {
		LeftToRight = 4,
		RightToLeft = 5,
		TopToBottom = 6,
		BottomToTop = 7,
	};

	// iso15924_tag packs four exact tag bytes most-significant first; unknown tags use native LTR.
	// Borrows prepared records with input/output cursors zero and no initialized positions.
	[[nodiscard]] GlyphShapingDirection AdjustUnicodeGlyphDirection(std::span<SubstitutionGlyph> glyphs, GlyphClusterLevel level, GlyphShapingDirection direction, std::uint32_t iso15924_tag);

} // namespace sgre::text
