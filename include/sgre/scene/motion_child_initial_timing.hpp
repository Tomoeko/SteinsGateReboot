#pragma once

#include "sgre/scene/motion_child_control.hpp"
#include "sgre/scene/motion_evaluator.hpp"

#include <cstddef>
#include <cstdint>

namespace sgre::scene {
	class MotionLayerParameterBindings;

	// Actual decoded current frame/control and parent TickCount are supplied by the selected owners before UpdateChild.
	// Native producer guards Playing and TickAssigned; the numeric tick setter separately guards preferred root.
	// This does not select a child timeline, bind its pose/owners, update it or transfer its events.
	[[nodiscard]] bool AssignMotionChildInitialTick(MotionPlayerCommands& child, const MotionTimelineClockState& parent, const PreparedMotionFrame& current, const MotionChildFrameControl& control, std::size_t maximum_loop_wraps = 65536);

	[[nodiscard]] bool AssignMotionChildInitialTick(MotionPlayerCommands& child, const MotionLayerParameterBindings& parameters, std::uint32_t layer, const PreparedMotionFrame& current, const MotionChildFrameControl& control, std::size_t maximum_loop_wraps = 65536);

} // namespace sgre::scene
