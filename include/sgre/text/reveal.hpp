#pragma once

#include <cstdint>
#include <span>

namespace sgre::text {

	struct TextRevealCursor {
		// Native text time units; conversion to the game clock belongs to the recovered render owner.
		float NextRelativeTime = 0;
		float LastRelativeTime = 0;
		float SegmentBaseTime = 0;
		std::uint32_t SegmentFirstElement = 0;
	};

	struct TextRevealRetiming {
		bool Changed = false;
		float Scale = 1;
		float SegmentDuration = 0;
	};

	// Supply history-line elements followed by current-line elements in their native order.
	// The rendered counter includes failed append attempts and can exceed retained elements.
	// Call only while preparing text; the operation preflights and retimes caller-owned values without allocation.
	[[nodiscard]] TextRevealRetiming RetimeTextRevealSegment(TextRevealCursor& cursor, std::span<float> element_times, std::uint32_t rendered_element_count, std::uint32_t first_element, bool force_target_time, float default_element_delay, std::int32_t target_time);

} // namespace sgre::text
