#pragma once

#include "sgre/text/layout.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace sgre::text {

	struct ShapedTextGlyph {
		std::uint32_t GlyphIndex = 0;
		std::int32_t XAdvance26Dot6 = 0;
		std::int32_t YAdvance26Dot6 = 0;
		std::int32_t XOffset26Dot6 = 0;
		std::int32_t YOffset26Dot6 = 0;
	};

	struct TextRunGlyph {
		std::uint32_t GlyphIndex = 0;
		TextCursor Position;
	};

	struct OwnedTextGlyphRun {
		std::vector<TextRunGlyph> Glyphs;
		TextCursor Advance;
	};

	// Converts recovered shaping output into caller-owned glyph positions, returning the signed advance.
	// This preserves native 26.6-to-float ordering; it does not perform Unicode shaping or font selection.
	[[nodiscard]] TextCursor PrepareTextGlyphRun(std::span<const ShapedTextGlyph> shaped_glyphs, std::span<TextRunGlyph> output);

} // namespace sgre::text
