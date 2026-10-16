#pragma once

#include "sgre/text/single_substitution.hpp"

#include <cstdint>
#include <span>

namespace sgre::text {

	enum class UnicodeBufferScratchFlags : std::uint32_t {
		None = 0,
		HasNonAscii = 1,
		HasDefaultIgnorables = 2,
		HasUnsafeToBreak = 0x10,
	};

	enum class UnicodeGlyphPropertyFlags : std::uint16_t {
		DefaultIgnorable = 0x20,
		// The exact setter cases are recovered; this bit's downstream consumption remains open.
		IgnorableSpecialCase = 0x40,
		ZeroWidthJoiner = 0x100,
		ZeroWidthNonJoiner = 0x200,
	};

	// Borrows records whose GlyphIndex still contains the Unicode codepoint before glyph mapping.
	// Replaces CharacterState's low word and preserves all other fields and existing scratch bits.
	[[nodiscard]] UnicodeBufferScratchFlags InitializeUnicodeGlyphProperties(std::span<SubstitutionGlyph> glyphs, UnicodeBufferScratchFlags scratch_flags = UnicodeBufferScratchFlags::None);

} // namespace sgre::text
