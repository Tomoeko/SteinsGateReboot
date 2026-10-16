#pragma once

#include "sgre/text/initial_clusters.hpp"

#include <span>

namespace sgre::text {

	// Neutral-shaper ordering phase after the native normalization buffer swap, with input cursor zero.
	// previous_input contains retained output/backfill records, with separate storage or the same base as glyphs.
	// CharacterState must already contain the recovered category and modified combining class.
	[[nodiscard]] UnicodeBufferScratchFlags ReorderNeutralUnicodeCombiningGlyphs(std::span<SubstitutionGlyph> glyphs, std::span<SubstitutionGlyph> previous_input, GlyphClusterLevel level, UnicodeBufferScratchFlags scratch_flags);

} // namespace sgre::text
