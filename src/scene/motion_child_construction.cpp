#include "sgre/scene/motion_child_construction.hpp"

#include "sgre/scene/motion_clone_resource.hpp"

#include "sgre/scene/motion_particle_player_state.hpp"
#include "sgre/scene/motion_selected_mesh_ancestry.hpp"

#include <stdexcept>
#include <utility>

namespace sgre::scene {

	struct MotionChildLifetime {
		MotionChildConstructionState* Actual = nullptr;
	};

	MotionChildBorrow::MotionChildBorrow(std::weak_ptr<MotionChildLifetime> lifetime)
	    : m_Lifetime(std::move(lifetime)) {
	}

	bool MotionChildBorrow::IsLive() const noexcept {
		const auto lifetime = m_Lifetime.lock();

		return lifetime && lifetime->Actual != nullptr;
	}

	bool MotionChildBorrow::SameTarget(const MotionChildBorrow& other) const noexcept {
		return !m_Lifetime.owner_before(other.m_Lifetime) && !other.m_Lifetime.owner_before(m_Lifetime);
	}

	MotionChildConstructionState& MotionChildBorrow::Require() const {
		const auto lifetime = m_Lifetime.lock();
		if (!lifetime || lifetime->Actual == nullptr) {
			throw std::runtime_error("borrowed Motion child has retired");
		}

		return *lifetime->Actual;
	}

	MotionChildConstructionState::~MotionChildConstructionState() noexcept {
		m_Lifetime->Actual = nullptr;
	}

	MotionChildBorrow MotionChildConstructionState::Borrow() const {
		return MotionChildBorrow(m_Lifetime);
	}

	static float ReadChildManagerRandom(const MotionResourceManager& manager, const MotionChildConstructionServices& services) {
		if (!services.ReadManagerRandom) {
			throw std::runtime_error("motion child construction requires actual manager random sampling");
		}

		return services.ReadManagerRandom(manager);
	}

	MotionChildConstructionState::MotionChildConstructionState(const MotionPlayerCommands& parent_commands, const MotionRootScene& parent_root, const MotionPlayerRenderControls& parent_render, const resource::MotionLayer& layer, const PreparedMotionLayer& prepared, const MotionChildConstructionServices& services)
	    : MotionChildConstructionState(parent_commands, parent_commands, parent_root, parent_render, layer, prepared, services) {
	}

	MotionChildConstructionState::MotionChildConstructionState(const MotionChildConstructionState& parent, const resource::MotionLayer& layer, const PreparedMotionLayer& prepared, const MotionChildConstructionServices& services)
	    : MotionChildConstructionState(parent.m_Commands, parent.m_UltimateParentCommands, parent.m_Root, parent.m_RenderControls, layer, prepared, services) {
	}

	MotionChildConstructionState::MotionChildConstructionState(const MotionParticlePlayerState& parent, const resource::MotionLayer& layer, const PreparedMotionLayer& prepared, const MotionChildConstructionServices& services)
	    : MotionChildConstructionState(parent.Commands(), parent.UltimateParentCommands(), parent.Root(), parent.RenderControls(), layer, prepared, services) {
	}

	MotionChildConstructionState::MotionChildConstructionState(const MotionPlayerCommands& parent_commands, const MotionPlayerCommands& ultimate_commands, const MotionRootScene& parent_root, const MotionPlayerRenderControls& parent_render, const resource::MotionLayer& layer, const PreparedMotionLayer& prepared, const MotionChildConstructionServices& services)
	    : m_Configuration(PrepareMotionChildRootConfiguration(layer, prepared, parent_root.Priority())), m_Random(ReadChildManagerRandom(parent_commands.Manager(), services)), m_Commands(parent_commands.Manager(), parent_commands), m_Root(m_Configuration), m_RenderControls(parent_render), m_ImmediateParentCommands(parent_commands), m_UltimateParentCommands(ultimate_commands) {
		m_Lifetime = std::make_shared<MotionChildLifetime>();
		m_Lifetime->Actual = this;
	}

	MotionChildConstructionState::MotionChildConstructionState(const MotionChildConstructionState& source, const MotionPlayerCommands& parent_commands, const MotionPlayerCommands& ultimate_commands)
	    : m_PreparationOwner(source.m_PreparationOwner), m_Configuration(source.m_Configuration), m_Random(source.m_Random), m_Commands(parent_commands.Manager(), parent_commands), m_Root(source.m_Root), m_RenderControls(source.m_RenderControls), m_Controls(source.m_Controls), m_ImmediateParentCommands(parent_commands), m_UltimateParentCommands(ultimate_commands), m_Lifetime(std::make_shared<MotionChildLifetime>()) {
		if (&parent_commands.Manager() != &ultimate_commands.Manager()) {
			throw std::runtime_error("motion child clone requires the same actual manager and new containing parents");
		}
		m_Commands.CopyForClone(source.m_Commands);
		m_Lifetime->Actual = this;
	}

	MotionPlayerCommands& MotionChildConstructionState::Commands() noexcept {
		return m_Commands;
	}

	const MotionPlayerCommands& MotionChildConstructionState::Commands() const noexcept {
		return m_Commands;
	}

	MotionRootScene& MotionChildConstructionState::Root() noexcept {
		return m_Root;
	}

	const MotionRootScene& MotionChildConstructionState::Root() const noexcept {
		return m_Root;
	}

	MotionPlayerRandom& MotionChildConstructionState::Random() noexcept {
		return m_Random;
	}

	const MotionPlayerRandom& MotionChildConstructionState::Random() const noexcept {
		return m_Random;
	}

	const MotionPlayerRenderControls& MotionChildConstructionState::RenderControls() const noexcept {
		return m_RenderControls;
	}

	const MotionPlayerCommands& MotionChildConstructionState::ImmediateParentCommands() const noexcept {
		return m_ImmediateParentCommands;
	}

	const MotionPlayerCommands& MotionChildConstructionState::UltimateParentCommands() const noexcept {
		return m_UltimateParentCommands;
	}

	bool MotionChildConstructionState::IndependentLayerInherit() const noexcept {
		return m_Configuration.IndependentLayerInherit;
	}

	const MotionClipRectangle* MotionChildConstructionState::InheritedClip() const noexcept {
		return m_InheritedClip;
	}

	const MotionStencilLayerState* MotionChildConstructionState::InheritedStencil() const noexcept {
		return m_InheritedStencil;
	}

	const MotionSelectedDeformationState* MotionChildConstructionState::InheritedDeformation() const {
		if (m_DeformationTransferOwner != nullptr) {
			m_DeformationTransferOwner->RequireChildBorrow(*this);
		}

		return m_InheritedDeformation;
	}

	const MotionChildFrameControl& MotionChildConstructionState::Control(std::size_t slot) const {
		if (slot >= m_Controls.size()) {
			throw std::runtime_error("motion child control slot must be zero or one");
		}

		return m_Controls[slot];
	}

	void MotionChildConstructionState::ApplyControlUpdate(std::size_t slot, const std::optional<MotionChildFrameControl>& update) {
		if (slot >= m_Controls.size()) {
			throw std::runtime_error("motion child control slot must be zero or one");
		}

		ApplyMotionChildFrameControl(update, m_Controls[slot]);
	}

} // namespace sgre::scene
