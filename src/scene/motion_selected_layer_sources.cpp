#include "sgre/scene/motion_selected_layer_sources.hpp"

#include "sgre/scene/motion_selected_layer_nodes.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <utility>

namespace sgre::scene {

	static constexpr std::size_t kMaximumSourceRegistrations = 256;
	static constexpr std::size_t kMaximumSelectedSourceLayers = 65536;

	MotionSelectedLayerSources::MotionSelectedLayerSources(MotionSelectedLayerNodes& selected, const MotionAssetCatalog& catalog, ReadRegisteredAsset read_asset)
	    : m_Selected(selected), m_Manager(selected.m_Commands.Manager()), m_Catalog(catalog), m_ReadAsset(std::move(read_asset)) {
		if (!m_ReadAsset || selected.m_SourceOwner != nullptr || !selected.m_Commands.SelectedProject()) {
			throw std::runtime_error("motion selected sources require their exclusive actual registered asset producer");
		}
		m_Project = selected.m_Commands.SelectedProject()->Project;
		m_Identity = selected.m_Commands.SelectedProject()->Identity;
		RequireSelected();
		if (m_Manager.Count() > kMaximumSourceRegistrations || selected.Nodes().size() > kMaximumSelectedSourceLayers) {
			throw std::runtime_error("motion selected sources exceed their bounded registration/layer scope");
		}

		std::array<MotionProjectId, kMaximumSourceRegistrations> order{};
		const auto count = m_Manager.WriteRegistrationOrder(order);
		std::vector<MotionSourceRegistration> sources;
		sources.reserve(count);
		m_Registrations.reserve(count);
		for (const auto project : std::span(order).first(count)) {
			const auto& asset = m_ReadAsset(project);
			if (!m_Catalog.ContainsPrepared(asset) || !asset.Document || !asset.Backing || !asset.Model || asset.Identity != m_Manager.Identity(project) || asset.Model.get() != &m_Manager.Resource(project) || asset.Identity.RootOffset != asset.Document->Header.RootOffset) {
				throw std::runtime_error("motion source aliases require the exact registered loaded asset/model/root owner");
			}
			Registration registration;
			registration.Project = project;
			registration.Asset = &asset;
			registration.Aliases = asset.Aliases;
			m_Registrations.push_back(registration);
			MotionSourceRegistration source;
			source.Project = asset.Identity;
			source.Sources = asset.Model->Sources;
			source.Aliases = asset.Aliases;
			sources.push_back(source);
		}
		m_Registry = std::make_unique<PreparedMotionSourceRegistry>(sources);
		RequireRegistrations();

		const auto nodes = selected.Nodes();
		m_Entries.resize(nodes.size());
		for (std::size_t index = 0; index < nodes.size(); ++index) {
			auto& entry = m_Entries[index];
			entry.Node = &nodes[index];
			if (nodes[index].Prepared->Type == 0 || nodes[index].Prepared->Type == 12) {
				MotionStaticSourceServices services;
				services.FindTexture = [this](const ResolvedMotionIcon& binding) {
					return FindTexture(binding);
				};
				entry.Binding = std::make_unique<MotionStaticSourceBinding>(*m_Registry, m_Identity, std::move(services), &selected.m_ExternalSources[index]);
			} else if (nodes[index].Prepared->Type != 1 && nodes[index].Prepared->Type != 2 && nodes[index].Prepared->Type != 3 && nodes[index].Prepared->Type != 4 && nodes[index].Prepared->Type != 7) {
				throw std::runtime_error("motion selected sources require the actual producer for this layer kind");
			}
		}
		selected.m_SourceOwner = this;
	}

	MotionSelectedLayerSources::~MotionSelectedLayerSources() noexcept {
		if (m_Selected.m_SourceOwner == this) {
			m_Selected.m_SourceOwner = nullptr;
		}
	}

	void MotionSelectedLayerSources::RequireSelected() const {
		const auto& selection = m_Selected.m_Commands.CachedSelection();
		// Native515d40 reads the retained timeline at+140. Failed lookup clears only
		// the separate root at+150; it does not retire these evaluated source cells.
		const bool selection_matches = selection && selection->Project == m_Project && selection->Identity == m_Identity && selection->Timeline == &m_Selected.m_Timeline;
		if (!m_Selected.m_Published || m_Selected.m_Mutating || (m_Selected.m_SourceOwner != nullptr && m_Selected.m_SourceOwner != this) || &m_Selected.m_Commands.Manager() != &m_Manager || !selection_matches || m_Selected.m_Commands.CachedTimeline() != &m_Selected.m_Timeline || !m_Manager.Contains(m_Project) || m_Manager.Identity(m_Project) != m_Identity) {
			throw std::runtime_error("motion selected source borrow requires its exact live requesting project and selected tree");
		}
		const auto& resource = m_Manager.Resource(m_Project);
		bool owned = false;
		for (const auto& character : resource.Objects) {
			if (&character != selection->Character) {
				continue;
			}
			owned = std::any_of(character.Timelines.begin(), character.Timelines.end(), [&](const auto& timeline) {
				return &timeline == &m_Selected.m_Timeline;
			});
			if (owned) {
				break;
			}
		}
		if (!owned || m_Selected.m_CommandSelectionSerial != m_Selected.m_Commands.SelectionSerial() || m_Selected.Nodes().size() != m_Selected.m_Preparation.Layers.size()) {
			throw std::runtime_error("motion selected source requester is outside its actual registered character/timeline");
		}
	}

