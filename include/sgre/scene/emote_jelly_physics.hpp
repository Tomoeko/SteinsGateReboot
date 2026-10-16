#pragma once

#include "sgre/resource/motion_mesh.hpp"
#include "sgre/scene/emote_bobble_model.hpp"
#include "sgre/scene/emote_jelly_surface_samples.hpp"
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

	struct EmoteJellyTressDefinition {
		resource::MotionPoint Coordinate{0.5f, 0.5f};
		float Radius = 0.25f;
		std::array<float, 3> Tilt{0, 0, 1};
	};

	struct EmoteJellyDefinition {
		std::string Uid;
		std::uint32_t Substeps = 1;
		float Speed = 1;
		float Gravity = 9.8f;
		bool AutoMass = true;
		float Mass = 1;
		float Density = 1;
		float SizeHorizontal = 1;
		float SizeVertical = 1;
		std::uint32_t ParticleCount = 3;
		float DefaultRotation = 0;
		float TiltFactor = 0;
		float StretchFactor = 0;
		float HeightRate = 0.5f;
		float Friction = 0;
		// Native metadata reads elasticH for both actual model spring coefficients.
		float ElasticHorizontal = 0;
		float ElasticVertical = 0;
		std::array<float, 3> CoordinateScale{1, 1, 1};
		float Sensitivity = 0;
		float CollisionRepulsion = 100;
		bool AdhocSimulation = true;
		float SampleDensity = 2;
		float LimitLengthRatio = 2;
		bool ReverseSampleOrder = true;
		bool CorrectFix = false;
		bool CorrectScale = false;
		std::vector<resource::MotionPoint> Fixes;
		std::array<float, 3> TipTilt{0, 0, 1};
		std::array<float, 4> RigidRange{0, 0, 1, 1};
		std::vector<EmoteJellyTressDefinition> Tresses;
	};

	// Native310740 owns each real tress model, its N+1 rings and all shared fitting constraints.
	// The containing UID driver supplies actual posture/forces and copies the resulting pending lattice.
	class EmoteJellyPhysics {
	public:
		explicit EmoteJellyPhysics(EmoteJellyDefinition definition);
		~EmoteJellyPhysics() noexcept;
		EmoteJellyPhysics(const EmoteJellyPhysics&) = delete;
		EmoteJellyPhysics& operator=(const EmoteJellyPhysics&) = delete;
		void SetPosture(const EmotePhysicalPosture& posture);
		void Initialize();
		void ClearForces();
		// Actual group leases enclose this owner; the native mask selects existing tresses without renumbering.
		void AttachCollider(EmoteCollider& collider, std::uint32_t tress_mask);
		void AddForce(std::size_t tress, std::size_t particle, const std::array<float, 3>& force);
		// Actual entity delta is in60Hz ticks, converted to solver seconds after native subdivision.
		void Update(float elapsed_ticks);
		[[nodiscard]] std::span<const resource::MotionPoint> PrepareSamples();
		[[nodiscard]] const resource::MotionBezierLattice& PrepareLattice(float correction_angle, float correction_scale, std::span<const resource::MotionPoint> world_fix_targets);
		[[nodiscard]] const EmoteJellyDefinition& Definition() const noexcept;
		[[nodiscard]] std::uint8_t EncodedDegree() const noexcept;
		[[nodiscard]] resource::MotionPoint Origin() const noexcept;
		[[nodiscard]] std::size_t TressCount() const noexcept;
		[[nodiscard]] std::span<const resource::MotionPoint> InitialRings(std::size_t tress) const;
		[[nodiscard]] std::span<const EmoteJellyRingSample> Bindings(std::size_t tress) const;
		[[nodiscard]] std::span<const EmoteBobbleParticle> Particles(std::size_t tress) const;
		[[nodiscard]] std::span<const resource::MotionPoint> PreparedRings(std::size_t tress) const;
		[[nodiscard]] std::span<const resource::MotionPoint> PreparedTargets(std::size_t tress) const;

	private:
		friend class EmoteJellyControl;
		void UpdateForUnorderedComparison(float elapsed_ticks);
		[[nodiscard]] const resource::MotionBezierLattice& PrepareLatticeForUnorderedComparison(float correction_angle, float correction_scale, std::span<const resource::MotionPoint> world_fix_targets);
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::scene
