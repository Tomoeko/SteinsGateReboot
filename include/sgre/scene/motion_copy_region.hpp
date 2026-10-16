#pragma once

#include "sgre/scene/motion_scissor.hpp"

#include <cstdint>

namespace sgre::scene {

	enum class MotionCopyRegionAction {
		Disable,
		Preserve,
		Copy
	};

	struct MotionCopyRegionInput {
		MotionScissorViewport Target;
		MotionScissorMatrix View;
		MotionScissorMatrix Model;
		MotionBounds Accumulated;
		MotionBounds Clip;
		std::uint32_t BlendMode = 0;
		float BlendParameter = 0;
	};

	struct MotionCopyRegionPlan {
		MotionCopyRegionAction Action = MotionCopyRegionAction::Disable;
		MotionBounds Rectangle;
	};

	// Preserve keeps the prior copied texture/UV affine and accumulated bounds; Copy resets bounds after device success.
	[[nodiscard]] MotionCopyRegionPlan PrepareMotionCopyRegion(const MotionCopyRegionInput& input);

} // namespace sgre::scene
