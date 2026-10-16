#pragma once

#include "sgre/scene/motion_clip_bounds.hpp"

#include <cstdint>

namespace sgre::scene {

	struct MotionCopiedTargetSize {
		std::int32_t Width = 0;
		std::int32_t Height = 0;
	};

	struct MotionCopiedTargetUpdate {
		MotionCopiedTargetSize Size;
		bool Replace = false;
	};

	// Native replacement deletes the previous owner, stores Size, allocates/prepares and then installs the new owner.
	// This function only decides that transition; physical texture ownership and format stay caller-owned.
	[[nodiscard]] MotionCopiedTargetUpdate PrepareMotionCopiedTargetUpdate(MotionCopiedTargetSize current, const MotionBounds& requested);

} // namespace sgre::scene
