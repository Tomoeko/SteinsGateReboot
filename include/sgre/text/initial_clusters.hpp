#pragma once

#include "sgre/text/unicode_preprocess.hpp"

#include <cstdint>
#include <span>

namespace sgre::text {

	enum class GlyphClusterLevel : std::uint32_t {
		MonotoneGraphemes = 0,
		MonotoneCharacters = 1,
		Characters = 2,
	};

	// Borrows prepared Unicode records before direction adjustment and glyph mapping.
	// The native initial phase has no pending output records and its input cursor is zero.
	[[nodiscard]] UnicodeBufferScratchFlags InitializeUnicodeGlyphClusters(std::span<SubstitutionGlyph> glyphs, GlyphClusterLevel level, UnicodeBufferScratchFlags scratch_flags);

} // namespace sgre::text
