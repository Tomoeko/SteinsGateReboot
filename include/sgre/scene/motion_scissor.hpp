#pragma once

#include "sgre/scene/motion_clip_bounds.hpp"

#include <array>
#include <cstdint>
#include <optional>

namespace sgre::scene {

	struct MotionScissorMatrix {
		std::array<float, 12> Coefficients{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
	};

	struct MotionScissorViewport {
		std::int32_t Width = 0;
		std::int32_t Height = 0;
	};

	struct MotionIntegerScissor {
		std::int32_t MinimumX = 0;
		std::int32_t MinimumY = 0;
		std::int32_t MaximumX = 0;
		std::int32_t MaximumY = 0;
	};

	struct MotionScissorUpdate {
		MotionBounds Raw;
		MotionBounds Rounded;
		std::optional<MotionIntegerScissor> Submission;
	};

	[[nodiscard]] MotionBounds TransformMotionScissorBounds(const MotionBounds& bounds, const MotionScissorMatrix& view, const MotionScissorMatrix& model, MotionScissorViewport viewport);
	// Store Raw/Rounded before submission; update the applied rounded cache only after submission succeeds.
	[[nodiscard]] MotionScissorUpdate PrepareMotionScissorUpdate(const MotionBounds& requested, const MotionBounds& applied_rounded);

} // namespace sgre::scene
