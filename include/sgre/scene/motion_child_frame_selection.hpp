#pragma once

#include "sgre/scene/motion_child_frame_slots.hpp"

#include <cstdint>
#include <functional>

namespace sgre::scene {
	class MotionLayerComposition;

	enum class MotionChildFrameSelection {
		ContinueUpdate,
		BindRoot,
		Reset
	};

	// Uses matching actual selected slots and exclusive child commands for the ordinary clock path.
	// Bound records and nonzero parameter flags require the typed owner overload below.
	// The reset service retires only this child's actual selected descendants, preserving this slot/child owner.
	// BindRoot still requires numeric pose, inherited owners, child update/event prepend and publication afterward.
	[[nodiscard]] MotionChildFrameSelection SelectMotionChildFrameCommands(MotionChildFrameSlots& slots, const MotionTimelineClockState& parent, bool node_changed, std::uint32_t parent_parameter_flags, const std::function<void()>& retire_selected_tree);

	// Exact slot/parameter/local/composition ownership supplies node Changed and the current control word.
	[[nodiscard]] MotionChildFrameSelection SelectMotionChildFrameCommands(MotionChildFrameSlots& slots, const MotionLayerComposition& composition, const std::function<void()>& retire_selected_tree);

} // namespace sgre::scene
