#pragma once

#include "sgre/resource/motion_mesh.hpp"
#include "sgre/scene/emote_ellipse_collision.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace sgre::scene {
	// Serialized collisionMap shape types, distinct from Motion layer shape snapshots.
	enum class EmoteColliderKind : std::uint32_t {
		HalfPlane = 0,
		SolidEllipse = 1,
		InteriorEllipse = 2
	};

	struct EmoteColliderDefinition {
		EmoteColliderKind Kind = EmoteColliderKind::HalfPlane;
		resource::MotionPoint First;
		resource::MotionPoint Second;
		resource::MotionPoint Radius{1, 1};
		float Rotation = 0;
	};

	struct EmoteColliderPosture {
		resource::MotionPoint Coordinate;
		float Rotation = 0;
		float Cosine = 1;
		float Sine = 0;
	};

	class EmoteCollider {
	public:
		explicit EmoteCollider(EmoteColliderDefinition definition);
		EmoteCollider(const EmoteCollider&) = delete;
		EmoteCollider& operator=(const EmoteCollider&) = delete;
		void SetEnabled(bool enabled) noexcept;
		void SetPosture(const EmoteColliderPosture& posture);
		void ResetMotion();
		void ClearContact() noexcept;
		// Each real entity restarts interpolation from the preceding owner posture, including shared colliders.
		void PrepareSubsteps(std::uint32_t count);
		void AdvanceSubstep();
		[[nodiscard]] bool Project(resource::MotionPoint& point, const resource::MotionPoint& particle_displacement, float particle_radius);
		[[nodiscard]] bool ProjectSection(resource::MotionPoint& projected, const EmoteCollisionEllipse& particle, const resource::MotionPoint& particle_displacement);
		[[nodiscard]] bool Enabled() const noexcept;
		[[nodiscard]] bool Contacted() const noexcept;
		[[nodiscard]] const EmoteColliderPosture& InterpolatedPosture() const noexcept;

	private:
		[[nodiscard]] bool ProjectHalfPlane(resource::MotionPoint& point, float particle_radius) const;
		[[nodiscard]] bool ProjectEllipse(resource::MotionPoint& point, const resource::MotionPoint& particle_displacement, float particle_radius);
		EmoteColliderDefinition m_Definition;
		EmoteColliderPosture m_Previous;
		EmoteColliderPosture m_Current;
		EmoteColliderPosture m_Interpolated;
		resource::MotionPoint m_TranslationStep;
		float m_RotationStep = 0;
		float m_StepCosine = 1;
		float m_StepSine = 0;
		float m_LocalCosine = 1;
		float m_LocalSine = 0;
		std::uint32_t m_StepCount = 0;
		std::uint32_t m_StepIndex = 0;
		std::size_t m_RemainingSamples = 0;
		bool m_Enabled = false;
		bool m_Contacted = false;
	};
} // namespace sgre::scene
