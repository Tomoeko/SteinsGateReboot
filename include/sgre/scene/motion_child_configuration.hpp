#pragma once

#include "sgre/resource/motion.hpp"
#include "sgre/scene/motion_evaluator.hpp"

#include <array>
#include <cstdint>

namespace sgre::scene {

	struct MotionChildRootConfiguration {
		std::uint32_t CoordinatePlane = 0;
		std::array<MotionTransformOperation, 4> TransformOrder = {MotionTransformOperation::Flip, MotionTransformOperation::Shear, MotionTransformOperation::Scale, MotionTransformOperation::Rotate};
		bool IndependentLayerInherit = false;
		float Priority = 0;
	};

	// The actual kind3 raw/prepared node supplies required metadata; priority comes from its actual parent player.
	// The caller uses !IndependentLayerInherit for the existing selected child composition normalization mode.
	[[nodiscard]] MotionChildRootConfiguration PrepareMotionChildRootConfiguration(const resource::MotionLayer& layer, const PreparedMotionLayer& prepared, float parent_priority);

} // namespace sgre::scene
