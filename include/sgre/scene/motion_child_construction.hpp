#pragma once

#include "sgre/scene/motion_child_configuration.hpp"
#include "sgre/scene/motion_child_control.hpp"
#include "sgre/scene/motion_player_commands.hpp"
#include "sgre/scene/motion_preparation_lease.hpp"
#include "sgre/scene/motion_random.hpp"
#include "sgre/scene/motion_root_scene.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace sgre::scene {

	struct MotionClipRectangle;
	class MotionLayerClipRectangles;
	class MotionStencilLayerState;
	class MotionLayerStencilAncestry;
	class MotionSelectedMeshAncestry;
	class MotionSelectedDeformationState;
	class MotionParticlePlayerState;

	struct MotionPlayerRenderControls {
		std::uint32_t ColorFilter = 0;
		std::uint32_t ColorFilterParameter = 0;
		std::uint32_t EmissiveColor = 0;
		float AmbientFactor = 1;
	};

	struct MotionChildConstructionServices {
		// Required observational service samples the actual parent's serialized manager generator exactly once.
		std::function<float(const MotionResourceManager&)> ReadManagerRandom;
		// Only kind4 screen deletion reads the actual registered document screenSize.
		std::function<const resource::PsbValue&(MotionProjectId)> ReadDocumentRoot;
	};

	class MotionChildConstructionState;
	struct MotionChildLifetime;

	// This weak lifetime token follows the actual owned child through JOIN transfers; it retains no player.
	class MotionChildBorrow {
	public:
		[[nodiscard]] bool IsLive() const noexcept;
		[[nodiscard]] bool SameTarget(const MotionChildBorrow& other) const noexcept;

	private:
		friend class MotionChildConstructionState;
		friend class MotionPlayerTask;
		explicit MotionChildBorrow(std::weak_ptr<MotionChildLifetime> lifetime);
		[[nodiscard]] MotionChildConstructionState& Require() const;
		std::weak_ptr<MotionChildLifetime> m_Lifetime;
	};

	class MotionChildConstructionState {
	public:
		// Immediate/ultimate parent commands, manager registrations and selected target-string owners outlive this state.
		// This owns only the recovered constructor state; selected-tree/resource retirement and update/publication remain enclosing phases.
		MotionChildConstructionState(const MotionPlayerCommands& parent_commands, const MotionRootScene& parent_root, const MotionPlayerRenderControls& parent_render, const resource::MotionLayer& layer, const PreparedMotionLayer& prepared, const MotionChildConstructionServices& services);
		MotionChildConstructionState(const MotionChildConstructionState& parent, const resource::MotionLayer& layer, const PreparedMotionLayer& prepared, const MotionChildConstructionServices& services);
		MotionChildConstructionState(const MotionParticlePlayerState& parent, const resource::MotionLayer& layer, const PreparedMotionLayer& prepared, const MotionChildConstructionServices& services);
		// The clone borrows its new actual parents and copies the live capsule without sampling the manager.
		MotionChildConstructionState(const MotionChildConstructionState& source, const MotionPlayerCommands& parent_commands, const MotionPlayerCommands& ultimate_commands);
		~MotionChildConstructionState() noexcept;
		[[nodiscard]] MotionChildBorrow Borrow() const;
		MotionChildConstructionState(const MotionChildConstructionState&) = delete;
		MotionChildConstructionState& operator=(const MotionChildConstructionState&) = delete;
		MotionChildConstructionState(MotionChildConstructionState&&) = delete;
		MotionChildConstructionState& operator=(MotionChildConstructionState&&) = delete;

		[[nodiscard]] MotionPlayerCommands& Commands() noexcept;
		[[nodiscard]] const MotionPlayerCommands& Commands() const noexcept;
		[[nodiscard]] MotionRootScene& Root() noexcept;
		[[nodiscard]] const MotionRootScene& Root() const noexcept;
		[[nodiscard]] MotionPlayerRandom& Random() noexcept;
		[[nodiscard]] const MotionPlayerRandom& Random() const noexcept;
		[[nodiscard]] const MotionPlayerRenderControls& RenderControls() const noexcept;
		[[nodiscard]] const MotionPlayerCommands& ImmediateParentCommands() const noexcept;
		[[nodiscard]] const MotionPlayerCommands& UltimateParentCommands() const noexcept;
		[[nodiscard]] bool IndependentLayerInherit() const noexcept;
		// Native root+3b0 borrows the actual enclosing selected rectangle through child clip evaluation/use.
		[[nodiscard]] const MotionClipRectangle* InheritedClip() const noexcept;
		// The enclosing genuine stencil ancestry owner outlives this child borrow through its composition/publication.
		[[nodiscard]] const MotionStencilLayerState* InheritedStencil() const noexcept;
		// Native root+3e0 borrows this exact current containing kind3 stop node or deformation ancestor.
		[[nodiscard]] const MotionSelectedDeformationState* InheritedDeformation() const;
		// Actual selected slot identity is supplied by frame preparation, never reconstructed from time here.
		[[nodiscard]] const MotionChildFrameControl& Control(std::size_t slot) const;
		void ApplyControlUpdate(std::size_t slot, const std::optional<MotionChildFrameControl>& update);

	private:
		friend class MotionSelectedLayerNodes;
		friend class MotionSceneEvaluation;
		friend class MotionLayerClipRectangles;
		friend class MotionLayerStencilAncestry;
		friend class MotionSelectedMeshAncestry;
		MotionChildConstructionState(const MotionPlayerCommands& parent_commands, const MotionPlayerCommands& ultimate_commands, const MotionRootScene& parent_root, const MotionPlayerRenderControls& parent_render, const resource::MotionLayer& layer, const PreparedMotionLayer& prepared, const MotionChildConstructionServices& services);
		// Declared first so every selected child/command callback retires before its immutable frame owner.
		MotionPreparationLease m_PreparationOwner;
		const MotionClipRectangle* m_InheritedClip = nullptr;
		const MotionStencilLayerState* m_InheritedStencil = nullptr;
		const MotionSelectedMeshAncestry* m_DeformationTransferOwner = nullptr;
		const MotionSelectedDeformationState* m_InheritedDeformation = nullptr;
		std::uint32_t m_DeformationTransferLayer = 0;
		std::uint64_t m_DeformationTransferSerial = 0;
		MotionChildRootConfiguration m_Configuration;
		MotionPlayerRandom m_Random;
		MotionPlayerCommands m_Commands;
		MotionRootScene m_Root;
		MotionPlayerRenderControls m_RenderControls;
		std::array<MotionChildFrameControl, 2> m_Controls{};
		const MotionPlayerCommands& m_ImmediateParentCommands;
		const MotionPlayerCommands& m_UltimateParentCommands;
		std::shared_ptr<MotionChildLifetime> m_Lifetime;
	};

} // namespace sgre::scene