	void MotionSelectedLayerSources::RequireRegistrations() const {
		RequireSelected();
		if (m_Manager.Count() != m_Registrations.size()) {
			throw std::runtime_error("motion selected source registration order changed");
		}
		std::array<MotionProjectId, kMaximumSourceRegistrations> order{};
		if (m_Manager.WriteRegistrationOrder(order) != m_Registrations.size()) {
			throw std::runtime_error("motion selected source registration order changed");
		}
		for (std::size_t index = 0; index < m_Registrations.size(); ++index) {
			const auto& registration = m_Registrations[index];
			if (order[index] != registration.Project || !m_Manager.Contains(registration.Project)) {
				throw std::runtime_error("motion selected source registration identity changed");
			}
			const auto& asset = m_ReadAsset(registration.Project);
			if (&asset != registration.Asset || !m_Catalog.ContainsPrepared(asset) || asset.Identity != m_Manager.Identity(registration.Project) || !asset.Model || asset.Model.get() != &m_Manager.Resource(registration.Project) || asset.Aliases.data() != registration.Aliases.data() || asset.Aliases.size() != registration.Aliases.size()) {
				throw std::runtime_error("motion selected source asset/alias borrow changed");
			}
		}
	}

	MotionLocalFrameServices MotionSelectedLayerSources::FrameServices(std::function<void(std::string_view, std::string_view)> enqueue_action) {
		RequireRegistrations();
		if (!enqueue_action) {
			throw std::runtime_error("motion selected source phase requires its actual action queue");
		}
		MotionLocalFrameServices services;
		services.BindSource = [this](std::uint32_t layer, const PreparedMotionFrame& frame, const PreparedMotionFrame&) {
			Bind(layer, frame);
		};
		services.EnqueueAction = std::move(enqueue_action);

		return services;
	}

	void MotionSelectedLayerSources::Bind(std::uint32_t layer, const PreparedMotionFrame& frame) {
		RequireRegistrations();
		const auto* local = m_Selected.m_LocalNumericOwner;
		if (local == nullptr || !local->m_Traversing || local->m_SelectedOwner != &m_Selected || local->m_Resource != &m_Manager.Resource(m_Project) || &local->m_Prepared != &m_Selected.m_Preparation || layer >= m_Entries.size() || !m_Entries[layer].Binding) {
			throw std::runtime_error("motion selected source binding requires its actual containing local cursor phase");
		}
		const auto index = local->State(layer).CurrentIndex;
		const auto& node = m_Selected.Nodes()[layer];
		if (&node != m_Entries[layer].Node || node.Prepared != &m_Selected.m_Preparation.Layers[layer] || index >= node.Prepared->Frames.size() || &frame != &node.Prepared->Frames[index]) {
			throw std::runtime_error("motion selected source binding requires the exact actual current prepared frame");
		}
		static_cast<void>(m_Entries[layer].Binding->Bind(frame));
		RequireRegistrations();
		m_Entries[layer].Frame = index;
	}

	std::optional<RegisteredMotionSource> MotionSelectedLayerSources::FindTexture(const ResolvedMotionIcon& binding) const {
		RequireRegistrations();
		if (binding.RegistrationIndex >= m_Registrations.size() || binding.Source == nullptr || binding.Icon == nullptr) {
			throw std::runtime_error("motion selected source resolution exceeds its actual registered owners");
		}
		const auto& registration = m_Registrations[binding.RegistrationIndex];
		if (registration.Asset->Identity != binding.Project) {
			throw std::runtime_error("motion selected source resolved registration differs from its actual loaded asset");
		}
		const auto texture = m_Manager.FindSource(registration.Project, binding.Source->Name);
		if (!texture || texture->Source != binding.Source || std::none_of(binding.Source->Icons.begin(), binding.Source->Icons.end(), [&](const auto& icon) {
			    return &icon == binding.Icon;
		    })) {
			throw std::runtime_error("motion selected source texture/icon differs from its actual resolved registration");
		}

		return texture;
	}

	void MotionSelectedLayerSources::RequireCurrent(std::uint32_t layer) const {
		RequireRegistrations();
		const auto* local = m_Selected.m_LocalNumericOwner;
		if (local == nullptr || local->m_Traversing || !local->m_Selected || local->m_SelectedOwner != &m_Selected || local->m_Resource != &m_Manager.Resource(m_Project) || layer >= m_Entries.size() || !m_Entries[layer].Binding || !m_Entries[layer].Frame || &m_Selected.Nodes()[layer] != m_Entries[layer].Node || *m_Entries[layer].Frame != local->State(layer).CurrentIndex) {
			throw std::runtime_error("motion selected source state requires its exact completed current local cursor owner");
		}
	}

	const MotionStaticSourceState& MotionSelectedLayerSources::State(std::uint32_t layer) const {
		RequireCurrent(layer);

		return m_Entries[layer].Binding->State();
	}

	const MotionStaticSourceBinding& MotionSelectedLayerSources::Source(std::uint32_t layer) const {
		RequireCurrent(layer);

		return *m_Entries[layer].Binding;
	}

	MotionProjectId MotionSelectedLayerSources::RequestingProject() const {
		RequireRegistrations();

		return m_Project;
	}

	std::optional<MotionProjectId> MotionSelectedLayerSources::ResolvedProject(std::uint32_t layer) const {
		const auto& state = State(layer);
		if (!state.Binding) {
			return std::nullopt;
		}

		return m_Registrations.at(state.Binding->RegistrationIndex).Project;
	}

} // namespace sgre::scene
