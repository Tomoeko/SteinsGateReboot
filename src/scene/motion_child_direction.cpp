#include "sgre/scene/motion_child_direction.hpp"

#include <cmath>
#include <stdexcept>

namespace sgre::scene {

	static constexpr std::size_t kChildDirectionAngleEasing = 2;

	std::optional<float> EvaluateMotionChildDirectionOffset(const PreparedMotionFrame& current, const PreparedMotionFrame& following, const MotionChildFrameControl& current_control, const MotionChildFrameControl& following_control, const resource::MotionResource& resource, float evaluation_position) {
		if (current.Hidden || current.Attributes.Source.empty() || current.Attributes.Source.front() == '\0') {
			throw std::runtime_error("motion child direction requires actual selected-tree reset for hidden/empty current frames");
		}

		if (current_control.DirectionMode > 4) {
			throw std::runtime_error("motion child direction mode is outside the recovered native family");
		}

		if (current_control.DirectionMode == 0) {
			return std::nullopt;
		}

		const auto start = current_control.DirectionOffset;
		if (!std::isfinite(start)) {
			throw std::runtime_error("motion child direction requires a finite selected offset");
		}

		if (!current_control.CompleteDirection || !current.Tween || following.Hidden || following_control.DirectionMode == 0) {
			return start;
		}

		const auto end = following_control.DirectionOffset;
		if (!std::isfinite(end)) {
			throw std::runtime_error("motion child direction requires a finite following offset");
		}

		if (start == end) {
			return start;
		}

		if (!std::isfinite(evaluation_position) || !std::isfinite(current.Time) || !std::isfinite(following.Time) || following.Time <= current.Time) {
			throw std::runtime_error("motion child direction interpolation requires actual finite positive-duration selected slots");
		}

		// This native ratio ignores frame interval and the local-pose epsilon snap.
		const auto ratio = (evaluation_position - current.Time) / (following.Time - current.Time);
		auto adjusted_end = end;
		if (end <= start && start - end > 180.0f) {
			adjusted_end += 360.0f;
		} else if (end > start && end - start > 180.0f) {
			adjusted_end -= 360.0f;
		}

		const resource::EasingCurve* curve = nullptr;
		const auto curve_index = current.Attributes.EasingIndices[kChildDirectionAngleEasing];
		if (start != adjusted_end && curve_index.has_value()) {
			if (*curve_index >= resource.Easing.size()) {
				throw std::runtime_error("motion child direction angle easing exceeds its actual resource owner");
			}

			curve = &resource.Easing[*curve_index];
		}

		return resource::InterpolateAngle(start, end, ratio, resource::RotationTransition::Shortest, curve);
	}

} // namespace sgre::scene
