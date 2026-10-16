#include "sgre/scene/motion_root_scene.hpp"

#include <cmath>
#include <stdexcept>

namespace sgre::scene {

	static void RequireRootPose(const MotionRootOverrides& overrides) {
		const std::array<float, 8> values{overrides.Coordinate[0], overrides.Coordinate[1], overrides.Coordinate[2], overrides.Angle, overrides.ZoomX, overrides.ZoomY, overrides.ShearX, overrides.ShearY};
		for (const auto value : values) {
			if (!std::isfinite(value)) {
				throw std::runtime_error("motion root composition requires finite coordinates and pose");
			}
		}

		if (overrides.Opacity > 255) {
			throw std::runtime_error("motion root opacity exceeds its native byte domain");
		}
	}

	MotionRootScene::MotionRootScene()
	    : MotionRootScene(MotionRootMatrixMode::Recomputed) {
	}

	MotionRootScene::MotionRootScene(MotionRootMatrixMode matrix_mode)
	    : m_MatrixMode(matrix_mode) {
		if (matrix_mode != MotionRootMatrixMode::Recomputed && matrix_mode != MotionRootMatrixMode::SuppliedByParent) {
			throw std::runtime_error("unsupported motion root matrix mode");
		}

		// 51a400 initializes evaluated visibility independently of the false script-visible override.
		m_Frame.Hierarchy.Attributes.Hidden = false;
		m_Frame.Hierarchy.Transform.XX = 0;
		m_Frame.Hierarchy.Transform.XY = 0;
		m_Frame.Hierarchy.Transform.YX = 0;
		m_Frame.Hierarchy.Transform.YY = 0;
	}

	MotionRootScene::MotionRootScene(const MotionChildRootConfiguration& configuration)
	    : MotionRootScene(MotionRootMatrixMode::SuppliedByParent) {
		if (configuration.CoordinatePlane > 1) {
			throw std::runtime_error("motion child root requires its recovered XY/XZ coordinate plane");
		}

		for (const auto operation : configuration.TransformOrder) {
			if (static_cast<std::uint32_t>(operation) > 3) {
				throw std::runtime_error("motion child root transform order exceeds its recovered operation domain");
			}
		}

		m_RootTransform.CoordinatePlane = configuration.CoordinatePlane;
		m_RootTransform.TransformOrder = configuration.TransformOrder;
		m_Frame.Hierarchy.CoordinatePlane = configuration.CoordinatePlane;
		m_Priority = configuration.Priority;
	}

	void MotionRootScene::BindChildPose(const MotionChildRootPose& pose) {
		if (m_MatrixMode != MotionRootMatrixMode::SuppliedByParent) {
			throw std::runtime_error("motion child root binding requires its actual parent-supplied matrix owner");
		}

		RequireRootPose(pose.Overrides);
		const std::array<float, 4> matrix{pose.Transform.XX, pose.Transform.XY, pose.Transform.YX, pose.Transform.YY};
		for (const auto value : matrix) {
			if (!std::isfinite(value)) {
				throw std::runtime_error("motion child root binding requires a finite parent matrix");
			}
		}

		m_Overrides = pose.Overrides;
		// 521bf0 always dirties the root after writing even an unchanged supplied matrix.
		m_Overrides.Dirty = true;
		m_ParentTransform = pose.Transform;
		m_ModulationColor = pose.ModulationColor;
		m_ParentPoseBound = true;
		m_Frame.Composed = false;
	}

	void MotionRootScene::SetCoordinate(resource::MotionPoint coordinate) {
		if (m_Overrides.Coordinate[0] != coordinate.X || m_Overrides.Coordinate[1] != coordinate.Y) {
			m_Overrides.Dirty = true;
			m_Overrides.Coordinate[0] = coordinate.X;
			m_Overrides.Coordinate[1] = coordinate.Y;
		}
	}

	void MotionRootScene::SetCoordinate3(const std::array<float, 3>& coordinate) {
		if (m_Overrides.Coordinate != coordinate) {
			m_Overrides.Dirty = true;
			m_Overrides.Coordinate = coordinate;
		}
	}

	void MotionRootScene::SetLeft(float left) {
		resource::MotionPoint coordinate;
		coordinate.X = left;
		coordinate.Y = m_Overrides.Coordinate[1];
		SetCoordinate(coordinate);
	}

	void MotionRootScene::SetTop(float top) {
		resource::MotionPoint coordinate;
		coordinate.X = m_Overrides.Coordinate[0];
		coordinate.Y = top;
		SetCoordinate(coordinate);
	}

	void MotionRootScene::SetDepth(float depth) {
		auto coordinate = m_Overrides.Coordinate;
		coordinate[2] = depth;
		SetCoordinate3(coordinate);
	}

	void MotionRootScene::SetZoom(float horizontal, float vertical) noexcept {
		if (m_Overrides.ZoomX != horizontal || m_Overrides.ZoomY != vertical) {
			m_Overrides.Dirty = true;
			m_Overrides.ZoomX = horizontal;
			m_Overrides.ZoomY = vertical;
		}
	}

	void MotionRootScene::SetAngleDegrees(float degrees) noexcept {
		if (m_Overrides.Angle != degrees) {
			m_Overrides.Dirty = true;
			m_Overrides.Angle = degrees;
		}
	}

	void MotionRootScene::SetAngleRadians(float radians) noexcept {
		const float numerator = radians * 360.0f;
		SetAngleDegrees(numerator / 6.28318548f);
	}

	float MotionRootScene::AngleDegrees() const noexcept {
		return m_Overrides.Angle;
	}

	float MotionRootScene::AngleRadians() const noexcept {
		const float product = m_Overrides.Angle * 3.14159274f;

		return (product + product) / 360.0f;
	}

