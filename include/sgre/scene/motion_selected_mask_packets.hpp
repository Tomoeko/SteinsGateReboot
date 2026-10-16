#pragma once

#include "sgre/scene/motion_mask_plan.hpp"
#include "sgre/scene/motion_selected_quad_geometry.hpp"
#include "sgre/scene/motion_stencil_row_policies.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace sgre::scene {

	struct MotionMaskOwnerFrame {
		MotionMaskOwnerFrame(MotionStencilRowPolicies& rows, MotionSelectedQuadGeometry& geometry, std::span<const MotionComposedLayer> numeric) : Rows(rows), Geometry(geometry), Numeric(numeric) {
		}
		MotionStencilRowPolicies& Rows;
		MotionSelectedQuadGeometry& Geometry;
		std::span<const MotionComposedLayer> Numeric;
	};

	class MotionSelectedMaskPackets {
	public:
		// Exact row/geometry/selected owners outlive these packets. Contexts contain the complete bound child graph, parent first.
		// Numeric spans are the exact completed row and geometry phase and remain valid through packet consumption.
		explicit MotionSelectedMaskPackets(std::span<const MotionMaskOwnerFrame> owners);
		MotionSelectedMaskPackets(const MotionSelectedMaskPackets&) = delete;
		MotionSelectedMaskPackets& operator=(const MotionSelectedMaskPackets&) = delete;
		void Build(std::span<const MotionMaskOwnerFrame> owners);
		// These views borrow this completed preparation; the next owner preparation invalidates them.
		[[nodiscard]] std::span<const MotionClipNode> Nodes() const;
		[[nodiscard]] std::span<const MotionMaskSettings> Settings() const;
		[[nodiscard]] std::size_t Index(const MotionStencilRowState& row) const;

	private:
		struct Binding {
			const MotionStencilRowState* Row = nullptr;
			MotionSelectedQuadGeometry* Geometry = nullptr;
			std::size_t Owner = 0;
			std::size_t Layer = 0;
		};
		void RequireLive() const;
		std::vector<MotionStencilRowPolicies*> m_Owners;
		std::vector<std::uint64_t> m_PreparationSerials;
		std::unordered_map<const MotionStencilRowState*, std::size_t> m_RowIndices;
		std::vector<Binding> m_Bindings;
		std::vector<MotionClipNode> m_Nodes;
		std::vector<MotionMaskSettings> m_Settings;
		std::vector<std::vector<MotionClipCommand>> m_Members;
		bool m_Built = false;
	};

} // namespace sgre::scene
