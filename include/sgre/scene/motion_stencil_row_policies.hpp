#pragma once

#include "sgre/scene/motion_layer_stencil_ancestry.hpp"
#include "sgre/scene/motion_priority_cursor.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace sgre::scene {

	class MotionStencilRowPolicies;
	class MotionSelectedMeshVertices;

	class MotionStencilRowState {
	public:
		MotionStencilRowState() = default;
		[[nodiscard]] const MotionSelectedLayerNode& Node() const;
		[[nodiscard]] bool Published() const;
		[[nodiscard]] std::uint32_t StencilType() const;
		[[nodiscard]] bool ControlEnabled() const;
		[[nodiscard]] float Scale() const;
		[[nodiscard]] float Offset() const;
		[[nodiscard]] const MotionStencilRowState* Ancestor() const;
		[[nodiscard]] std::span<const MotionStencilRowState* const> Members() const;

	private:
		friend class MotionSceneEvaluation;
		friend class MotionStencilRowPolicies;
		friend class MotionSelectedMaskPackets;
		friend class MotionSelectedMeshMaskPackets;
		friend class MotionSelectedMeshVertices;
		const MotionStencilRowPolicies* m_Owner = nullptr;
		const MotionSelectedLayerNode* m_Node = nullptr;
		const MotionStencilRowState* m_Ancestor = nullptr;
		std::vector<const MotionStencilRowState*> m_Members;
		std::uint32_t m_StencilType = 0;
		float m_Scale = 0;
		float m_Offset = 0;
		bool m_ControlEnabled = false;
		bool m_Published = false;
	};

	class MotionStencilRowPolicies {
	public:
		// Selected nodes, ancestry, immutable preparation and all source/summary owners outlive these persistent row components.
		MotionStencilRowPolicies(MotionSelectedLayerNodes& selected, const MotionLayerStencilAncestry& ancestry, MotionStencilRowPolicies* containing_rows = nullptr);
		// The bound component is this exact exclusive selected child, prepared before frame publication.
		void BindChildRows(std::size_t layer, MotionStencilRowPolicies& child_rows);
		// A real graph transition retires packet/queue borrows before replacing exact child row owners.
		// Persistent local rows and their native retained coefficients keep their identities.
		void RefreshChildRows(std::span<MotionStencilRowPolicies* const> children);
		// Real kind4 vectors publish each surviving selected particle in native forward order, even for hidden emitters.
		void RefreshChildRows(std::span<MotionStencilRowPolicies* const> children, std::span<const std::vector<MotionStencilRowPolicies*>> particles);
		[[nodiscard]] bool HasBoundRows(const MotionStencilRowPolicies& child) const noexcept;
		// Removes derived pointer borrows before any selected child retires; retained coefficients stay intact.
		void InvalidateChildRows();
		// The containing numeric phase is fixed through child publication and this owner's subsequent Publish.
		void PrepareRecursiveFrame(std::span<const MotionComposedLayer> numeric);
		MotionStencilRowPolicies(const MotionStencilRowPolicies&) = delete;
		MotionStencilRowPolicies& operator=(const MotionStencilRowPolicies&) = delete;
		// Recovers row stencil fields and mask member identities from actual 50ad30. Geometry and complete draw packets remain separate required producers.
		// Every actual numeric/current/source input and completed stencil ancestry precedes publication. Exact child row owners and their genuine completed numeric phases precede recursive collection.
		void Publish(std::span<const MotionComposedLayer> numeric, std::span<const std::size_t> current_indices, std::span<const MotionStaticSourceBinding* const> sources, float position, const MotionPriorityCursor& priority);
		[[nodiscard]] const MotionStencilRowState& Row(std::size_t index) const;
		[[nodiscard]] std::size_t Size() const noexcept;
		[[nodiscard]] std::span<const MotionStencilRowState* const> CollectedRows() const;
		[[nodiscard]] std::span<const MotionStencilRowState* const> GlobalRows() const;

	private:
		friend class MotionSceneEvaluation;
		friend class MotionStencilRowState;
		friend class MotionSelectedMaskPackets;
		friend class MotionSelectedMeshMaskPackets;
		friend class MotionSelectedMeshVertices;
		friend class MotionSelectedQuadGeometry;
		void Publish(std::span<const MotionComposedLayer> numeric, std::span<const std::size_t> current_indices, std::span<const MotionStaticSourceBinding* const> sources, float position, const MotionPriorityCursor& priority, bool copied_snapshot);
		void RequireLive(const MotionStencilRowState& state) const;
		void ReserveChildRows(std::span<MotionStencilRowPolicies* const> children, std::span<const std::vector<MotionStencilRowPolicies*>> particles);
		[[nodiscard]] const MotionStencilRowState& ResolveAncestorRow(const MotionStencilLayerState& ancestor) const;
		MotionSelectedLayerNodes& m_Selected;
		const MotionLayerStencilAncestry& m_Ancestry;
		std::vector<MotionStencilRowState> m_Rows;
		std::size_t m_NodeCount = 0;
		MotionStencilRowPolicies* m_ContainingRows = nullptr;
		std::vector<MotionStencilRowPolicies*> m_ChildRows;
		std::vector<std::vector<MotionStencilRowPolicies*>> m_ParticleRows;
		std::vector<MotionStencilRowPolicies*> m_BoundRows;
		std::vector<const MotionStencilRowState*> m_CollectedRows;
		std::vector<const MotionStencilRowState*> m_GlobalRows;
		std::size_t m_CollectionCapacity = 0;
		bool m_BoundToContainingRows = false;
		std::size_t m_ContainingLayer = 0;
		std::span<const MotionComposedLayer> m_PreparedNumeric;
		std::span<const MotionComposedLayer> m_PublishedNumeric;
		MotionSelectedMeshVertices* m_MeshVertices = nullptr;
		std::uint64_t m_PreparedMeshAncestrySerial = 0;
		std::uint64_t m_PreparationSerial = 0;
		std::uint64_t m_ContainingPreparationSerial = 0;
		bool m_CollectedThisFrame = false;
		bool m_Published = false;
	};

} // namespace sgre::scene