	void MotionRootScene::SetOpacity(std::uint32_t opacity) noexcept {
		if (m_Overrides.Opacity != opacity) {
			m_Overrides.Dirty = true;
			m_Overrides.Opacity = opacity;
		}
	}

	void MotionRootScene::SetVisible(bool visible) noexcept {
		if (m_Overrides.Visible != visible) {
			m_Overrides.Dirty = true;
			m_Overrides.Visible = visible;
		}
	}

	void MotionRootScene::SetFlip(bool horizontal, bool vertical) noexcept {
		if (m_Overrides.FlipX != horizontal || m_Overrides.FlipY != vertical) {
			m_Overrides.Dirty = true;
			m_Overrides.FlipX = horizontal;
			m_Overrides.FlipY = vertical;
		}
	}

	void MotionRootScene::SetPriority(float priority) noexcept {
		m_Priority = priority;
	}

	void MotionRootScene::SetVelocity(const std::array<float, 3>& velocity) {
		for (const auto component : velocity) {
			if (!std::isfinite(component)) {
				throw std::runtime_error("motion player velocity requires finite components");
			}
		}
		m_Velocity = velocity;
	}

	void MotionRootScene::SetVelocityAttenuation(float attenuation) {
		if (!std::isfinite(attenuation) || attenuation < 0) {
			throw std::runtime_error("motion player attenuation requires its finite nonnegative recovered domain");
		}
		m_VelocityAttenuation = attenuation;
	}

	void MotionRootScene::AdvanceVelocity(float delta_ticks) {
		if (!std::isfinite(delta_ticks)) {
			throw std::runtime_error("motion player velocity requires its finite actual scaled delta");
		}
		auto coordinates = m_Overrides.Coordinate;
		auto velocity = m_Velocity;
		bool moved = false;
		for (std::size_t axis = 0; axis < velocity.size(); ++axis) {
			if (velocity[axis] != 0) {
				const auto displacement = velocity[axis] * delta_ticks;
				coordinates[axis] = displacement + coordinates[axis];
				moved = true;
			}
		}
		if (m_VelocityAttenuation != 1) {
			const auto elapsed_seconds = delta_ticks / 60.0f;
			const auto factor = std::pow(m_VelocityAttenuation, elapsed_seconds);
			if (!std::isfinite(factor)) {
				throw std::runtime_error("motion player attenuation exceeds its finite recovered power domain");
			}
			for (auto& component : velocity) {
				component *= factor;
			}
		}
		for (std::size_t axis = 0; axis < velocity.size(); ++axis) {
			if (!std::isfinite(coordinates[axis]) || !std::isfinite(velocity[axis])) {
				throw std::runtime_error("motion player velocity exceeds finite coordinates or retained velocity");
			}
		}
		m_Overrides.Coordinate = coordinates;
		m_Overrides.Dirty = m_Overrides.Dirty || moved;
		m_Velocity = velocity;
	}

	const std::array<float, 3>& MotionRootScene::Velocity() const noexcept {
		return m_Velocity;
	}

	float MotionRootScene::VelocityAttenuation() const noexcept {
		return m_VelocityAttenuation;
	}

	void MotionRootScene::Compose(bool tick_assigned) {
		m_Frame.Composed = false;
		RequireRootPose(m_Overrides);
		if (m_MatrixMode == MotionRootMatrixMode::SuppliedByParent && !m_ParentPoseBound) {
			throw std::runtime_error("motion child root composition requires actual parent pose binding");
		}

		const auto previous = m_Frame.Hierarchy.Attributes.Coordinate;
		auto& attributes = m_Frame.Hierarchy.Attributes;
		attributes.Coordinate = m_Overrides.Coordinate;
		attributes.Hidden = !m_Overrides.Visible;
		attributes.FlipX = m_Overrides.FlipX;
		attributes.FlipY = m_Overrides.FlipY;
		attributes.Angle = m_Overrides.Angle;
		attributes.ZoomX = m_Overrides.ZoomX;
		attributes.ZoomY = m_Overrides.ZoomY;
		attributes.ShearX = m_Overrides.ShearX;
		attributes.ShearY = m_Overrides.ShearY;
		attributes.Opacity = m_Overrides.Opacity;
		m_Overrides.Dirty = false;
		m_Frame.Hierarchy.Transform = m_MatrixMode == MotionRootMatrixMode::SuppliedByParent ? m_ParentTransform : EvaluateMotionTransform(m_RootTransform, attributes);
		m_Frame.Hierarchy.ModulationColor = m_ModulationColor;
		for (std::size_t index = 0; index < m_Frame.Displacement.size(); ++index) {
			m_Frame.Displacement[index] = tick_assigned ? 0 : attributes.Coordinate[index] - previous[index];
		}

		m_Frame.Composed = true;
	}

	const MotionRootOverrides& MotionRootScene::Overrides() const noexcept {
		return m_Overrides;
	}

	const MotionRootFrame& MotionRootScene::Frame() const noexcept {
		return m_Frame;
	}

	float MotionRootScene::Priority() const noexcept {
		return m_Priority;
	}

	MotionRootMatrixMode MotionRootScene::MatrixMode() const noexcept {
		return m_MatrixMode;
	}

	const std::array<MotionTransformOperation, 4>& MotionRootScene::TransformOrder() const noexcept {
		return m_RootTransform.TransformOrder;
	}

	void MotionRootScene::TransferModulationColor(std::uint32_t color) noexcept {
		// The native caller already compared player ColorFilter; this write always dirties the root.
		m_ModulationColor = color;
		m_Overrides.Dirty = true;
	}

	std::uint32_t MotionRootScene::ModulationColor() const noexcept {
		return m_ModulationColor;
	}

} // namespace sgre::scene
