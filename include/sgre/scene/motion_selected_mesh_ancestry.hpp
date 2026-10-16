#pragma once

#include "sgre/scene/motion_blank_source.hpp"
#include "sgre/scene/motion_deformation.hpp"
#include "sgre/scene/motion_selected_layer_nodes.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace sgre::scene {

	class MotionLayerComposition;
	class MotionManagerRuntime;
	class MotionSelectedLayerSources;
	class MotionSelectedMeshAncestry;
	class MotionSelectedMeshVertices;
	class MotionStaticSourceBinding;

	struct MotionDeformationSourceGeometry {
		const resource::MotionSource* Source = nullptr;
		const resource::MotionIcon* Icon = nullptr;
		// Retain the actual inline blank descriptor values with their exact source cache owner; no atlas/texture identity.
		std::optional<MotionBlankSourceRectangle> Blank;
		std::optional<MotionExternalSourceBinding> External;
		const MotionStaticSourceBinding* BlankSource = nullptr;
		std::array<resource::MotionPoint, 4> Quad{};
		resource::MotionPoint Origin;
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
		// Mode 0 and source-less cells never receive a made-up deformation matrix.
		std::optional<resource::MotionMeshMatrix> Space;
	};

	class MotionSelectedDeformationState {
	public:
		[[nodiscard]] const MotionSelectedLayerNode& Node() const;
		[[nodiscard]] const MotionSelectedDeformationState* Ancestor() const;
		[[nodiscard]] bool Active() const;
		[[nodiscard]] bool ExportsToChildren() const;
		[[nodiscard]] bool StopsInheritance() const;
		[[nodiscard]] bool Changed() const;
		[[nodiscard]] resource::MotionPoint Anchor() const;
		[[nodiscard]] const MotionDeformationSourceGeometry* SourceGeometry() const;
		[[nodiscard]] const PreparedMotionDeformation* World() const;

	private:
		friend class MotionSceneEvaluation;
		friend class MotionSelectedMeshAncestry;
		friend class MotionSelectedMeshVertices;
		friend class MotionSelectedMeshMaskPackets;
		friend class MotionStencilRowPolicies;
		const MotionSelectedMeshAncestry* m_Owner = nullptr;
		const MotionSelectedLayerNode* m_Node = nullptr;
		MotionChildConstructionState* m_Child = nullptr;
		const MotionSelectedDeformationState* m_Parent = nullptr;
		const MotionSelectedDeformationState* m_Ancestor = nullptr;
		resource::MotionPoint m_Anchor;
		std::optional<MotionDeformationSourceGeometry> m_Source;
		resource::MotionBezierLattice m_OwnLattice;
		resource::MotionBezierLattice m_CombinedLattice;
		std::optional<PreparedMotionDeformation> m_World;
		bool m_Active = false;
		bool m_Export = false;
		bool m_Stop = false;
		bool m_Changed = false;
	};

	class MotionSelectedMeshAncestry {
	public:
		// Selected tree, actual registered source/policy owners and exact containing geometry outlive every borrow.
		// This installs the exclusive mixed-node producer before numeric construction; it grants no ready frame.
		MotionSelectedMeshAncestry(MotionSelectedLayerNodes& selected, MotionSelectedLayerSources& sources, const MotionManagerRuntime& manager, const MotionSelectedMeshAncestry* containing = nullptr);
		// Copy receiving keeps separate-root inherited words dormant until the genuine next parent transfer.
		MotionSelectedMeshAncestry(MotionSelectedLayerNodes& selected, MotionSelectedLayerSources& sources, const MotionManagerRuntime& manager, const MotionSelectedMeshAncestry* containing, const MotionSelectedMeshAncestry& source);
		~MotionSelectedMeshAncestry() noexcept;
		MotionSelectedMeshAncestry(const MotionSelectedMeshAncestry&) = delete;
		MotionSelectedMeshAncestry& operator=(const MotionSelectedMeshAncestry&) = delete;
		void Compose(MotionLayerComposition& composition);
		[[nodiscard]] const MotionSelectedDeformationState& State(std::uint32_t layer) const;
		// Actual selected control/command target and current parent numeric pose bind this child before its +3e0 transfer.
		// The real containing kind3 stop/ancestor is copied before child UpdateChild and event prepend.
		void BindChildDeformation(std::uint32_t layer, MotionChildConstructionState& child) const;
		void BindParticleDeformation(std::uint32_t layer, MotionParticlePlayerState& child, const MotionSelectedLayerNodes& selected) const;

	private:
		friend class MotionSceneEvaluation;
		friend class MotionSelectedDeformationState;
		friend class MotionSelectedMeshVertices;
		friend class MotionSelectedMeshMaskPackets;
		friend class MotionStencilRowPolicies;
		friend class MotionChildConstructionState;
		friend class MotionParticlePlayerState;
		MotionSelectedMeshAncestry(MotionSelectedLayerNodes& selected, MotionSelectedLayerSources& sources, const MotionManagerRuntime& manager, const MotionSelectedMeshAncestry* containing, bool copied_root_pending);
		[[nodiscard]] static bool RequireClone(const MotionSelectedLayerNodes& selected, const MotionSelectedMeshAncestry& source, const MotionSelectedMeshAncestry* containing);
		void RequireOwners() const;
		void RequirePhase() const;
		void RequireLive(const MotionSelectedDeformationState& state) const;
		void RequireChildBorrow(const MotionChildConstructionState& child) const;
		void RequireParticleBorrow(const MotionParticlePlayerState& child, const MotionSelectedLayerNodes& selected) const;
		[[nodiscard]] bool CombinedChainReaches(const MotionSelectedDeformationState& state, const MotionSelectedDeformationState& ancestor) const;
		MotionSelectedLayerNodes& m_Selected;
		MotionSelectedLayerSources& m_Sources;
		const MotionManagerRuntime& m_Manager;
		const MotionSelectedMeshAncestry* m_Containing = nullptr;
		const MotionSelectedDeformationState* m_RootAncestor = nullptr;
		const MotionLayerComposition* m_Composition = nullptr;
		std::vector<MotionSelectedDeformationState> m_States;
		std::uint64_t m_CompositionSerial = 0;
		std::uint64_t m_ContainingSerial = 0;
		std::uint64_t m_AncestrySerial = 0;
		bool m_Composed = false;
		bool m_ClonedRootTransferPending = false;
	};

} // namespace sgre::scene
