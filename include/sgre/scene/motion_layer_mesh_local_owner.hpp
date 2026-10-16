#pragma once

#include "sgre/scene/motion_evaluator.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace sgre::scene {
	class MotionLocalFrames;
	class MotionSelectedLayerNodes;
	struct MotionLocalFrameState;

	struct MotionMeshDecodedSlot {
		std::optional<std::size_t> FrameIndex;
		resource::MotionDeformation Deformation;
	};

	class MotionLayerMeshLocalOwner {
	public:
		// Construction belongs to the actual selected cell; getters reject any unbound duplicate.
		MotionLayerMeshLocalOwner(MotionSelectedLayerNodes& selected, std::uint32_t layer);
		MotionLayerMeshLocalOwner(MotionSelectedLayerNodes& selected, std::uint32_t layer, const MotionLayerMeshLocalOwner& source);
		MotionLayerMeshLocalOwner(const MotionLayerMeshLocalOwner&) = delete;
		MotionLayerMeshLocalOwner& operator=(const MotionLayerMeshLocalOwner&) = delete;
		[[nodiscard]] const MotionMeshDecodedSlot& Slot(std::size_t slot) const;
		// These borrowed controls expire at the next genuine local evaluation or selected retirement.
		[[nodiscard]] const std::optional<resource::MotionBezierPatch>& OwnPatch() const;
		// This is the actual current/pending node grid, independent of serialized cubic numeric trace storage.
		[[nodiscard]] const resource::MotionBezierLattice& CurrentLattice() const;
		[[nodiscard]] bool OverrideLatched() const;
		[[nodiscard]] resource::MotionPoint PixelOrigin() const;
		[[nodiscard]] resource::MotionPoint PhysicalOrigin() const;
		[[nodiscard]] bool PendingWrite() const;
		[[nodiscard]] std::size_t EvaluatedFrameIndex() const;
		[[nodiscard]] float EvaluatedPosition() const;

	private:
		friend class MotionLocalFrames;
		friend class MotionLayerUidReference;
		bool AssignLattice(const resource::MotionBezierLattice* lattice, bool compare);
		bool AssignComparedNativeOutput(const resource::MotionBezierLattice& lattice);
		void AssignOrigins(const resource::MotionPoint& pixel_origin, const resource::MotionPoint& physical_origin);
		void CompleteComposition();
		void RequireLive() const;
		void PrepareFrame(std::size_t slot, std::size_t frame);
		void ApplyLocalEvaluation(const MotionLocalFrameState& state, float position, const MotionLayerEvaluationCache& evaluation, bool changed);
		MotionSelectedLayerNodes& m_Selected;
		std::uint32_t m_Layer = 0;
		std::array<MotionMeshDecodedSlot, 2> m_Slots;
		std::optional<resource::MotionBezierPatch> m_OwnPatch;
		resource::MotionBezierLattice m_CurrentLattice;
		resource::MotionPoint m_PixelOrigin;
		resource::MotionPoint m_PhysicalOrigin;
		bool m_OverrideLatched = false;
		bool m_PendingWrite = false;
		std::size_t m_EvaluatedFrame = 0;
		float m_EvaluatedPosition = 0;
		bool m_Evaluated = false;
	};

} // namespace sgre::scene
