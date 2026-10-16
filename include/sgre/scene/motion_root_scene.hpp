#pragma once

#include "sgre/scene/motion_child_configuration.hpp"
#include "sgre/scene/motion_evaluator.hpp"

#include <array>
#include <cstdint>

namespace sgre::scene {

	enum class MotionRootMatrixMode {
		Recomputed,
		SuppliedByParent
	};

	struct MotionRootOverrides {
		std::array<float, 3> Coordinate{};
		float Angle = 0;
		float ZoomX = 1;
		float ZoomY = 1;
		float ShearX = 0;
		float ShearY = 0;
		std::uint32_t Opacity = 255;
		bool Visible = false;
		bool FlipX = false;
		bool FlipY = false;
		bool Dirty = true;
	};

	struct MotionChildRootPose {
		MotionRootOverrides Overrides;
		MotionLinearTransform Transform;
		std::uint32_t ModulationColor = 0x808080ff;
	};

	struct MotionRootFrame {
		MotionHierarchyState Hierarchy;
		std::array<float, 3> Displacement{};
		bool Composed = false;
	};

	class MotionRootScene {
	public:
		// Recovers the real root node's initial overrides and composition input, not a selected layer producer.
		MotionRootScene();
		// The actual kind3 child constructor sets its player +419 to preserve the supplied root matrix.
		explicit MotionRootScene(MotionRootMatrixMode matrix_mode);
		// Actual selected-child construction copies root plane/order and parent priority before any binding.
		explicit MotionRootScene(const MotionChildRootConfiguration& configuration);
		// Numeric parent binding precedes the child's actual update; inherited native owner identities are separate required phases.
		void BindChildPose(const MotionChildRootPose& pose);
		void SetCoordinate(resource::MotionPoint coordinate);
		void SetCoordinate3(const std::array<float, 3>& coordinate);
		void SetLeft(float left);
		void SetTop(float top);
		void SetDepth(float depth);
		// Native51fcb0 stores both raw scale factors when either ordered comparison differs.
		void SetZoom(float horizontal, float vertical) noexcept;
		// Native51f160 stores the complete integer word; composition admission remains separate.
		void SetOpacity(std::uint32_t opacity) noexcept;
		// Ordinary player +160=false retains raw degrees; radians use the registered wrapper's separate float32 operations.
		void SetAngleDegrees(float degrees) noexcept;
		void SetAngleRadians(float radians) noexcept;
		[[nodiscard]] float AngleDegrees() const noexcept;
		[[nodiscard]] float AngleRadians() const noexcept;
		void SetVisible(bool visible) noexcept;
		void SetFlip(bool horizontal, bool vertical) noexcept;
		void SetPriority(float priority) noexcept;
		// Native522ab0 transfers the actual emitter modulation to its ordinary emitted root.
		void TransferModulationColor(std::uint32_t color) noexcept;
		// Native50c550 advances actual player velocity before root composition; delta is scaled frame ticks.
		void SetVelocity(const std::array<float, 3>& velocity);
		void SetVelocityAttenuation(float attenuation);
		void AdvanceVelocity(float delta_ticks);
		[[nodiscard]] const std::array<float, 3>& Velocity() const noexcept;
		[[nodiscard]] float VelocityAttenuation() const noexcept;
		// Native composition consumes root overrides before clearing the player's assigned-tick flag.
		void Compose(bool tick_assigned);
		[[nodiscard]] const MotionRootOverrides& Overrides() const noexcept;
		[[nodiscard]] const MotionRootFrame& Frame() const noexcept;
		[[nodiscard]] float Priority() const noexcept;
		[[nodiscard]] MotionRootMatrixMode MatrixMode() const noexcept;
		[[nodiscard]] const std::array<MotionTransformOperation, 4>& TransformOrder() const noexcept;
		[[nodiscard]] std::uint32_t ModulationColor() const noexcept;

	private:
		friend class MotionPlayerTask;
		MotionRootOverrides m_Overrides;
		std::array<float, 3> m_Velocity{};
		float m_VelocityAttenuation = 1;
		MotionRootFrame m_Frame;
		PreparedMotionLayer m_RootTransform;
		float m_Priority = 0;
		MotionRootMatrixMode m_MatrixMode = MotionRootMatrixMode::Recomputed;
		MotionLinearTransform m_ParentTransform;
		std::uint32_t m_ModulationColor = 0x808080ff;
		bool m_ParentPoseBound = false;
	};

} // namespace sgre::scene
