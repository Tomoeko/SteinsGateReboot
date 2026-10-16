#pragma once

#include "sgre/resource/motion_mesh.hpp"
#include "sgre/scene/emote_soft_pendulum.hpp"
#include "sgre/scene/emote_tress_model.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace sgre::scene {
	class EmoteCollider;
	struct EmoteCatenaryDefinition {
		std::string Uid;
		std::uint32_t Substeps = 1;
		float Speed = 1;
		float Gravity = 0;
		float GravityRotation = 0;
		float DefaultRotation = 0;
		float Mass = 0;
		float Straightness = 0;
		float SizeHorizontal = 0;
		float SizeVertical = 0;
		float Length = 0;
		resource::MotionPoint Origin;
		std::array<resource::MotionPoint, 2> Fixes{};
		std::uint32_t ParticleCount = 3;
		std::vector<resource::MotionPoint> RestPoints;
		std::uint32_t DeformIterations = 0;
		float ControlRadius = 0;
		float FrictionTop = 0;
		float FrictionBottom = 0;
		float BottomRatio = 0;
		float Elastic = 0;
		std::array<float, 3> CoordinateScale{};
		float CollisionRepulsion = 100;
		std::uint32_t CollisionHardness = 0;
		bool AdhocSimulation = true;
		bool MassTiltedDistribution = false;
		bool CorrectFix = false;
		bool CorrectScale = false;
	};

	// Native306190 owns one genuine two-fixed-end model and an authored rest-curve constraint catalogue.
	class EmoteCatenaryPhysics {
	public:
		explicit EmoteCatenaryPhysics(EmoteCatenaryDefinition definition);
		~EmoteCatenaryPhysics() noexcept;
		EmoteCatenaryPhysics(const EmoteCatenaryPhysics&) = delete;
		EmoteCatenaryPhysics& operator=(const EmoteCatenaryPhysics&) = delete;
		// Native assignment copies mutable values while retaining this owner's resources and bindings.
		void RequireCanAssignState(const EmoteCatenaryPhysics& source) const;
		void AssignState(const EmoteCatenaryPhysics& source);
		void SetPosture(const EmotePhysicalPosture& posture);
		void SetCorrection(float angle, float scale);
		void SetWorldFixes(std::span<const resource::MotionPoint> targets);
		void Initialize();
		void ClearForcesAndMeasureBounds();
		void AddForce(std::size_t particle, const std::array<float, 3>& force);
		// The actual containing collision group keeps each borrowed collider alive through this entity.
		void AttachCollider(EmoteCollider& collider);
		// Native307b70 includes its actual first-use settling branch and ordinary interpolated substeps.
		void Update(float elapsed_ticks);
		// Native307460 returns the actual normalized cubic/quartic lattice. Borrow expires on preparation/update.
		[[nodiscard]] const resource::MotionBezierLattice& PrepareLattice();
		[[nodiscard]] const EmoteCatenaryDefinition& Definition() const noexcept;
		[[nodiscard]] std::span<const EmoteSoftPendulumParticle> Particles() const noexcept;

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};
} // namespace sgre::scene
