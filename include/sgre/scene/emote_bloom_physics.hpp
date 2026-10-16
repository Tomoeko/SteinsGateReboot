#pragma once

#include "sgre/resource/motion_mesh.hpp"
#include "sgre/scene/emote_tress_model.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace sgre::scene {

	struct EmoteBloomDefinition {
		std::uint32_t Substeps = 1;
		float DefaultRotation = 0;
		float Speed = 1;
		float Mass = 1;
		float LimitLengthRatio = 2;
		float SizeHorizontal = 1;
		float SizeVertical = 1;
		resource::MotionPoint Origin{0.5f, 0.5f};
		resource::MotionPoint Center{0.5f, 0.5f};
		float Elastic = 0;
		float Friction = 0;
		float RotateWeight = 0;
		float TwistWeight = 0;
		float TrapeziumWeight = 0;
		float PitchWeight = 0;
		float ControlPointSize = 0;
		std::array<float, 3> CoordinateScale{1, 1, 1};
		float CollisionRepulsion = 100;
		std::uint32_t CollisionHardness = 0;
		float Straightness = 0;
		std::vector<resource::MotionPoint> Tresses;
		std::vector<resource::MotionPoint> Fixes;
		bool CorrectScale = false;
		bool CorrectFix = false;
	};

	struct EmoteBloomPoint {
		resource::MotionPoint Relative;
		resource::MotionPoint Direction;
		std::array<float, 3> Rest{};
		std::array<float, 3> Target{};
		std::array<float, 3> Previous{};
		std::array<float, 3> Predicted{};
		std::array<float, 3> Coordinate{};
		std::array<float, 3> Velocity{};
		std::array<float, 3> Force{};
		float Limit = 0;
		std::array<float, 16> Influences{};
	};

	// Native3025a0 /3045a0: a distinct float point-spring entity and its real cubic-grid influence owner.
	// The containing actual UID driver supplies posture/forces after plain Progress.
	class EmoteBloomPhysics {
	public:
		explicit EmoteBloomPhysics(EmoteBloomDefinition definition);
		~EmoteBloomPhysics() noexcept;
		EmoteBloomPhysics(const EmoteBloomPhysics&) = delete;
		EmoteBloomPhysics& operator=(const EmoteBloomPhysics&) = delete;
		void SetPosture(const EmotePhysicalPosture& actual_posture);
		void Initialize();
		void ClearForces();
		void AddForce(std::size_t point, const std::array<float, 3>& actual_force);
		void Update(float elapsed_ticks);
		[[nodiscard]] std::span<const resource::MotionPoint> PrepareSamples();
		[[nodiscard]] const resource::MotionBezierLattice& PrepareLattice();
		[[nodiscard]] const resource::MotionBezierLattice& PrepareLattice(float correction_angle, float correction_scale, std::span<const resource::MotionPoint> world_fix_targets);
		[[nodiscard]] std::span<const EmoteBloomPoint> Points() const noexcept;
		[[nodiscard]] const EmoteBloomDefinition& Definition() const noexcept;

	private:
		friend class EmoteBloomControl;
		// Only the actual immutable Speed0/no-collider UID transaction can consume unordered transient physical state.
		void UpdateForUnorderedComparison(float elapsed_ticks);
		[[nodiscard]] const resource::MotionBezierLattice& PrepareLatticeForUnorderedComparison(float correction_angle, float correction_scale, std::span<const resource::MotionPoint> world_fix_targets);
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::scene
