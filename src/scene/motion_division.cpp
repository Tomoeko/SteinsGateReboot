#include "sgre/scene/motion_division.hpp"

#include <cmath>
#include <stdexcept>

namespace sgre::scene {

	static constexpr std::size_t kMaxDivisionEntries = 1000000;
	static constexpr float kMaxDivisionAngleDegrees = 3600000;
	static constexpr float kFullTurnDegrees = 360;

	std::size_t EvaluateMotionDivisionIndex(const resource::MotionTimeline& timeline, float angle_degrees) {
		if (timeline.Kind != resource::MotionTimelineKind::Division || timeline.Division.empty() || timeline.Division.size() > kMaxDivisionEntries || timeline.Division.size() != timeline.MotionList.size()) {
			throw std::runtime_error("motion division requires its bounded nonempty division/motionList pair");
		}

		if (!std::isfinite(angle_degrees) || std::abs(angle_degrees) > kMaxDivisionAngleDegrees) {
			throw std::runtime_error("motion division angle is nonfinite or exceeds its degree limit");
		}

		for (const auto boundary : timeline.Division) {
			if (!std::isfinite(boundary)) {
				throw std::runtime_error("motion division boundary is nonfinite");
			}
		}

		// Native normalization repeatedly adds/subtracts float32 turns; fmod changes large-angle rounding.
		while (angle_degrees < 0) {
			angle_degrees += kFullTurnDegrees;
		}

		while (angle_degrees >= kFullTurnDegrees) {
			angle_degrees -= kFullTurnDegrees;
		}

		std::size_t index = 1;
		while (index < timeline.Division.size()) {
			if (angle_degrees > timeline.Division[index - 1] && timeline.Division[index] >= angle_degrees) {
				break;
			}

			++index;
		}

		return index % timeline.Division.size();
	}

} // namespace sgre::scene
