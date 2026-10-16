#pragma once

#include "sgre/scene/motion_clip_bounds.hpp"
#include "sgre/scene/motion_selected_mesh_ancestry.hpp"
#include "sgre/scene/motion_tessellation.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace sgre::scene {
	class MotionStencilRowPolicies;

	class MotionSelectedMeshVertices {
	public:
		// The exact ancestry, selected/local/source and containing owners outlive all vertex and clip borrows.
		explicit MotionSelectedMeshVertices(MotionSelectedMeshAncestry& ancestry);
		~MotionSelectedMeshVertices() noexcept;
		MotionSelectedMeshVertices(const MotionSelectedMeshVertices&) = delete;
		MotionSelectedMeshVertices& operator=(const MotionSelectedMeshVertices&) = delete;
		// Consumes the completed actual current ancestry, never a caller's frame/space/ready snapshot.
		void Compose();
		// Native527070 point evaluation uses the actual current ancestry without a source rectangle or vertex allocation.
		[[nodiscard]] resource::MotionPoint DeformedCoordinate(std::uint32_t layer) const;
		// Native50f6b0 transforms caller-owned points by this numeric matrix, then ancestors (not this own patch).
		void TransformPoints(std::uint32_t layer, std::span<resource::MotionPoint> points) const;
		[[nodiscard]] float DeformationScale(std::uint32_t layer) const;
		[[nodiscard]] bool GeometryChanged(std::uint32_t layer) const;
		[[nodiscard]] const MotionSelectedLayerNode& Node(std::uint32_t layer) const;
		[[nodiscard]] bool Allocated(std::uint32_t layer) const;
		[[nodiscard]] MotionClipGeometry& Geometry(std::uint32_t layer);
		[[nodiscard]] const MotionMeshSubdivision& Subdivision(std::uint32_t layer) const;

	private:
		friend class MotionSceneEvaluation;
		friend class MotionSelectedMeshMaskPackets;
		friend class MotionStencilRowPolicies;
		struct Entry {
			const MotionSelectedLayerNode* Node = nullptr;
			std::vector<resource::MotionPoint> Positions;
			MotionClipGeometry Geometry;
			MotionMeshSubdivision Subdivision;
			MotionMeshDivisionCache Division;
			bool Allocated = false;
		};
		void RequireOwners() const;
		void RequirePhase(std::uint32_t layer) const;
		[[nodiscard]] const MotionSelectedDeformationState* NextAncestor(const MotionSelectedDeformationState& state) const;
		[[nodiscard]] bool CombinedChainReaches(const MotionSelectedDeformationState& state, const MotionSelectedDeformationState& ancestor) const;
		[[nodiscard]] MotionMeshSubdivision SelectSubdivision(const MotionSelectedDeformationState& state, float ratio, MotionMeshDivisionCache& cache) const;
		[[nodiscard]] resource::MotionPoint BoundaryOffset(const MotionSelectedDeformationState& state) const;
		void AddBoundaryOffsets(const MotionSelectedDeformationState& state, std::span<resource::MotionPoint> positions) const;
		void EvaluateVertices(const MotionSelectedDeformationState& state, const MotionMeshSubdivision& subdivision, std::span<resource::MotionPoint> positions) const;
		MotionStencilRowPolicies* m_RowOwner = nullptr;
		std::uint64_t m_RowPreparationSerial = 0;
		std::uint64_t m_VertexSerial = 0;
		MotionSelectedMeshAncestry& m_Ancestry;
		MotionSelectedLayerNodes& m_Selected;
		std::vector<Entry> m_Entries;
		std::uint64_t m_PreparationSerial = 0;
		bool m_Composed = false;
	};

} // namespace sgre::scene
