#pragma once

#include "sgre/text/bitmap_draw.hpp"
#include "sgre/text/indicator_text.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>

namespace sgre::text {

	struct BitmapElementDrawScratch {
		std::span<TextCursor> Positions;
		std::span<float> LineWidths;
	};

	// Original5344f0 first prepares glyph positions/line widths, then visits each non-LF element in list order.
	// Font metadata, elements and scratch enclose this synchronous call; the output cannot retain references to commands.
	void EmitBitmapElementGlyphs(const resource::BitmapFont& font, std::span<const IndicatorTextElement> elements, TextCursor origin, resource::GlyphSize spacing, std::uint32_t opacity, std::int32_t alignment, float line_extent, BitmapElementDrawScratch scratch, const std::function<void(const BitmapGlyphCommand&)>& output);

} // namespace sgre::text
