#include "sgre/scene/motion_child_direction_phase.hpp"

#include "sgre/scene/motion_direction_math.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace sgre::scene {

	static float DirectionFromVector(float vertical, float horizontal, float offset) {
		auto angle = EvaluateMotionDirectionRadians(vertical, horizontal);
		angle *= 360.0f;
		angle /= std::bit_cast<float>(std::uint32_t{0x40c90fdb});
		angle += offset;
		if (!std::isfinite(angle) || std::abs(angle) > 3600000.0f) {
			throw std::runtime_error("motion child direction angle exceeds its finite bounded native wrap domain");
		}

		while (angle < 0) {
			angle += 360.0f;
		}

		while (angle >= 360.0f) {
			angle -= 360.0f;
		}

		return angle;
	}

	static std::optional<float> DirectionFromSelectedCoordinates(const PreparedMotionLayer& layer, const PreparedMotionFrame& current, const PreparedMotionFrame& following, const resource::MotionResource& resource, float position, float offset) {
		if (!current.Tween || following.Hidden) {
			return std::nullopt;
		}

		if (!std::isfinite(position) || !std::isfinite(current.Time) || !std::isfinite(following.Time) || following.Time <= current.Time) {
			throw std::runtime_error("motion child coordinate derivative requires actual finite positive-duration selected slots");
		}

		auto lower_ratio = (position - current.Time) / (following.Time - current.Time);
		auto upper_ratio = lower_ratio + 0.0001f;
		if (upper_ratio >= 1.0f) {
			upper_ratio = 1.0f;
			lower_ratio = std::bit_cast<float>(std::uint32_t{0x3f7ff972});
		}

		const auto lower = InterpolateMotionChildCoordinates(current, following, resource, lower_ratio, layer.CoordinatePlane);
		const auto upper = InterpolateMotionChildCoordinates(current, following, resource, upper_ratio, layer.CoordinatePlane);
		const auto axis = layer.CoordinatePlane == 0 ? 1u : 2u;

		return DirectionFromVector(upper[axis] - lower[axis], upper[0] - lower[0], offset);
	}

	std::optional<float> EvaluateMotionChildDirection(const MotionChildDirectionScene& scene, const MotionChildDirectionSelection& selection) {
		if (scene.Timeline.Kind != resource::MotionTimelineKind::Frames || scene.Layers.size() != scene.Preparation.Layers.size() || selection.LayerIndex >= scene.Layers.size()) {
			throw std::runtime_error("motion child direction requires its actual ordinary current composed node span");
		}

		const auto& layer = scene.Preparation.Layers[selection.LayerIndex];
		if (layer.Type != 3 || layer.CoordinatePlane > 1 || layer.MeshTransform != 0 || (layer.StencilType != 0 && layer.StencilType != 5) || selection.CurrentFrameIndex >= layer.Frames.size() || layer.Frames.size() - selection.CurrentFrameIndex < 2) {
			throw std::runtime_error("motion child direction requires the actual bounded kind3 selected current/following frames");
		}

		const auto& current = layer.Frames[selection.CurrentFrameIndex];
		const auto& following = layer.Frames[selection.CurrentFrameIndex + 1];
		const auto offset = EvaluateMotionChildDirectionOffset(current, following, selection.CurrentControl, selection.FollowingControl, scene.Resource, selection.EvaluationTime);
		if (!offset) {
			return std::nullopt;
		}

		const auto& parent = scene.Layers[selection.LayerIndex];
		if (selection.CurrentControl.DirectionMode == 1) {
			const auto angle = *offset + parent.Hierarchy.Attributes.Angle;
			if (!std::isfinite(angle)) {
				throw std::runtime_error("motion child layer-relative direction requires a finite current node angle");
			}

			return angle;
		}

		if (selection.CurrentControl.DirectionMode == 2 && !scene.ChildClock.InitialCompositionPending) {
			const auto axis = layer.CoordinatePlane == 0 ? 1u : 2u;

			return DirectionFromVector(parent.Displacement[axis], parent.Displacement[0], *offset);
		}

		if (selection.CurrentControl.DirectionMode == 2 || selection.CurrentControl.DirectionMode == 3) {
			return DirectionFromSelectedCoordinates(layer, current, following, scene.Resource, selection.EvaluationTime, *offset);
		}

		const auto target = ResolveMotionChildTargetIndex(scene.Timeline, selection.CurrentControl.DirectionTarget, scene.Layers.size());
		if (!target) {
			return std::nullopt;
		}

		if (*target == 0 && !scene.Root.Frame().Composed) {
			throw std::runtime_error("motion child root target requires its actual current composed root pose");
		}

		const auto coordinate = *target == 0 ? scene.Root.Frame().Hierarchy.Attributes.Coordinate : scene.Layers[*target - 1].Hierarchy.Attributes.Coordinate;
		const auto axis = layer.CoordinatePlane == 0 ? 1u : 2u;

		return DirectionFromVector(coordinate[axis] - parent.Hierarchy.Attributes.Coordinate[axis], coordinate[0] - parent.Hierarchy.Attributes.Coordinate[0], *offset);
	}

} // namespace sgre::scene
