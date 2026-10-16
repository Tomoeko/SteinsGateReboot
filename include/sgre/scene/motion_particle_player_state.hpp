#pragma once

#include "sgre/scene/motion_child_construction.hpp"
#include "sgre/scene/motion_particle_definition.hpp"

namespace sgre::scene {
	class MotionPlayerUpdate;

	class MotionParticlePlayerState {
	public:
		// These actual parent/manager owners enclose this particle and its enclosing selected Context.
		// The emitted player's ordinary root recomputes its matrix; it is not a kind3 supplied-pose child.
		MotionParticlePlayerState(const MotionPlayerCommands& parent_commands, const MotionRootScene& parent_root, const MotionPlayerRenderControls& parent_render, const MotionChildConstructionServices& services);
		MotionParticlePlayerState(const MotionChildConstructionState& parent, const MotionChildConstructionServices& services);
		MotionParticlePlayerState(const MotionParticlePlayerState& parent, const MotionChildConstructionServices& services);
		MotionParticlePlayerState(const MotionParticlePlayerState& source, const MotionPlayerCommands& parent_commands, const MotionPlayerCommands& ultimate_commands);
		MotionParticlePlayerState(const MotionParticlePlayerState&) = delete;
		MotionParticlePlayerState& operator=(const MotionParticlePlayerState&) = delete;
		MotionParticlePlayerState(MotionParticlePlayerState&&) = delete;
		MotionParticlePlayerState& operator=(MotionParticlePlayerState&&) = delete;
		// Native5235CF copies the preferred root before SetCharacter/Play. Scene construction follows that actual selection.
		// Failed lookup leaves the genuine stopped/root-only player for the enclosing pre-tick retirement phase.
		void SelectInitialTarget(const MotionParticleTarget& target);
		[[nodiscard]] MotionPlayerCommands& Commands() noexcept;
		[[nodiscard]] const MotionPlayerCommands& Commands() const noexcept;
		[[nodiscard]] MotionRootScene& Root() noexcept;
		[[nodiscard]] const MotionRootScene& Root() const noexcept;
		[[nodiscard]] MotionPlayerRandom& Random() noexcept;
		[[nodiscard]] const MotionPlayerRandom& Random() const noexcept;
		[[nodiscard]] const MotionPlayerRenderControls& RenderControls() const noexcept;
		[[nodiscard]] const MotionPlayerCommands& ImmediateParentCommands() const noexcept;
		[[nodiscard]] const MotionPlayerCommands& UltimateParentCommands() const noexcept;
		// Borrows the exact current containing stencil owner through this actual particle update/draw.
		[[nodiscard]] const MotionStencilLayerState* InheritedStencil() const noexcept;

	private:
		friend class MotionSceneEvaluation;
		friend class MotionSelectedLayerNodes;
		friend class MotionSelectedMeshAncestry;
		friend class MotionLayerClipRectangles;
		friend class MotionLayerStencilAncestry;
		MotionParticlePlayerState(const MotionPlayerCommands& parent_commands, const MotionPlayerCommands& ultimate_commands, const MotionRootScene& parent_root, const MotionPlayerRenderControls& parent_render, const MotionChildConstructionServices& services);
		// Enclosing selected Context retires before this state; its owned preparation encloses command/event strings.
		MotionPreparationLease m_PreparationOwner;
		MotionPlayerUpdate* m_UpdateOwner = nullptr;
		const MotionClipRectangle* m_InheritedClip = nullptr;
		const MotionStencilLayerState* m_InheritedStencil = nullptr;
		const MotionSelectedMeshAncestry* m_DeformationTransferOwner = nullptr;
		const MotionSelectedDeformationState* m_InheritedDeformation = nullptr;
		std::uint32_t m_DeformationTransferLayer = 0;
		std::uint64_t m_DeformationTransferSerial = 0;
		MotionPlayerRandom m_Random;
		MotionPlayerCommands m_Commands;
		MotionRootScene m_Root;
		MotionPlayerRenderControls m_RenderControls;
		const MotionPlayerCommands& m_ImmediateParentCommands;
		const MotionPlayerCommands& m_UltimateParentCommands;
		bool m_InitialSelectionAttempted = false;
	};

} // namespace sgre::scene
