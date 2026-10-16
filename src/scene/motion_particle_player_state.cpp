#include "sgre/scene/motion_particle_player_state.hpp"

#include "sgre/scene/motion_clone_resource.hpp"

#include <stdexcept>

namespace sgre::scene {

	static float ReadParticleManagerRandom(const MotionPlayerCommands& parent, const MotionPlayerCommands& ultimate, const MotionChildConstructionServices& services) {
		if (&parent.Manager() != &ultimate.Manager() || !services.ReadManagerRandom) {
			throw std::runtime_error("motion particle construction requires its actual parent manager and serialized random sample");
		}

		return services.ReadManagerRandom(parent.Manager());
	}

	MotionParticlePlayerState::MotionParticlePlayerState(const MotionPlayerCommands& parent_commands, const MotionRootScene& parent_root, const MotionPlayerRenderControls& parent_render, const MotionChildConstructionServices& services)
	    : MotionParticlePlayerState(parent_commands, parent_commands, parent_root, parent_render, services) {
	}

	MotionParticlePlayerState::MotionParticlePlayerState(const MotionChildConstructionState& parent, const MotionChildConstructionServices& services)
	    : MotionParticlePlayerState(parent.Commands(), parent.UltimateParentCommands(), parent.Root(), parent.RenderControls(), services) {
	}

	MotionParticlePlayerState::MotionParticlePlayerState(const MotionParticlePlayerState& parent, const MotionChildConstructionServices& services)
	    : MotionParticlePlayerState(parent.Commands(), parent.UltimateParentCommands(), parent.Root(), parent.RenderControls(), services) {
	}

	MotionParticlePlayerState::MotionParticlePlayerState(const MotionPlayerCommands& parent_commands, const MotionPlayerCommands& ultimate_commands, const MotionRootScene& parent_root, const MotionPlayerRenderControls& parent_render, const MotionChildConstructionServices& services)
	    : m_Random(ReadParticleManagerRandom(parent_commands, ultimate_commands, services)), m_Commands(parent_commands.Manager(), parent_commands), m_RenderControls(parent_render), m_ImmediateParentCommands(parent_commands), m_UltimateParentCommands(ultimate_commands) {
		m_Root.SetPriority(parent_root.Priority());
	}

	MotionParticlePlayerState::MotionParticlePlayerState(const MotionParticlePlayerState& source, const MotionPlayerCommands& parent_commands, const MotionPlayerCommands& ultimate_commands)
	    : m_PreparationOwner(source.m_PreparationOwner), m_Random(source.m_Random), m_Commands(parent_commands.Manager(), parent_commands), m_Root(source.m_Root), m_RenderControls(source.m_RenderControls), m_ImmediateParentCommands(parent_commands), m_UltimateParentCommands(ultimate_commands), m_InitialSelectionAttempted(source.m_InitialSelectionAttempted) {
		if (&parent_commands.Manager() != &ultimate_commands.Manager()) {
			throw std::runtime_error("motion particle clone requires the same actual manager and new containing parents");
		}
		m_Commands.CopyForClone(source.m_Commands);
	}

	void MotionParticlePlayerState::SelectInitialTarget(const MotionParticleTarget& target) {
		if (m_InitialSelectionAttempted) {
			throw std::runtime_error("motion particle initial target belongs to its one genuine construction phase");
		}

		m_InitialSelectionAttempted = true;
		m_Commands.SetCharacter(target.Character);
		// Play performs the actual optional deferred-stealth D0 selection after ordinary selection, matching523664.
		m_Commands.Play(target.Timeline, MotionPlayFlags::None);
		m_Root.SetVisible(true);
	}

	MotionPlayerCommands& MotionParticlePlayerState::Commands() noexcept {
		return m_Commands;
	}

	const MotionPlayerCommands& MotionParticlePlayerState::Commands() const noexcept {
		return m_Commands;
	}

	MotionRootScene& MotionParticlePlayerState::Root() noexcept {
		return m_Root;
	}

	const MotionRootScene& MotionParticlePlayerState::Root() const noexcept {
		return m_Root;
	}

	MotionPlayerRandom& MotionParticlePlayerState::Random() noexcept {
		return m_Random;
	}

	const MotionPlayerRandom& MotionParticlePlayerState::Random() const noexcept {
		return m_Random;
	}

	const MotionPlayerRenderControls& MotionParticlePlayerState::RenderControls() const noexcept {
		return m_RenderControls;
	}

	const MotionPlayerCommands& MotionParticlePlayerState::ImmediateParentCommands() const noexcept {
		return m_ImmediateParentCommands;
	}

	const MotionPlayerCommands& MotionParticlePlayerState::UltimateParentCommands() const noexcept {
		return m_UltimateParentCommands;
	}

	const MotionStencilLayerState* MotionParticlePlayerState::InheritedStencil() const noexcept {
		return m_InheritedStencil;
	}

} // namespace sgre::scene
