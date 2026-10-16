#pragma once

#include "sgre/resource/motion_mesh.hpp"
#include "sgre/scene/emote_tress_model.hpp"

#include <array>
#include <cstddef>
#include <span>

namespace sgre::scene {
	class EmoteMeshFixAlignment {
	public:
		// Native 2fdd50 retains authored fix geometry in the control's physical dimensions.
		EmoteMeshFixAlignment(std::span<const resource::MotionPoint> fixes, float width, float height);
		void Normalize(resource::MotionBezierLattice& lattice, const EmotePhysicalPosture& posture, const resource::MotionMeshMatrix& world_to_normalized) const;
		void Correct(resource::MotionBezierLattice& lattice, std::span<const resource::MotionPoint> world_targets, const resource::MotionMeshMatrix& world_to_normalized) const;

	private:
		friend class EmoteJellyPhysics;
		friend class EmoteBloomPhysics;
		friend class EmoteStringMeshOutput;
		void NormalizeForUnorderedComparison(resource::MotionBezierLattice& lattice, const EmotePhysicalPosture& posture, const resource::MotionMeshMatrix& world_to_normalized) const;
		void CorrectForUnorderedComparison(resource::MotionBezierLattice& lattice, std::span<const resource::MotionPoint> world_targets, const resource::MotionMeshMatrix& world_to_normalized) const;
		void NormalizeImpl(resource::MotionBezierLattice& lattice, const EmotePhysicalPosture& posture, const resource::MotionMeshMatrix& world_to_normalized, bool unordered_comparison) const;
		void CorrectImpl(resource::MotionBezierLattice& lattice, std::span<const resource::MotionPoint> world_targets, const resource::MotionMeshMatrix& world_to_normalized, bool unordered_comparison) const;
		std::array<resource::MotionPoint, 3> m_Fixes{};
		std::size_t m_Count = 0;
		float m_ReferenceLength = 0;
		resource::MotionMeshMatrix m_Reference;
		resource::MotionPoint m_ReferenceThird;
	};
} // namespace sgre::scene
