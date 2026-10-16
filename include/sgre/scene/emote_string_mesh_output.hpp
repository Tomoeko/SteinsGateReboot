#pragma once

#include "sgre/resource/motion_mesh.hpp"
#include "sgre/scene/emote_string_physics.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace sgre::scene {
	struct EmoteStringMeshOutputDefinition {
		std::vector<resource::MotionPoint> Fixes;
		std::array<float, 4> RigidRange{0, 0, 1, 1};
		std::uint32_t RigidIterations = 0;
		bool CorrectFix = false;
		bool CorrectScale = false;
	};

	// Entries retain actual serialized String-control order, matching ReadEmoteStringPhysicsDefinitions.
	[[nodiscard]] std::vector<EmoteStringMeshOutputDefinition> ReadEmoteStringMeshOutputDefinitions(const resource::PsbDocument& document);

	class EmoteStringMeshOutput {
	public:
		// The actual initialized physical owner outlives this output. Native309af0 stores identity minus its initial output.
		EmoteStringMeshOutput(EmoteStringPhysics& actual_physics, EmoteStringMeshOutputDefinition definition);
		~EmoteStringMeshOutput() noexcept;
		EmoteStringMeshOutput(const EmoteStringMeshOutput&) = delete;
		EmoteStringMeshOutput& operator=(const EmoteStringMeshOutput&) = delete;
		// Called after the actual entity update. Fix targets are its real transformed/predicted world points, never normalized metadata replacements.
		// The returned lattice borrows this owner until the next preparation; actual UID SetMesh copies its controls into the pending mesh owner.
		[[nodiscard]] const resource::MotionBezierLattice& Prepare(const EmotePhysicalPosture& completed_posture, float correction_angle, float correction_scale, std::span<const resource::MotionPoint> world_fix_targets);

	private:
		friend class EmoteStringControl;
		[[nodiscard]] const resource::MotionBezierLattice& PrepareForUnorderedComparison(const EmotePhysicalPosture& completed_posture, float correction_angle, float correction_scale, std::span<const resource::MotionPoint> world_fix_targets);
		[[nodiscard]] const resource::MotionBezierLattice& PrepareImpl(const EmotePhysicalPosture& completed_posture, float correction_angle, float correction_scale, std::span<const resource::MotionPoint> world_fix_targets, bool unordered_comparison);
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};
} // namespace sgre::scene
