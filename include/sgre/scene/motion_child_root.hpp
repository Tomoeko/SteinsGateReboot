#pragma once

#include "sgre/scene/motion_child_control.hpp"
#include "sgre/scene/motion_child_direction_phase.hpp"
#include "sgre/scene/motion_layer_composition.hpp"

#include <cstdint>

namespace sgre::scene {

	// Current prepared frame/control and composed parent are the actual selected owners, alive through this phase.
	// Call only when native node Changed or parameter flags require rebinding, before actual child UpdateChild.
	// This bounded DT0/1 numeric producer does not transfer clip/projection/stencil owners or admit kind3 layers.
	void BindMotionChildRoot(MotionRootScene& child, const PreparedMotionLayer& selected_layer, const PreparedMotionFrame& current_frame, const MotionComposedLayer& parent, const MotionChildFrameControl& control, std::int32_t color_filter);

	// Coupled numeric family consumes the actual selected node/slot/clock and real serialized target ordinal.
	// Inherited clip/projection/stencil transfer and complete child ownership remain required before admission.
	void BindMotionChildRoot(MotionRootScene& child, const MotionChildDirectionScene& scene, const MotionChildDirectionSelection& selection, std::int32_t color_filter);

} // namespace sgre::scene
