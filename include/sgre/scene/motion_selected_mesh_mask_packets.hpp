#pragma once

#include "sgre/scene/motion_mask_plan.hpp"
#include "sgre/scene/motion_selected_mesh_vertices.hpp"
#include "sgre/scene/motion_stencil_row_policies.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace sgre::scene {

	struct MotionMeshMaskOwner {
		MotionMeshMaskOwner(MotionStencilRowPolicies& rows, MotionSelectedMeshVertices& vertices) : Rows(rows), Vertices(vertices) {
		}
		MotionStencilRowPolicies& Rows;
		MotionSelectedMeshVertices& Vertices;
	};

	class MotionSelectedMeshMaskPackets {
	public:
		// The complete actual selected child graph is bound parent first, before row/vertex preparation.
		// All row, selected, numeric, registered source and vertex owners outlive these packet borrows.
		explicit MotionSelectedMeshMaskPackets(std::span<const MotionMeshMaskOwner> owners);
		~MotionSelectedMeshMaskPackets() noexcept;
		MotionSelectedMeshMaskPackets(const MotionSelectedMeshMaskPackets&) = delete;
		MotionSelectedMeshMaskPackets& operator=(const MotionSelectedMeshMaskPackets&) = delete;
		// Requires genuine current numeric, stencil, prepared rows and vertex composition for each actual context.
		void Build();
		[[nodiscard]] std::span<const MotionClipNode> Nodes() const;
		[[nodiscard]] std::span<const MotionMaskSettings> Settings() const;
		[[nodiscard]] std::size_t Index(const MotionStencilRowState& row) const;

	private:
		struct Binding {
			const MotionStencilRowState* Row = nullptr;
			std::size_t Owner = 0;
			std::uint32_t Layer = 0;
		};
		void RequireOwnerPhase(std::size_t owner) const;
		void RequireLive() const;
		std::vector<MotionMeshMaskOwner> m_Owners;
		std::vector<Binding> m_Bindings;
		std::unordered_map<const MotionStencilRowState*, std::size_t> m_RowIndices;
		std::vector<std::vector<MotionClipCommand>> m_Members;
		std::vector<MotionClipNode> m_Nodes;
		std::vector<MotionMaskSettings> m_Settings;
		std::vector<std::uint64_t> m_PreparationSerials;
		std::vector<std::uint64_t> m_VertexSerials;
		bool m_Built = false;
	};

} // namespace sgre::scene
