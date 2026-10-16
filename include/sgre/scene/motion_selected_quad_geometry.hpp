#pragma once

#include "sgre/scene/motion_clip_bounds.hpp"
#include "sgre/scene/motion_layer_composition.hpp"
#include "sgre/scene/motion_selected_layer_nodes.hpp"
#include "sgre/scene/motion_static_source.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace sgre::scene {

	class MotionStencilRowPolicies;

	class MotionSelectedQuadGeometry {
	public:
		// Actual selected/prepared/source/numeric owners outlive this exclusive geometry and every row borrowing it.
		// This complete quad family derives absent inherited deformation only from its exact undeformed selected ancestors.
		MotionSelectedQuadGeometry(MotionSelectedLayerNodes& selected, MotionStencilRowPolicies& rows, const MotionSelectedQuadGeometry* containing = nullptr);
		MotionSelectedQuadGeometry(const MotionSelectedQuadGeometry&) = delete;
		MotionSelectedQuadGeometry& operator=(const MotionSelectedQuadGeometry&) = delete;
		void Compose(std::span<const MotionComposedLayer> numeric, std::span<const std::size_t> current_indices, std::span<const MotionStaticSourceBinding* const> sources, std::span<const std::uint8_t> changed, float depth_projection);
		[[nodiscard]] bool Allocated(std::size_t layer) const;
		[[nodiscard]] MotionClipGeometry& Geometry(std::size_t layer);
		[[nodiscard]] const MotionSelectedLayerNode& Node(std::size_t layer) const;

	private:
		friend class MotionSelectedMaskPackets;
		struct Entry {
			const MotionSelectedLayerNode* Node = nullptr;
			std::array<resource::MotionPoint, 4> Positions{};
			MotionClipGeometry Geometry;
			bool Allocated = false;
		};
		void RequirePhase() const;
		void RequireLive(std::size_t layer) const;
		MotionSelectedLayerNodes& m_Selected;
		MotionStencilRowPolicies& m_Rows;
		const MotionSelectedQuadGeometry* m_Containing = nullptr;
		std::vector<Entry> m_Entries;
		std::span<const MotionComposedLayer> m_Numeric;
		std::uint64_t m_ComposedSerial = 0;
		std::uint64_t m_ContainingSerial = 0;
		bool m_Composed = false;
	};

} // namespace sgre::scene
