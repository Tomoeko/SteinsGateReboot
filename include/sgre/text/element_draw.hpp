#pragma once

#include "sgre/text/layout.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace sgre::text {

	enum class TextFontStyle : std::uint32_t {
		Regular = 0,
		Bold = 1,
		Italic = 2
	};

	enum class TextGlyphPassKind {
		Edge,
		Shadow,
		Main
	};

	struct TextElementDrawStyle {
		// These are the owner's stored words, before the native draw caller's byte swap.
		std::uint32_t MainColor = 0;
		std::uint32_t ShadowColor = 0;
		std::uint32_t EdgeColor = 0;
		bool Bold = false;
		bool Italic = false;
		bool ShadowEnabled = false;
		bool EdgeEnabled = false;
		std::int32_t ShadowOffset = 0;
		std::int32_t EdgeWidth = 0;
	};

	struct TextGlyphDrawPass {
		TextGlyphPassKind Kind = TextGlyphPassKind::Main;
		TextCursor Position;
		std::uint32_t ColorRgba = 0;
		std::int32_t EdgeWidth = 0;
	};

	struct TextElementDrawPlan {
		TextFontStyle FontStyle = TextFontStyle::Regular;
		std::array<TextGlyphDrawPass, 3> Passes{};
		std::size_t PassCount = 0;
	};

	// Draw ordinary passes first; then traverse all ruby glyphs for each pass in the same order.
	// Font selection, glyph-run ownership, and rasterization remain the prepared owner's responsibility.
	[[nodiscard]] TextElementDrawPlan PrepareTextElementDrawPlan(const TextCursor& render_origin, const TextCursor& element_position, const TextElementDrawStyle& style);
	[[nodiscard]] TextGlyphDrawPass PrepareTextRubyDrawPass(const TextGlyphDrawPass& element_pass, const TextCursor& ruby_offset);

} // namespace sgre::text
