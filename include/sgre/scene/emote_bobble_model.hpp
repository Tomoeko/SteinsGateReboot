#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace sgre::scene {
	struct EmoteBobbleDefinition {
		bool Enabled = true;
		float Gravity = 1;
		float ReferenceRotation = 0;
		std::array<float, 3> Direction{0, 1, 0};
		float Length = 1;
		float MassScale = 1;
		// Native Jelly produces one mass weight per actual sampled cross-section.
		std::vector<float> MassWeights;
		float AlignmentSpring = 0;
		float LengthSpring = 0;
		float ControlRadius = 1;
		float CollisionRepulsion = 100;
		float FrictionBegin = 0;
		float FrictionEnd = 0;
		bool AdhocSimulation = false;
		float CoordinateRotation = 0;
		std::array<float, 3> CoordinateScale{1, 1, 1};
	};

	struct EmoteBobblePosture {
		std::array<float, 3> Coordinate{};
		float Rotation = 0;
	};

	struct EmoteBobbleParticle {
		std::array<float, 3> Previous{};
		std::array<float, 3> Predicted{};
		std::array<float, 3> Position{};
		std::array<float, 3> Velocity{};
		std::array<float, 3> Force{};
		float Friction = 0;
		float Mass = 0;
		float InverseMass = 0;
		bool Fixed = false;
		float AlignmentSpring = 0;
		float LengthSpring = 0;
	};

	class EmoteBobbleSections;
	struct EmoteBobbleSection;
	struct EmoteBobbleSectionDefinition;
	class EmoteCollider;
	class EmoteJellySectionRings;

	class EmoteBobbleModel {
	public:
		// Numerical-only instances have no collider geometry. The shape-aware constructor owns its completed325fc0 cache.
		explicit EmoteBobbleModel(const EmoteBobbleDefinition& definition);
		EmoteBobbleModel(const EmoteBobbleDefinition& definition, const EmoteBobbleSectionDefinition& sections);
		~EmoteBobbleModel() noexcept;
		EmoteBobbleModel(const EmoteBobbleModel&) = delete;
		EmoteBobbleModel& operator=(const EmoteBobbleModel&) = delete;
		void SetPosture(const EmoteBobblePosture& posture);
		// Native325e80 consumes these actual retained cosine/sine fields directly.
		void SetPosture(const EmoteBobblePosture& posture, float cosine, float sine);
		void Initialize();
		void ClearForces();
		// The actual collider outlives this model, including between completed solver steps.
		void AttachCollider(EmoteCollider& collider);
		void AddForce(std::size_t particle, const std::array<float, 3>& force);
		void Step(float seconds);
		void WritePoints(std::span<std::array<float, 2>> destination) const;
		[[nodiscard]] std::span<const EmoteBobbleParticle> Particles() const noexcept;

	private:
		friend class EmoteBobbleSections;
		friend class EmoteJellyPhysics;
		void StepForUnorderedJellyComparison(float seconds);
		void StepImpl(float seconds, bool unordered_comparison);
		friend class EmoteJellySectionRings;
		void RequireIdle() const;
		void AddAccelerationAndSprings(float seconds);
		void ApplyColliders(float seconds, std::span<const EmoteBobbleSection> completed_sections);
		void RefreshCompletedSections();
		void ProjectAdhocVelocities(float seconds);
		void SolveCompliantLengths(float seconds);
		void SolveAdhocLengths();
		EmoteBobbleDefinition m_Definition;
		std::vector<EmoteBobbleParticle> m_Particles;
		std::vector<float> m_LengthLambdas;
		std::array<float, 3> m_Acceleration{};
		std::array<float, 3> m_Direction{};
		std::array<float, 3> m_Anchor{};
		std::array<float, 2> m_CoordinateOffset{};
		std::array<float, 4> m_CoordinateMatrix{};
		float m_SegmentLength = 0;
		float m_MaximumLength = 0;
		float m_MaximumSquared = 0;
		float m_PostureRotation = 0;
		bool m_PostureSet = false;
		bool m_Initialized = false;
		// Only the enclosing Jelly zero-time transaction may retain native unordered numeric output.
		bool m_UnorderedJellyComparison = false;
		bool m_Operating = false;
		bool m_Valid = true;
		std::vector<EmoteCollider*> m_Colliders;
		// Borrows this nonmoving model and dies before its particle storage.
		std::unique_ptr<EmoteBobbleSections> m_CompletedSections;
	};
} // namespace sgre::scene
