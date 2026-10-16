#pragma once

#include "sgre/scene/motion_layer_composition.hpp"
#include "sgre/scene/motion_selected_layer_nodes.hpp"
#include "sgre/scene/motion_static_source.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace sgre::scene {
	class MotionLayerStencilAncestry;
	class MotionStencilMaskOwner;

	class MotionStencilLayerState {
	public:
		MotionStencilLayerState() = default;
		[[nodiscard]] const MotionSelectedLayerNode& Node() const;
		[[nodiscard]] bool Active() const;
		[[nodiscard]] const MotionStencilLayerState* Ancestor() const;
		// Only genuine kind12 nodes carry this exact exclusive retained summary owner.
		[[nodiscard]] const MotionStencilMaskOwner* MaskOwner() const;

	private:
		friend class MotionSceneEvaluation;
		friend class MotionLayerStencilAncestry;
		const MotionLayerStencilAncestry* m_Owner = nullptr;
		const MotionSelectedLayerNode* m_Node = nullptr;
		const MotionStencilMaskOwner* m_Mask = nullptr;
		const MotionStencilLayerState* m_Ancestor = nullptr;
		bool m_Active = false;
	};

	class MotionLayerStencilAncestry {
	public:
		// The exact live selected tree/preparation and all source/summary owners outlive this phase and its child borrows.
		explicit MotionLayerStencilAncestry(MotionSelectedLayerNodes& selected);
		MotionLayerStencilAncestry(const MotionLayerStencilAncestry&) = delete;
		MotionLayerStencilAncestry& operator=(const MotionLayerStencilAncestry&) = delete;
		// Genuine numeric composition, selected indices, source bindings and evaluated kind12 controls precede this phase.
		// Immediate raw parents govern stencil ancestry even when numeric inheritance skips an ancestor.
		// A child root borrow must equal its exact containing child transfer. Ordinary roots require null until their producer is recovered.
		void Compose(std::span<const MotionComposedLayer> layers, std::span<const std::size_t> current_indices, std::span<const MotionStaticSourceBinding* const> sources, float position, const MotionStencilLayerState* root_stencil);
		[[nodiscard]] const MotionStencilLayerState& State(std::size_t layer) const;
		// Actual521bf0 transfers the kind3 inherited ancestor to its exact exclusive child before UpdateChild.
		// The current node itself is not substituted, and this transfer writes no numeric root dirty state.
		void BindChildStencil(std::size_t layer, MotionChildConstructionState& child) const;
		void BindParticleStencil(std::size_t layer, MotionParticlePlayerState& child, const MotionSelectedLayerNodes& selected) const;

	private:
		friend class MotionSceneEvaluation;
		friend class MotionStencilLayerState;
		void RequireLive(const MotionStencilLayerState& state) const;
		MotionSelectedLayerNodes& m_Selected;
		const PreparedMotionTimeline& m_Prepared;
		std::vector<MotionStencilLayerState> m_States;
		bool m_Composed = false;
	};

} // namespace sgre::scene
