#pragma once

#include "sgre/resource/motion.hpp"

#include <cstddef>

namespace sgre::scene {

	// Degrees are the player's division offset plus the selected source manager's division angle.
	// The result indexes this immutable Division/MotionList pair and neither selects nor starts a player.
	[[nodiscard]] std::size_t EvaluateMotionDivisionIndex(const resource::MotionTimeline& timeline, float angle_degrees);

} // namespace sgre::scene
