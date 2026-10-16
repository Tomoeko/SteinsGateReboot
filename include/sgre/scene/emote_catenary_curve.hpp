#pragma once

#include <array>
#include <cstddef>
#include <optional>

namespace sgre::scene {
	struct EmoteCatenaryCurve {
		double OffsetX = 0;
		double OffsetY = 0;
		double Scale = 0;
		// Native31e870 divides arc length into equal segments, rather than sampling evenly along X.
		[[nodiscard]] std::array<double, 2> SampleArc(double first_x, double last_x, std::size_t index, std::size_t segments) const;
	};
	// Native31ecb0 returns absence when its bounded nested root solve cannot fit the actual endpoints/length.
	[[nodiscard]] std::optional<EmoteCatenaryCurve> PrepareEmoteCatenaryCurve(const std::array<double, 2>& first, const std::array<double, 2>& second, double length);
} // namespace sgre::scene
