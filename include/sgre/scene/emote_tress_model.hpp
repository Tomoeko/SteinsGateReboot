#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace sgre::scene {
	class EmoteCollider;

	struct EmotePhysicalPosture {
		std::array<float, 3> Coordinate{};
		float Rotation = 0;
		float Cosine = 1;
		float Sine = 0;
	};

	struct EmoteTressSideDefinition {
		float Mass = 0;
		float Length = 0;
		std::uint32_t ParticleCount = 2;
		float TrailingLength = 0;
		std::uint32_t TrailingPoints = 0;
	};

	enum class EmoteTressRigidSide : std::uint8_t {
		None = 0,
		First = 1,
		Second = 2
	};

	struct EmoteTressDefinition {
		float Gravity = 9.8f;
		float GravityRotation = 0;
		float ReferenceRotation = 0;
		EmoteTressRigidSide RigidSide = EmoteTressRigidSide::None;
		EmoteTressSideDefinition First;
		EmoteTressSideDefinition Second;
		float RotationInheritance = 0;
		float Elastic = 0;
		float FrictionBegin = 0;
		float FrictionEnd = 0;
		float ControlRadius = 0;
		float CoordinateRotation = 0;
		std::array<float, 3> CoordinateScale{1, 1, 1};
		bool MassTiltedDistribution = false;
		bool AdhocSimulation = false;
		std::uint32_t CollisionHardness = 0;
		float CollisionRepulsion = 0;
	};

	// Native322ca0: two real physical branches around the same authored String fix.
	// Owns their particles and rigid points; output is first branch reversed, then second with one shared anchor.
	class EmoteTressModel {
	public:
		explicit EmoteTressModel(const EmoteTressDefinition& definition);
		~EmoteTressModel() noexcept;
		EmoteTressModel(const EmoteTressModel&) = delete;
		EmoteTressModel& operator=(const EmoteTressModel&) = delete;
		void SetPosture(const EmotePhysicalPosture& posture);
		void Initialize();
		void ClearForcesAndMeasureBounds();
		void AddForce(bool first_side, std::size_t particle, const std::array<float, 3>& force);
		void AttachCollider(EmoteCollider& collider);
		void Step(float seconds);
		[[nodiscard]] std::size_t PointCount() const noexcept;
		void WritePoints(std::span<std::array<float, 2>> destination) const;
		void WriteSegmentLengths(std::span<float> destination) const;

	private:
		friend class EmoteStringPhysics;
		friend class EmoteTressSpline;
		void StepImpl(float seconds, bool unordered_comparison);
		void WritePointsImpl(std::span<std::array<float, 2>> destination, bool unordered_comparison) const;
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};
} // namespace sgre::scene
