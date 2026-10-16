#pragma once

#include "sgre/scene/motion_scissor.hpp"

namespace sgre::scene {
	// Exact native reset with zero XY, unit scale and zero rotation; used only by a real graphics reset phase.
	[[nodiscard]] MotionScissorMatrix PrepareDefaultRenderModel();
} // namespace sgre::scene
