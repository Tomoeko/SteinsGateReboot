#pragma once

#include "sgre/text/layout.hpp"

#include <span>

namespace sgre::text {

	struct TextBounds {
		float Left = 0;
		float Top = 0;
		float Right = 0;
		float Bottom = 0;
	};

	struct TextGlyphGeometry {
		TextCursor Position;
		resource::GlyphSize Metrics;
	};

	struct TextBlockElement {
		TextGlyphGeometry Geometry;
		std::span<const TextGlyphGeometry> Ruby;
	};

	struct TextBlockLine {
		// The owner supplies the native line's initial bounds; expansion keeps their coordinate origin.
		TextBounds Bounds;
		std::span<TextBlockElement> Elements;
	};

	struct TextBlockGeometrySettings {
		TextDirection Direction = TextDirection::Horizontal;
		TextAlignment Alignment = TextAlignment::Start;
		float RenderWidth = 0;
		float RenderHeight = 0;
	};

	struct TextBlockGeometry {
		TextBounds Bounds;
		float CrossAlignmentOffset = 0;
	};

	// Expands line bounds, unions the block, and aligns ordinary positions without allocation.
	// Ruby bounds use their stored coordinates; their offsets are not rewritten by block alignment.
	// An empty history retains the incoming block bounds before applying the native alignment phase.
	[[nodiscard]] TextBlockGeometry FinalizeTextBlockGeometry(const TextBlockGeometrySettings& settings, std::span<TextBlockLine> lines, const TextBounds& previous_bounds = {});

} // namespace sgre::text
