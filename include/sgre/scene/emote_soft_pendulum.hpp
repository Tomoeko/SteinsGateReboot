#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace sgre::scene {
	class EmoteCollider;

	struct EmoteSoftPendulumDefinition {
		std::array<float, 3> Acceleration{};
		float Mass = 1;
		float Elastic = 0;
		float RotationInheritance = 0;
		float Length = 1;
		std::uint32_t ParticleCount = 2;
		float ControlRadius = 0;
		float CoordinateRotation = 0;
		std::array<float, 3> CoordinateScale{1, 1, 1};
		float ReferenceRotation = 0;
		bool MassTiltedDistribution = false;
		float TrailingLength = 0;
		std::uint32_t TrailingPoints = 0;
		float FrictionBegin = 0;
		float FrictionEnd = 0;
		bool AdhocSimulation = false;
		// Native31f520 fixes both endpoints and uses the native bottom-centred friction profile.
		bool FixedTip = false;
		float BottomRatio = 0;
		std::uint32_t CollisionHardness = 0;
		float CollisionRepulsion = 0;
	};

	struct EmoteSoftPendulumPosture {
		std::array<float, 3> Coordinate{};
		float Rotation = 0;
	};

	struct EmoteSoftPendulumParticle {
		bool Fixed = false;
		bool Collided = false;
		double Mass = 0;
		double InverseMass = 0;
		std::array<double, 3> Previous{};
		std::array<double, 3> Predicted{};
		std::array<double, 3> Position{};
		std::array<double, 3> Velocity{};
		std::array<double, 3> Force{};
		double Friction = 0;
		// Native particle+98 retains the preceding segment projected onto the actual acceleration.
		double AccelerationProjection = 0;
	};

	struct EmoteSoftPendulumBounds {
		double Left = 0;
		double Top = 0;
		double Right = 0;
		double Bottom = 0;
	};

	class EmoteSoftPendulum {
	public:
		// Native31f760 one-fixed-end physical model used by actual String tresses.
		// Definition values are actual producer inputs, not captured particle state.
		explicit EmoteSoftPendulum(const EmoteSoftPendulumDefinition& definition);
		EmoteSoftPendulum(const EmoteSoftPendulum&) = delete;
		EmoteSoftPendulum& operator=(const EmoteSoftPendulum&) = delete;
		// Native assignment copies mutable values while retaining this owner's resources and bindings.
		void RequireCanAssignState(const EmoteSoftPendulum& source) const;
		void AssignState(const EmoteSoftPendulum& source);
		void SetPosture(const EmoteSoftPendulumPosture& posture);
		// Native322190 supplies the real two endpoints after SetPosture; no endpoint is synthesized.
		void SetFixedEnds(const std::array<float, 2>& first, const std::array<float, 2>& last);
		void Initialize();
		// Entity reset/force collection precedes its native physical-model Step.
		void ClearForcesAndMeasureBounds();
		void AddForce(std::size_t particle, const std::array<float, 3>& force);
		// The containing entity owns collider interpolation; every borrowed collider encloses this model.
		void AttachCollider(EmoteCollider& collider);
		// Actual entity substeps supply positive seconds after ticks/60/substepCount.
		// The containing entity advances the real borrowed colliders before this physical substep.
		void Step(float seconds);
		void WritePoints(std::span<std::array<float, 2>> destination) const;
		void WriteSegmentLengths(std::span<float> destination) const;
		[[nodiscard]] std::span<const EmoteSoftPendulumParticle> Particles() const noexcept;
		[[nodiscard]] std::span<const std::array<double, 3>> TrailingPositions() const noexcept;
		[[nodiscard]] const EmoteSoftPendulumBounds& Bounds() const;

	private:
		friend class EmoteTressModel;
		void StepForUnorderedStringComparison(float seconds);
		void StepImpl(float seconds, bool unordered_comparison);
		void WritePointsImpl(std::span<std::array<float, 2>> destination, bool unordered_comparison) const;
		void RequireIdle() const;
		void InitializeCatenary();
		void AddAcceleration(double seconds);
		void SolveLengths(double seconds);
		void ProjectAdhocVelocities(double seconds);
		void SolveAdhocLengths();
		void ApplyCollisions(bool velocity_response, double seconds);
		void UpdateTrailingPositions(bool from_last_particle);
		EmoteSoftPendulumDefinition m_Definition;
		std::vector<EmoteSoftPendulumParticle> m_Particles;
		std::vector<double> m_LengthLambdas;
		std::vector<std::array<double, 3>> m_TrailingPositions;
		std::vector<EmoteCollider*> m_Colliders;
		std::array<double, 3> m_InitialAcceleration{};
		std::array<double, 3> m_Acceleration{};
		std::array<double, 3> m_Anchor{};
		std::array<double, 2> m_CoordinateOffset{};
		std::array<double, 4> m_CoordinateMatrix{};
		double m_ReciprocalAccelerationLength = 0;
		double m_SegmentLength = 0;
		double m_SegmentSquared = 0;
		double m_ElasticGain = 0;
		double m_TrailingSpacing = 0;
		float m_PostureRotation = 0;
		EmoteSoftPendulumBounds m_Bounds;
		bool m_DirectionPrepared = false;
		bool m_PostureSet = false;
		bool m_Initialized = false;
		bool m_EndsSet = false;
		bool m_BoundsReady = false;
		bool m_UnorderedStringComparison = false;
		bool m_Operating = false;
		bool m_Valid = true;
	};
} // namespace sgre::scene
