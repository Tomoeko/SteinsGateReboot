#pragma once

#include "sgre/scene/motion_child_control.hpp"
#include "sgre/scene/motion_evaluator.hpp"

#include <optional>

namespace sgre::scene {

	// These are the real decoded current/following slots and their retained controls, not a fresh metadata search.
	// Immutable frames, controls and the actual resource curve owner outlive this numeric phase.
	// A disabled direction returns no offset; DT2/3/4 angle/target/path producers remain separate required phases.
	[[nodiscard]] std::optional<float> EvaluateMotionChildDirectionOffset(const PreparedMotionFrame& current, const PreparedMotionFrame& following, const MotionChildFrameControl& current_control, const MotionChildFrameControl& following_control, const resource::MotionResource& resource, float evaluation_position);

} // namespace sgre::scene
