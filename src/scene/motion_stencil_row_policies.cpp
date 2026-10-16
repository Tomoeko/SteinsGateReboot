#include "sgre/scene/motion_stencil_row_policies.hpp"

#include "sgre/scene/motion_local_frames.hpp"
#include "sgre/scene/motion_particle_layer_state.hpp"
#include "sgre/scene/motion_particle_player_state.hpp"
#include "sgre/scene/motion_selected_mesh_vertices.hpp"
#include "sgre/scene/motion_stencil_mask_owner.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace sgre::scene {

	static constexpr std::size_t kMaximumStencilPolicyRows = 65536;
	static constexpr std::size_t kMaximumStencilPolicyMembers = 1048576;

	static std::string_view StencilRowSourceName(std::string_view text) {
		return text.substr(0, text.find('\0'));
	}

	MotionStencilRowPolicies::MotionStencilRowPolicies(MotionSelectedLayerNodes& selected, const MotionLayerStencilAncestry& ancestry, MotionStencilRowPolicies* containing_rows)
	    : m_Selected(selected), m_Ancestry(ancestry), m_ContainingRows(containing_rows) {
		const auto nodes = selected.Nodes();
		if (nodes.size() > kMaximumStencilPolicyRows || selected.DrawRowCount() > nodes.size()) {
			throw std::runtime_error("motion stencil policies require bounded actual selected drawing rows");
		}

		if ((selected.m_EnclosingChild == nullptr && selected.m_EnclosingParticle == nullptr) != (containing_rows == nullptr)) {
			throw std::runtime_error("motion stencil rows require their actual containing child row owner");
		}
		if (containing_rows != nullptr) {
			bool exact_child = false;
			for (const auto& parent_node : containing_rows->m_Selected.Nodes()) {
				if (parent_node.Prepared->Type == 3 && containing_rows->m_Selected.ChildNodes(parent_node.NativeOrdinal - 1) == &selected && &containing_rows->m_Selected.Child(parent_node.NativeOrdinal - 1) == selected.m_EnclosingChild) {
					exact_child = true;
					m_ContainingLayer = parent_node.NativeOrdinal - 1;
					break;
				}
			}
			if (selected.m_EnclosingParticle != nullptr) {
				for (const auto ordinal : containing_rows->m_Selected.ParticleOrdinals()) {
					if (containing_rows->m_Selected.OwnsParticleSelection(ordinal - 1, selected)) {
						exact_child = true;
						m_ContainingLayer = ordinal - 1;
						break;
					}
				}
			}
			if (!exact_child) {
				throw std::runtime_error("motion stencil rows require this exact exclusive containing selected tree");
			}
		}
		m_ChildRows.resize(nodes.size());
		m_ParticleRows.resize(nodes.size());
		m_CollectionCapacity = nodes.size();
		m_CollectedRows.reserve(m_CollectionCapacity);
		m_GlobalRows.reserve(m_CollectionCapacity);
		m_NodeCount = nodes.size();
		m_Rows.resize(selected.DrawRowCount());
		std::size_t members = 0;
		for (const auto& node : nodes) {
			if (!node.DrawRow) {
				continue;
			}
			if (*node.DrawRow >= m_Rows.size() || m_Rows[*node.DrawRow].m_Node != nullptr || (node.Prepared->Type != 0 && node.Prepared->Type != 3 && node.Prepared->Type != 12)) {
				throw std::runtime_error("motion stencil policies require each exact unique selected drawing row");
			}

			auto& row = m_Rows[*node.DrawRow];
			row.m_Owner = this;
			row.m_Node = &node;
			if (node.Prepared->Type == 12) {
				const auto count = selected.StencilMask(node.NativeOrdinal - 1).MaskLayers().size();
				if (count > kMaximumStencilPolicyRows || count + 1 > kMaximumStencilPolicyMembers - members) {
					throw std::runtime_error("motion stencil row member storage exceeds its actual bounded lists");
				}
				row.m_Members.reserve(count + 1);
				members += count + 1;
			}
		}
		for (const auto& row : m_Rows) {
			if (row.m_Node == nullptr) {
				throw std::runtime_error("motion stencil policies must retain every actual selected drawing row");
			}
		}
	}

	void MotionStencilRowPolicies::BindChildRows(std::size_t layer, MotionStencilRowPolicies& child_rows) {
		const auto nodes = m_Selected.Nodes();
		if (layer >= nodes.size() || nodes[layer].Prepared->Type != 3 || m_Selected.ChildNodes(static_cast<std::uint32_t>(layer)) != &child_rows.m_Selected || child_rows.m_ContainingRows != this || child_rows.m_Selected.m_EnclosingChild != &m_Selected.Child(static_cast<std::uint32_t>(layer)) || m_ChildRows[layer] != nullptr || m_BoundToContainingRows || child_rows.m_BoundToContainingRows) {
			throw std::runtime_error("motion child row binding requires its exact exclusive selected child context and bottom-up preparation");
		}
		auto children = m_ChildRows;
		children[layer] = &child_rows;
		ReserveChildRows(children, m_ParticleRows);
	}

	void MotionStencilRowPolicies::InvalidateChildRows() {
		if (m_MeshVertices != nullptr) {
			throw std::runtime_error("motion child row invalidation requires retired packet bindings");
		}
		m_Published = false;
		m_CollectedThisFrame = false;
		m_CollectedRows.clear();
		m_GlobalRows.clear();
		std::fill(m_ChildRows.begin(), m_ChildRows.end(), nullptr);
		for (auto& particles : m_ParticleRows) {
			particles.clear();
		}
		m_BoundRows.clear();
		for (auto& row : m_Rows) {
			row.m_Published = false;
			row.m_Ancestor = nullptr;
			row.m_Members.clear();
		}
	}

	void MotionStencilRowPolicies::RefreshChildRows(std::span<MotionStencilRowPolicies* const> children) {
		std::vector<std::vector<MotionStencilRowPolicies*>> particles(m_NodeCount);
		RefreshChildRows(children, particles);
	}

	bool MotionStencilRowPolicies::HasBoundRows(const MotionStencilRowPolicies& child) const noexcept {
		return std::find(m_BoundRows.begin(), m_BoundRows.end(), &child) != m_BoundRows.end();
	}

	void MotionStencilRowPolicies::RefreshChildRows(std::span<MotionStencilRowPolicies* const> children, std::span<const std::vector<MotionStencilRowPolicies*>> particles) {
		const auto nodes = m_Selected.Nodes();
		if (m_MeshVertices != nullptr || children.size() != nodes.size() || particles.size() != nodes.size() || nodes.size() != m_NodeCount) {
			throw std::runtime_error("motion child row refresh requires retired packet bindings and the same exact local selected rows");
		}
		for (std::size_t index = 0; index != children.size(); ++index) {
			const auto* child = children[index];
			if (nodes[index].Prepared->Type == 4) {
				if (child != nullptr || particles[index].size() != m_Selected.Particles(static_cast<std::uint32_t>(index)).Size()) {
					throw std::runtime_error("motion particle row refresh requires every actual surviving selected player in vector order");
				}
				for (std::size_t occurrence = 0; occurrence < particles[index].size(); ++occurrence) {
					const auto* particle = particles[index][occurrence];
					if (particle == nullptr || particle->m_MeshVertices != nullptr || particle->m_ContainingRows != this || particle->m_ContainingLayer != index || !m_Selected.OwnsParticleSelection(static_cast<std::uint32_t>(index), occurrence, particle->m_Selected)) {
						throw std::runtime_error("motion particle row refresh requires each exact live emitted selected owner");
					}
				}
				continue;
			}
			if (!particles[index].empty()) {
				throw std::runtime_error("motion particle rows cannot attach to a non-emitter selected layer");
			}
			if (nodes[index].Prepared->Type != 3) {
				if (child != nullptr) {
					throw std::runtime_error("motion child row refresh cannot attach a structural or sprite layer");
				}
				continue;
			}
			if (child == nullptr) {
				if (!m_Selected.Child(static_cast<std::uint32_t>(index)).Commands().Names().CurrentMotion.empty()) {
					throw std::runtime_error("motion child row refresh cannot omit a present actual child motion");
				}
				continue;
			}
			if (child->m_MeshVertices != nullptr || child->m_ContainingRows != this || child->m_ContainingLayer != index || m_Selected.ChildNodes(static_cast<std::uint32_t>(index)) != &child->m_Selected || child->m_Selected.m_EnclosingChild != &m_Selected.Child(static_cast<std::uint32_t>(index))) {
				throw std::runtime_error("motion child row refresh requires each exact live exclusive selected child");
			}
		}
		ReserveChildRows(children, particles);
	}

	void MotionStencilRowPolicies::ReserveChildRows(std::span<MotionStencilRowPolicies* const> children, std::span<const std::vector<MotionStencilRowPolicies*>> particles) {
		const auto nodes = m_Selected.Nodes();
		std::vector<std::size_t> contributions(nodes.size());
		for (std::size_t index = 0; index < nodes.size(); ++index) {
			const auto kind = nodes[index].Prepared->Type;
			if (kind == 0 || kind == 12) {
				contributions[index] = 1;
			} else if (kind == 3) {
				const auto* child = children[index];
				contributions[index] = child == nullptr ? 0 : child->m_CollectionCapacity;
			} else if (kind == 4) {
				for (const auto* particle : particles[index]) {
					if (particle == nullptr || particle->m_CollectionCapacity > kMaximumStencilPolicyMembers - contributions[index]) {
						throw std::runtime_error("motion emitted row collection exceeds its actual bounded child graph");
					}
					contributions[index] += particle->m_CollectionCapacity;
				}
			}
		}

		std::vector<MotionStencilRowPolicies*> bound;
		for (std::size_t index = 0; index < nodes.size(); ++index) {
			if (children[index] != nullptr) {
				if (bound.size() == kMaximumStencilPolicyRows || std::find(bound.begin(), bound.end(), children[index]) != bound.end()) {
					throw std::runtime_error("motion child row owner graph exceeds its exact unique bounded contexts");
				}
				bound.push_back(children[index]);
			}
			if (particles[index].size() > kMaximumStencilPolicyRows - bound.size()) {
				throw std::runtime_error("motion emitted row owner graph exceeds its bounded context count");
			}
			for (auto* particle : particles[index]) {
				if (std::find(bound.begin(), bound.end(), particle) != bound.end()) {
					throw std::runtime_error("motion emitted row owner graph duplicates an actual selected context");
				}
				bound.push_back(particle);
			}
		}

		// Repeated priority entries repeat that exact layer's contribution, not the largest unrelated child.
		std::size_t capacity = nodes.size();
		for (const auto& priority : m_Selected.m_Preparation.Priority) {
			std::size_t count = 0;
			for (const auto index : priority.DrawOrder) {
				if (index >= contributions.size() || contributions[index] > kMaximumStencilPolicyMembers - count) {
					throw std::runtime_error("motion recursive row collection exceeds its actual bounded priority graph");
				}
				count += contributions[index];
			}
			capacity = std::max(capacity, count);
		}
		std::size_t member_capacity = 0;
		std::vector<std::size_t> capacities(m_Rows.size());
		for (const auto& node : nodes) {
			if (!node.DrawRow) {
				continue;
			}
			std::size_t count = 0;
			if (node.Prepared->Type == 12) {
				count = 1;
				for (const auto* member : m_Selected.StencilMask(node.NativeOrdinal - 1).MaskLayers()) {
					if (member == nullptr || member->NativeOrdinal == 0 || member->NativeOrdinal > nodes.size() || &nodes[member->NativeOrdinal - 1] != member) {
						throw std::runtime_error("motion recursive mask reservation requires its actual resolved selected member");
					}
					const auto contribution = contributions[member->NativeOrdinal - 1];
					if (contribution > kMaximumStencilPolicyMembers - count) {
						throw std::runtime_error("motion recursive mask row membership exceeds its actual bounded member graph");
					}
					count += contribution;
				}
			} else if (node.Prepared->Type == 3) {
				count = contributions[node.NativeOrdinal - 1];
			}
			if (count > kMaximumStencilPolicyMembers - member_capacity) {
				throw std::runtime_error("motion recursive mask row membership exceeds its total storage bound");
			}
			member_capacity += count;
			capacities[*node.DrawRow] = count;
		}

		m_Published = false;
		m_CollectedRows.reserve(capacity);
		m_GlobalRows.reserve(capacity);
		for (std::size_t index = 0; index < m_Rows.size(); ++index) {
			m_Rows[index].m_Members.reserve(capacities[index]);
		}
		m_CollectionCapacity = capacity;
		std::copy(children.begin(), children.end(), m_ChildRows.begin());
		std::copy(particles.begin(), particles.end(), m_ParticleRows.begin());
		m_BoundRows = std::move(bound);
		for (auto* child : m_BoundRows) {
			if (child != nullptr) {
				child->m_BoundToContainingRows = true;
			}
		}
	}

	void MotionStencilRowPolicies::PrepareRecursiveFrame(std::span<const MotionComposedLayer> numeric) {
		m_Published = false;
		const auto nodes = m_Selected.Nodes();
		if (nodes.size() != m_NodeCount || numeric.size() != nodes.size()) {
			throw std::runtime_error("motion recursive publication requires its exact complete containing numeric phase");
		}
		std::uint64_t prepared_mesh_ancestry_serial = 0;
		if (m_MeshVertices != nullptr) {
			m_MeshVertices->RequireOwners();
			m_MeshVertices->m_Ancestry.RequirePhase();
			const auto actual_numeric = m_MeshVertices->m_Ancestry.m_Composition->Layers();
			if (numeric.data() != actual_numeric.data() || numeric.size() != actual_numeric.size()) {
				throw std::runtime_error("motion mesh row preparation requires its genuine current numeric owner");
			}
			prepared_mesh_ancestry_serial = m_MeshVertices->m_Ancestry.m_AncestrySerial;
		}
		for (std::size_t index = 0; index < nodes.size(); ++index) {
			if (&m_Ancestry.State(index).Node() != &nodes[index]) {
				throw std::runtime_error("motion recursive containing phase differs from its actual live stencil ancestry");
			}
		}
		if (m_PreparationSerial == std::numeric_limits<std::uint64_t>::max()) {
			throw std::runtime_error("motion recursive containing phase serial is exhausted");
		}
		if (m_ContainingRows != nullptr) {
			if (m_ContainingRows->m_PreparationSerial == 0) {
				throw std::runtime_error("motion child numeric preparation requires its actual containing preparation phase");
			}
			m_ContainingPreparationSerial = m_ContainingRows->m_PreparationSerial;
		}
		++m_PreparationSerial;
		m_PreparedNumeric = numeric;
		m_PreparedMeshAncestrySerial = prepared_mesh_ancestry_serial;
	}

	const MotionStencilRowState& MotionStencilRowPolicies::ResolveAncestorRow(const MotionStencilLayerState& ancestor) const {
		const auto nodes = m_Selected.Nodes();
		const auto& node = ancestor.Node();
		if (node.NativeOrdinal > 0 && node.NativeOrdinal <= nodes.size() && &nodes[node.NativeOrdinal - 1] == &node && node.DrawRow && *node.DrawRow < m_Rows.size()) {
			return m_Rows[*node.DrawRow];
		}
		const auto* inherited = m_Selected.m_EnclosingChild != nullptr ? m_Selected.m_EnclosingChild->InheritedStencil() : (m_Selected.m_EnclosingParticle != nullptr ? m_Selected.m_EnclosingParticle->InheritedStencil() : nullptr);
		if (m_ContainingRows == nullptr || inherited != &ancestor) {
			throw std::runtime_error("motion external stencil row must be its exact containing child transfer");
		}

		return m_ContainingRows->ResolveAncestorRow(ancestor);
	}

	void MotionStencilRowPolicies::Publish(std::span<const MotionComposedLayer> numeric, std::span<const std::size_t> current_indices, std::span<const MotionStaticSourceBinding* const> sources, float position, const MotionPriorityCursor& priority) {
		Publish(numeric, current_indices, sources, position, priority, false);
	}

	void MotionStencilRowPolicies::Publish(std::span<const MotionComposedLayer> numeric, std::span<const std::size_t> current_indices, std::span<const MotionStaticSourceBinding* const> sources, float position, const MotionPriorityCursor& priority, bool copied_snapshot) {
		m_Published = false;
		m_CollectedThisFrame = false;
		if (m_MeshVertices != nullptr) {
			m_MeshVertices->RequireOwners();
			m_MeshVertices->m_Ancestry.RequirePhase();
			if (m_PreparedMeshAncestrySerial != m_MeshVertices->m_Ancestry.m_AncestrySerial) {
				throw std::runtime_error("motion mesh rows require their current actual ancestry preparation");
			}
		}
		const auto nodes = m_Selected.Nodes();
		if (nodes.size() != m_NodeCount || m_Selected.DrawRowCount() != m_Rows.size() || nodes.size() != numeric.size() || nodes.size() != current_indices.size() || nodes.size() != sources.size() || !std::isfinite(position)) {
			throw std::runtime_error("motion stencil policies require every actual live numeric/current/source input");
		}
		if (m_ContainingRows != nullptr) {
			const auto containing_nodes = m_ContainingRows->m_Selected.Nodes();
			if (m_ContainingRows->m_PreparedNumeric.size() != containing_nodes.size() || m_ContainingLayer >= containing_nodes.size() || (m_Selected.m_EnclosingParticle != nullptr ? !m_ContainingRows->m_Selected.OwnsParticleSelection(static_cast<std::uint32_t>(m_ContainingLayer), m_Selected) : m_ContainingRows->m_Selected.ChildNodes(static_cast<std::uint32_t>(m_ContainingLayer)) != &m_Selected)) {
				throw std::runtime_error("motion child row phase requires its exact prepared containing numeric owner");
			}
			if (m_PreparationSerial == 0 || m_ContainingPreparationSerial != m_ContainingRows->m_PreparationSerial) {
				throw std::runtime_error("motion child row phase requires its current actual containing preparation");
			}
			// Actual 50ad30 does not recurse into an inactive kind3 node; even its child's row flags remain retained.
			if (!copied_snapshot && m_Selected.m_EnclosingParticle == nullptr && !m_ContainingRows->m_PreparedNumeric[m_ContainingLayer].Active) {
				m_PublishedNumeric = numeric;
				m_Published = true;
				return;
			}
		}
		if (!m_PreparedNumeric.empty() && (m_PreparedNumeric.data() != numeric.data() || m_PreparedNumeric.size() != numeric.size())) {
			throw std::runtime_error("motion recursive publication differs from its fixed prepared numeric phase");
		}
		if (&priority.Preparation() != &m_Selected.m_Preparation || !priority.State().Initialized || priority.DrawOrder().size() != nodes.size()) {
			throw std::runtime_error("motion recursive rows require their complete actual selected priority order");
		}
		m_CollectedRows.clear();
		m_GlobalRows.clear();
		for (auto& row : m_Rows) {
			row.m_Published = false;
		}

		for (const auto index : priority.DrawOrder()) {
			if (index >= nodes.size()) {
				throw std::runtime_error("motion recursive priority index exceeds its actual selected tree");
			}
			const auto& node = nodes[index];
			const auto& stencil = m_Ancestry.State(index);
			if (&stencil.Node() != &node || current_indices[index] >= node.Prepared->Frames.size() || numeric[index].FrameHidden != node.Prepared->Frames[current_indices[index]].Hidden) {
				throw std::runtime_error("motion stencil row policy differs from its exact selected ancestry/current frame");
			}
			if (node.Prepared->Type == 1 || node.Prepared->Type == 2 || node.Prepared->Type == 7) {
				if (sources[index] != nullptr) {
					throw std::runtime_error("motion structural stencil policy does not consume a drawing source");
				}
				continue;
			}
			if (node.Prepared->Type == 4) {
				if (sources[index] != nullptr || node.DrawRow || m_ParticleRows[index].size() != m_Selected.Particles(index).Size()) {
					throw std::runtime_error("motion emitter rows require its exact non-drawing owner and all surviving particle rows");
				}
				// Native50ae1f visits forward surviving players before testing the emitter's Active flag.
				for (std::size_t occurrence = 0; occurrence < m_ParticleRows[index].size(); ++occurrence) {
					const auto* particle = m_ParticleRows[index][occurrence];
					if (particle == nullptr || !particle->m_Published || particle->m_ContainingRows != this || particle->m_ContainingLayer != index || particle->m_ContainingPreparationSerial != m_PreparationSerial || !m_Selected.OwnsParticleSelection(index, occurrence, particle->m_Selected)) {
						throw std::runtime_error("motion particle rows require each exact completed emitted selected owner");
					}
					if (particle->m_CollectedRows.size() > m_CollectionCapacity - m_CollectedRows.size() || particle->m_GlobalRows.size() > m_CollectionCapacity - m_GlobalRows.size()) {
						throw std::runtime_error("motion emitted row collection exceeds its actual prepared priority capacity");
					}
					m_CollectedRows.insert(m_CollectedRows.end(), particle->m_CollectedRows.begin(), particle->m_CollectedRows.end());
					m_GlobalRows.insert(m_GlobalRows.end(), particle->m_GlobalRows.begin(), particle->m_GlobalRows.end());
				}
				continue;
			}
			if (node.Prepared->Type == 3) {
				if (sources[index] != nullptr) {
					throw std::runtime_error("motion child row collection cannot consume a sprite source binding");
				}
				if (!numeric[index].Active) {
					continue;
				}
				auto* child = m_ChildRows[index];
				std::span<const MotionStencilRowState* const> child_collected;
				std::span<const MotionStencilRowState* const> child_global;
				if (child == nullptr) {
					const auto* selected_child = m_Selected.ChildNodes(index);
					const auto& names = m_Selected.Child(index).Commands().Names();
					if ((selected_child != nullptr && !selected_child->Nodes().empty()) || !names.CurrentMotion.empty() || !names.RequestedMotion.empty()) {
						throw std::runtime_error("motion child rows require this exact completed recursive selected owner");
					}
					// Native50ad30 visits the genuine root-only player, contributing no child rows. Its containing mask row still publishes below.
				} else {
					if (m_Selected.ChildNodes(index) != &child->m_Selected || !child->m_Published || child->m_ContainingPreparationSerial != m_PreparationSerial) {
						throw std::runtime_error("motion child rows require this exact completed recursive selected owner");
					}
					child_collected = child->m_CollectedRows;
					child_global = child->m_GlobalRows;
				}
				if (child_collected.size() > m_CollectionCapacity - m_CollectedRows.size() || child_global.size() > m_CollectionCapacity - m_GlobalRows.size()) {
					throw std::runtime_error("motion recursive collection exceeds its prepared bounded row storage");
				}
				m_CollectedRows.insert(m_CollectedRows.end(), child_collected.begin(), child_collected.end());
				m_GlobalRows.insert(m_GlobalRows.end(), child_global.begin(), child_global.end());
				if (stencil.Active() || node.CompositeMask) {
					auto& row = m_Rows[*node.DrawRow];
					row.m_Published = true;
					row.m_StencilType = node.Prepared->StencilType;
					row.m_Ancestor = stencil.Ancestor() == nullptr ? nullptr : &ResolveAncestorRow(*stencil.Ancestor());
					row.m_ControlEnabled = false;
					row.m_Scale = 1;
					row.m_Offset = 0;
					if (child_collected.size() > row.m_Members.capacity()) {
						throw std::runtime_error("motion child mask members exceed their prepared bounded storage");
					}
					row.m_Members.assign(child_collected.begin(), child_collected.end());
				}
				continue;
			}
			if (sources[index] == nullptr || !node.DrawRow || *node.DrawRow >= m_Rows.size()) {
				throw std::runtime_error("motion stencil row policy requires its actual drawing source and reserved row");
			}
			const MotionStencilMaskOwner* mask = nullptr;
			if (node.Prepared->Type == 12) {
				mask = &m_Selected.StencilMask(static_cast<std::uint32_t>(index));
				// Native50bc40 uses the bound record position;50ad30 consumes that same retained summary.
				const auto layer_position = m_Selected.m_LocalNumericOwner == nullptr ? position : m_Selected.m_LocalNumericOwner->Position(static_cast<std::uint32_t>(index));
				if (stencil.MaskOwner() != mask || mask->EvaluatedFrameIndex() != current_indices[index] || mask->EvaluatedPosition() != layer_position) {
					throw std::runtime_error("motion stencil row requires its exact current retained mask owner");
				}
			}
			const auto& frame = node.Prepared->Frames[current_indices[index]];
			const auto& source = sources[index]->State();
			if (!frame.Hidden && !source.External && (source.Source != StencilRowSourceName(frame.Attributes.Source) || source.Icon != StencilRowSourceName(frame.Attributes.Icon))) {
				throw std::runtime_error("motion stencil row source differs from its exact current frame");
			}
			if (!numeric[index].Active || !source.Ready) {
				continue;
			}

			auto& row = m_Rows[*node.DrawRow];
			row.m_Published = true;
			row.m_StencilType = node.Prepared->StencilType;
			row.m_Ancestor = nullptr;
			if (const auto* ancestor = stencil.Ancestor()) {
				row.m_Ancestor = &ResolveAncestorRow(*ancestor);
			}
			// Disabled stencil controls retain the persistent row's previous coefficients.
			row.m_ControlEnabled = false;
			if (mask != nullptr) {
				const auto& summary = mask->Summary();
				row.m_ControlEnabled = summary.Enabled;
				if (summary.Enabled) {
					row.m_Scale = summary.Scale;
					row.m_Offset = summary.Offset;
					if (summary.ReverseWrite) {
						const auto operation = row.m_StencilType & 3u;
						if (operation == 1 || operation == 2) {
							row.m_StencilType = (row.m_StencilType & ~3u) | (3u - operation);
						}
					}
				}
			}
			if (m_CollectedRows.size() == m_CollectionCapacity || (node.Prepared->Type == 0 && m_GlobalRows.size() == m_CollectionCapacity)) {
				throw std::runtime_error("motion drawing row exceeds its prepared bounded collection storage");
			}
			m_CollectedRows.push_back(&row);
			if (node.Prepared->Type == 0) {
				m_GlobalRows.push_back(&row);
			}
		}

		for (std::size_t index = 0; index < nodes.size(); ++index) {
			const auto& node = nodes[index];
			if (node.Prepared->Type != 12 || !node.DrawRow) {
				continue;
			}
			auto& row = m_Rows[*node.DrawRow];
			if (!row.m_Published || (node.Prepared->StencilType & 4u) == 0) {
				continue;
			}
			row.m_Members.clear();
			row.m_Members.push_back(&row);
			for (const auto* member : m_Selected.StencilMask(static_cast<std::uint32_t>(index)).MaskLayers()) {
				if (member == nullptr || member->NativeOrdinal == 0 || member->NativeOrdinal > nodes.size() || &nodes[member->NativeOrdinal - 1] != member || !member->DrawRow || *member->DrawRow >= m_Rows.size()) {
					throw std::runtime_error("motion stencil member must retain its actual resolved selected drawing row");
				}
				const auto& member_row = m_Rows[*member->DrawRow];
				if (member_row.m_Published) {
					if (member->Prepared->Type == 0) {
						if (row.m_Members.size() == row.m_Members.capacity()) {
							throw std::runtime_error("motion sprite mask members exceed their prepared bounded storage");
						}
						row.m_Members.push_back(&member_row);
					} else if (member->Prepared->Type == 3) {
						if (member_row.m_Members.size() > row.m_Members.capacity() - row.m_Members.size()) {
							throw std::runtime_error("motion recursive mask members exceed their prepared bounded storage");
						}
						row.m_Members.insert(row.m_Members.end(), member_row.m_Members.begin(), member_row.m_Members.end());
					} else {
						throw std::runtime_error("motion composite mask members require actual sprite or child row collections");
					}
				}
			}
		}
		m_PublishedNumeric = numeric;
		m_CollectedThisFrame = true;
		m_Published = true;
	}

	void MotionStencilRowPolicies::RequireLive(const MotionStencilRowState& state) const {
		const auto nodes = m_Selected.Nodes();
		if (!m_Published || nodes.size() != m_NodeCount || m_Selected.DrawRowCount() != m_Rows.size() || state.m_Owner != this || state.m_Node == nullptr || state.m_Node->NativeOrdinal == 0) {
			throw std::runtime_error("motion stencil row state requires its exact live published selected owner");
		}
		const auto ordinal = state.m_Node->NativeOrdinal;
		if (ordinal > nodes.size() || &nodes[ordinal - 1] != state.m_Node || !state.m_Node->DrawRow || *state.m_Node->DrawRow >= m_Rows.size() || &m_Rows[*state.m_Node->DrawRow] != &state) {
			throw std::runtime_error("motion stencil row identity differs from its actual persistent selected row");
		}
	}

	const MotionStencilRowState& MotionStencilRowPolicies::Row(std::size_t index) const {
		if (index >= m_Rows.size()) {
			throw std::runtime_error("motion stencil row index exceeds its actual reserved storage");
		}
		RequireLive(m_Rows[index]);

		return m_Rows[index];
	}

	std::size_t MotionStencilRowPolicies::Size() const noexcept {
		return m_Rows.size();
	}

	std::span<const MotionStencilRowState* const> MotionStencilRowPolicies::CollectedRows() const {
		if (!m_Published || m_Selected.Nodes().size() != m_NodeCount) {
			throw std::runtime_error("motion recursive row collection requires its live completed selected owner");
		}

		return m_CollectedRows;
	}

	std::span<const MotionStencilRowState* const> MotionStencilRowPolicies::GlobalRows() const {
		static_cast<void>(CollectedRows());

		return m_GlobalRows;
	}

	const MotionSelectedLayerNode& MotionStencilRowState::Node() const {
		if (m_Owner == nullptr) {
			throw std::runtime_error("motion stencil row state has no actual selected owner");
		}
		m_Owner->RequireLive(*this);

		return *m_Node;
	}

	bool MotionStencilRowState::Published() const {
		static_cast<void>(Node());

		return m_Published;
	}

	std::uint32_t MotionStencilRowState::StencilType() const {
		static_cast<void>(Node());

		return m_StencilType;
	}

	bool MotionStencilRowState::ControlEnabled() const {
		static_cast<void>(Node());

		return m_ControlEnabled;
	}

	float MotionStencilRowState::Scale() const {
		static_cast<void>(Node());

		return m_Scale;
	}

	float MotionStencilRowState::Offset() const {
		static_cast<void>(Node());

		return m_Offset;
	}

	const MotionStencilRowState* MotionStencilRowState::Ancestor() const {
		static_cast<void>(Node());

		return m_Ancestor;
	}

	std::span<const MotionStencilRowState* const> MotionStencilRowState::Members() const {
		static_cast<void>(Node());

		return m_Members;
	}

} // namespace sgre::scene
