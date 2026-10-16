#pragma once

#include "sgre/resource/font.hpp"

#include <cstdint>
#include <span>

namespace sgre::text {

	enum class TextDirection {
		Horizontal,
		Vertical
	};

	enum class TextAlignment {
		Start,
		Center,
		End
	};

	struct TextCursor {
		float X = 0;
		float Y = 0;
	};

	struct TextElementPlacement {
		TextCursor Position;
		TextCursor NextCursor;
	};

	[[nodiscard]] TextAlignment DecodeTextAlignment(std::int32_t native_alignment);
	[[nodiscard]] float TextLineAlignmentOffset(TextAlignment alignment, float line_extent, float advanced_cursor);
	// This predicate enters native overflow handling; prohibition/word grouping may move earlier elements.
	[[nodiscard]] bool TextElementReachesLineLimit(TextDirection direction, const TextCursor& cursor, const resource::GlyphSize& metrics, float line_extent, bool allow_overflow);
	// The owner resolves wrapping before placing an element. These arithmetic operations allocate nothing.
	[[nodiscard]] TextElementPlacement PlaceMeasuredTextElement(TextDirection direction, const TextCursor& cursor, const resource::GlyphSize& metrics, float pitch);
	[[nodiscard]] float TextLineCrossExtent(TextDirection direction, std::span<const resource::GlyphSize> elements, float minimum_extent);

} // namespace sgre::text
