#pragma once

#include "sgre/scene/motion_clip_bounds.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace sgre::scene {

	struct MotionMaskedBlendInput {
		MotionBounds TargetBounds;
		resource::MotionPoint AxisScale;
		float Amount = 0;
	};

	enum class MotionMaskedBlendAxis {
		Horizontal,
		Vertical
	};

	struct MotionMaskedBlendPass {
		MotionMaskedBlendAxis Axis = MotionMaskedBlendAxis::Horizontal;
		std::uint32_t Step = 0;
		// The original blend callback receives an exact integral float in [0, 9].
		float BlendParameter = 0;
	};

	// Recovers special blend 0x10d pass selection; geometry, flush and shader work remain caller-owned.
	[[nodiscard]] std::size_t PrepareMotionMaskedBlendPasses(const MotionMaskedBlendInput& input, std::span<MotionMaskedBlendPass> output);

} // namespace sgre::scene
