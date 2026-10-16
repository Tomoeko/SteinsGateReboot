#include "sgre/scene/motion_child_root.hpp"

#include <array>
#include <cmath>
#include <stdexcept>

namespace sgre::scene {

	static void BindMotionChildPose(MotionRootScene& child, const PreparedMotionLayer& selected_layer, const PreparedMotionFrame& current_frame, const MotionComposedLayer& parent, std::optional<float> direction, std::int32_t color_filter) {
		if (child.MatrixMode() != MotionRootMatrixMode::SuppliedByParent || selected_layer.Type != 3 || selected_layer.CoordinatePlane > 1 || selected_layer.MeshTransform != 0 || (selected_layer.StencilType != 0 && selected_layer.StencilType != 5)) {
			throw std::runtime_error("motion child root requires its ordinary kind3 parent pose and actual supplied-matrix owner");
		}

		if (current_frame.Hidden || (current_frame.Attributes.Source.empty() || current_frame.Attributes.Source.front() == '\0')) {
			throw std::runtime_error("motion hidden/empty-source child requires actual selected-tree reset before root binding");
		}

		const auto& attributes = parent.Hierarchy.Attributes;
		const auto& matrix = parent.Hierarchy.Transform;
		const std::array<float, 14> finite{attributes.Coordinate[0], attributes.Coordinate[1], attributes.Coordinate[2], attributes.Angle, attributes.ZoomX, attributes.ZoomY, attributes.ShearX, attributes.ShearY, current_frame.Attributes.OriginX, current_frame.Attributes.OriginY, matrix.XX, matrix.XY, matrix.YX, matrix.YY};
		for (const auto value : finite) {
			if (!std::isfinite(value)) {
				throw std::runtime_error("motion child root requires finite parent pose, origin and matrix");
			}
		}

		MotionChildRootPose pose;
		pose.Overrides = child.Overrides();
		auto coordinate = attributes.Coordinate;
		pose.Overrides.Visible = parent.Active;
		pose.Overrides.FlipX = attributes.FlipX;
		pose.Overrides.FlipY = attributes.FlipY;
		if (pose.Overrides.ZoomX != attributes.ZoomX || pose.Overrides.ZoomY != attributes.ZoomY) {
			pose.Overrides.ZoomX = attributes.ZoomX;
			pose.Overrides.ZoomY = attributes.ZoomY;
		}

		if (pose.Overrides.ShearX != attributes.ShearX || pose.Overrides.ShearY != attributes.ShearY) {
			pose.Overrides.ShearX = attributes.ShearX;
			pose.Overrides.ShearY = attributes.ShearY;
		}
		pose.Overrides.Opacity = attributes.Opacity;
		pose.Transform = matrix;
		if (current_frame.Attributes.OriginX != 0 || current_frame.Attributes.OriginY != 0) {
			const auto origin_x = -current_frame.Attributes.OriginX;
			const auto origin_y = -current_frame.Attributes.OriginY;
			const auto x_adjust = origin_y * matrix.XY + origin_x * matrix.XX;
			const auto axis_adjust = origin_y * matrix.YY + origin_x * matrix.YX;
			coordinate[0] += x_adjust;
			coordinate[selected_layer.CoordinatePlane == 0 ? 1 : 2] += axis_adjust;
		}

		// Native equal-float comparisons preserve stored signed-zero bits for each complete field group.
		if (pose.Overrides.Coordinate != coordinate) {
			pose.Overrides.Coordinate = coordinate;
		}

		if (direction.has_value()) {
			if (pose.Overrides.Angle != *direction) {
				pose.Overrides.Angle = *direction;
			}

			if (*direction != attributes.Angle) {
				pose.Transform = RotateMotionChildTransform(matrix, *direction - attributes.Angle, attributes.FlipX != attributes.FlipY);
			}
		}

		const auto modulation = ComposeMotionColor(parent.Hierarchy.ModulationColor, attributes.Colors[0]);
		// Actual 522753 compares player +1c8 (color-filter mode), not its previous root color.
		pose.ModulationColor = static_cast<std::uint32_t>(color_filter) != modulation ? modulation : child.ModulationColor();
		child.BindChildPose(pose);
	}

	void BindMotionChildRoot(MotionRootScene& child, const PreparedMotionLayer& selected_layer, const PreparedMotionFrame& current_frame, const MotionComposedLayer& parent, const MotionChildFrameControl& control, std::int32_t color_filter) {
		if (control.DirectionMode > 1 || (control.DirectionMode != 0 && control.CompleteDirection && current_frame.Tween)) {
			throw std::runtime_error("motion child direction requires its actual selected-node velocity/curve/target or interpolated selected-control phase");
		}

		if (control.DirectionMode == 1 && !std::isfinite(control.DirectionOffset)) {
			throw std::runtime_error("motion child root requires a finite selected direction offset");
		}

		const std::optional<float> direction = control.DirectionMode == 1 ? std::optional<float>(control.DirectionOffset + parent.Hierarchy.Attributes.Angle) : std::nullopt;
		BindMotionChildPose(child, selected_layer, current_frame, parent, direction, color_filter);
	}

	void BindMotionChildRoot(MotionRootScene& child, const MotionChildDirectionScene& scene, const MotionChildDirectionSelection& selection, std::int32_t color_filter) {
		const auto direction = EvaluateMotionChildDirection(scene, selection);
		const auto& layer = scene.Preparation.Layers[selection.LayerIndex];
		const auto& current = layer.Frames[selection.CurrentFrameIndex];
		BindMotionChildPose(child, layer, current, scene.Layers[selection.LayerIndex], direction, color_filter);
	}

} // namespace sgre::scene
