#pragma once

#include "sgre/scene/motion_evaluator.hpp"

#include <cstdint>
#include <string>

namespace sgre::scene {
	// Native51c0a0 owns the outgoing value until51e720 consumes its matching incoming slot.
	// Strings in Numeric are not used: incoming source/icon remain owned by the incoming preparation.
	struct MotionJoinedLayer {
		std::string Path;
		std::uint32_t Kind = 0;
		std::uint32_t Mask = 0;
		MotionLayerSample Numeric;
	};
} // namespace sgre::scene
