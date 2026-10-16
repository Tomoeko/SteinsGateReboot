#include "sgre/text/reveal.hpp"

#include <cmath>
#include <stdexcept>

namespace sgre::text {

	static constexpr std::size_t kMaximumRevealElements = 1'000'000;

	static void RequireFiniteTime(float time) {
		if (!std::isfinite(time)) {
			throw std::runtime_error("text reveal time is nonfinite or overflowed");
		}
	}

	TextRevealRetiming RetimeTextRevealSegment(TextRevealCursor& cursor, std::span<float> element_times, std::uint32_t first_element, bool force_target_time, float default_element_delay, std::int32_t target_time) {
		if (element_times.size() > kMaximumRevealElements || cursor.SegmentFirstElement > kMaximumRevealElements || first_element > kMaximumRevealElements) {
			throw std::runtime_error("text reveal count exceeds recovery limit");
		}

		RequireFiniteTime(cursor.NextRelativeTime);
		RequireFiniteTime(cursor.LastRelativeTime);
		RequireFiniteTime(cursor.SegmentBaseTime);
		RequireFiniteTime(default_element_delay);
		TextRevealRetiming result;
		if (element_times.size() <= first_element) {
			return result;
		}

		const float available_duration = default_element_delay + cursor.LastRelativeTime;
		float duration = static_cast<float>(target_time) - cursor.SegmentBaseTime;
		RequireFiniteTime(available_duration);
		RequireFiniteTime(duration);
		if (duration <= 0) {
			// MAXSS selects its second operand on equality, preserving the original signed-zero result.
			duration = available_duration > duration ? available_duration : duration;
		} else if (force_target_time || duration <= available_duration) {
			result.Scale = duration / available_duration;
		}

		RequireFiniteTime(result.Scale);
		const float segment_end = duration + cursor.SegmentBaseTime;
		RequireFiniteTime(segment_end);
		for (std::size_t index = first_element; index < element_times.size(); ++index) {
			RequireFiniteTime(element_times[index]);
			RequireFiniteTime(result.Scale * element_times[index] + cursor.SegmentBaseTime);
		}

		for (std::size_t index = first_element; index < element_times.size(); ++index) {
			element_times[index] = result.Scale * element_times[index] + cursor.SegmentBaseTime;
		}

		cursor.SegmentBaseTime = segment_end;
		cursor.SegmentFirstElement = static_cast<std::uint32_t>(element_times.size());
		cursor.NextRelativeTime = 0;
		result.SegmentDuration = duration;
		result.Changed = true;

		return result;
	}

} // namespace sgre::text
