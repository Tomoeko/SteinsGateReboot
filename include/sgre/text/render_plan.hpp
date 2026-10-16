#pragma once

#include "sgre/text/block_geometry.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sgre::text {

	struct TextRenderElement {
		// Identity belongs to this prepared text owner, including retained link entries.
		std::uint32_t ElementId = 0;
		TextGlyphGeometry Geometry;
		std::u16string_view Text;
		float RevealTime = 0;
		std::int32_t LinkIndex = -1;
	};

	struct TextRenderLine {
		std::span<const TextRenderElement> Elements;
	};

	struct TextLinkState {
		std::vector<std::uint32_t> Elements;
		std::vector<TextBounds> RunBounds;
		std::int32_t LastRun = -1;
		std::u16string Text;
	};

	struct TextRevealBoundary {
		std::uint32_t FirstElement = 0;
		float Time = 0;
	};

	struct TextRenderPlan {
		std::vector<std::uint32_t> RenderOrder;
		// Retained reveal times follow the exact finalized pointer order used by native02eed0.
		std::vector<float> RevealTimes;
		std::vector<TextLinkState> Links;
		std::vector<TextRevealBoundary> RevealBoundaries;
	};

	// The native key-wait getter excludes the last terminal record and scales copied times only.
	[[nodiscard]] std::vector<TextRevealBoundary> PrepareTextKeyWaits(std::span<const TextRevealBoundary> boundaries, float time_scale);

	// Native reverse scan intentionally does not assume time ordering and leaves unordered comparisons false.
	[[nodiscard]] std::uint32_t CalculateTextShownElementCount(std::span<const float> finalized_times, std::int32_t elapsed_time, float time_scale);

	// Call during text preparation. Lines borrow the owner's finalized geometry and UTF-16 text.
	// Existing link state is retained and appended, as in the original finalized-list phase.
	// More than one reveal segment enables stable time ordering and updates all but the last boundary.
	[[nodiscard]] TextRenderPlan PrepareTextRenderPlan(std::span<const TextRenderLine> lines, std::span<const TextLinkState> links, std::span<const TextRevealBoundary> reveal_boundaries, std::uint32_t reveal_segment_count);

} // namespace sgre::text
