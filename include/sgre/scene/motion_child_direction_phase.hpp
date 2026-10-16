#pragma once

#include "sgre/scene/motion_child_direction.hpp"
#include "sgre/scene/motion_child_target.hpp"
#include "sgre/scene/motion_layer_composition.hpp"
#include "sgre/scene/motion_timeline_clock.hpp"

#include <cstddef>
#include <optional>
#include <span>

namespace sgre::scene {

	struct MotionChildDirectionScene {
		// These are one actual selected parent's immutable owners and its live composed node span.
		const resource::MotionTimeline& Timeline;
		const PreparedMotionTimeline& Preparation;
		const resource::MotionResource& Resource;
		const MotionRootScene& Root;
		std::span<const MotionComposedLayer> Layers;
		// The matching actual child's command/update clock is read before its subsequent UpdateChild.
		const MotionTimelineClockState& ChildClock;
	};

	struct MotionChildDirectionSelection {
		std::size_t LayerIndex;
		std::size_t CurrentFrameIndex;
		// Controls are the actual decoded current/following slots, including retained absent metadata writes.
		const MotionChildFrameControl& CurrentControl;
		const MotionChildFrameControl& FollowingControl;
		float EvaluationTime;
	};

	// No direction retains the child's existing angle/matrix-copy path. No target/pose fallback is provided.
	// Actual scene/selection/control owners outlive this numeric phase; kind3 task/admission remains closed.
	[[nodiscard]] std::optional<float> EvaluateMotionChildDirection(const MotionChildDirectionScene& scene, const MotionChildDirectionSelection& selection);

} // namespace sgre::scene
