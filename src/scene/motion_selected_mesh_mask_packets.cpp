#include "sgre/scene/motion_selected_mesh_mask_packets.hpp"

#include "sgre/scene/motion_layer_composition.hpp"
#include "sgre/scene/motion_selected_layer_sources.hpp"

#include <algorithm>
#include <stdexcept>

namespace sgre::scene {

	static constexpr std::size_t kMaximumMeshMaskRows = 65536;
	static constexpr std::size_t kMaximumMeshMaskMembers = 1048576;

	MotionSelectedMeshMaskPackets::MotionSelectedMeshMaskPackets(std::span<const MotionMeshMaskOwner> owners) {
		if (owners.empty() || owners.size() > kMaximumMeshMaskRows || owners[0].Rows.m_ContainingRows != nullptr || owners[0].Vertices.m_Ancestry.m_Containing != nullptr) {
			throw std::runtime_error("motion mesh mask packets require a bounded actual ultimate owner graph");
		}
		std::size_t row_count = 0;
		std::size_t member_count = 0;
		for (std::size_t index = 0; index < owners.size(); ++index) {
			const auto& owner = owners[index];
			for (std::size_t prior = 0; prior < index; ++prior) {
				if (&owner.Rows == &owners[prior].Rows || &owner.Vertices == &owners[prior].Vertices) {
					throw std::runtime_error("motion mesh mask graph duplicates an actual selected context");
				}
			}
			owner.Vertices.RequireOwners();
			if (&owner.Rows.m_Selected != &owner.Vertices.m_Selected || owner.Vertices.m_RowOwner != nullptr || owner.Rows.m_MeshVertices != nullptr || owner.Rows.Size() > kMaximumMeshMaskRows - row_count) {
				throw std::runtime_error("motion mesh mask packets require each exact exclusive selected row/vertex context");
			}
			if (index > 0) {
				bool containing = false;
				for (std::size_t parent = 0; parent < index; ++parent) {
					if (owner.Rows.m_ContainingRows == &owners[parent].Rows && owner.Vertices.m_Ancestry.m_Containing == &owners[parent].Vertices.m_Ancestry && owners[parent].Rows.HasBoundRows(owner.Rows)) {
						containing = true;
						break;
					}
				}
				if (!containing) {
					throw std::runtime_error("motion mesh mask packets require the exact containing child row/deformation graph");
				}
			}
			for (const auto* child : owner.Rows.m_BoundRows) {
				if (child != nullptr && std::none_of(owners.begin(), owners.end(), [&](const auto& entry) {
					    return &entry.Rows == child;
				    })) {
					throw std::runtime_error("motion mesh mask packets cannot omit a bound actual selected child");
				}
			}
			row_count += owner.Rows.Size();
			for (const auto& row : owner.Rows.m_Rows) {
				if (row.m_Node == nullptr || row.m_Node->NativeOrdinal == 0 || row.m_Node->NativeOrdinal > owner.Vertices.m_Entries.size() || owner.Vertices.m_Entries[row.m_Node->NativeOrdinal - 1].Node != row.m_Node || row.m_Members.capacity() > kMaximumMeshMaskMembers - member_count) {
					throw std::runtime_error("motion mesh mask packets require genuine bounded persistent row/vertex identities");
				}
				member_count += row.m_Members.capacity();
			}
		}
		m_Owners.reserve(owners.size());
		for (const auto& owner : owners) {
			m_Owners.emplace_back(owner.Rows, owner.Vertices);
		}
		m_PreparationSerials.resize(owners.size());
		m_VertexSerials.resize(owners.size());
		m_Bindings.reserve(row_count);
		m_Nodes.resize(row_count);
		m_Settings.resize(row_count);
		m_Members.resize(row_count);
		for (std::size_t owner_index = 0; owner_index < m_Owners.size(); ++owner_index) {
			for (const auto& row : m_Owners[owner_index].Rows.m_Rows) {
				Binding binding;
				binding.Row = &row;
				binding.Owner = owner_index;
				binding.Layer = row.m_Node->NativeOrdinal - 1;
				m_Bindings.push_back(binding);
				m_Members[m_Bindings.size() - 1].reserve(row.m_Members.capacity());
				if (!m_RowIndices.emplace(&row, m_Bindings.size() - 1).second) {
					throw std::runtime_error("motion mesh mask graph duplicates a persistent selected row");
				}
			}
		}
		for (const auto& owner : m_Owners) {
			owner.Vertices.m_RowOwner = &owner.Rows;
			owner.Rows.m_MeshVertices = &owner.Vertices;
		}
	}

	MotionSelectedMeshMaskPackets::~MotionSelectedMeshMaskPackets() noexcept {
		for (const auto& owner : m_Owners) {
			if (owner.Vertices.m_RowOwner == &owner.Rows) {
				owner.Vertices.m_RowOwner = nullptr;
				owner.Rows.m_MeshVertices = nullptr;
			}
		}
	}

	void MotionSelectedMeshMaskPackets::RequireOwnerPhase(std::size_t index) const {
		const auto& owner = m_Owners[index];
		for (const auto* child : owner.Rows.m_BoundRows) {
			if (child != nullptr && std::none_of(m_Owners.begin(), m_Owners.end(), [&](const auto& entry) {
				    return &entry.Rows == child;
			    })) {
				throw std::runtime_error("motion mesh mask graph changed after its exact owner binding");
			}
		}
		owner.Vertices.RequireOwners();
		owner.Vertices.m_Ancestry.RequirePhase();
		const auto numeric = owner.Vertices.m_Ancestry.m_Composition->Layers();
		if (owner.Vertices.m_RowOwner != &owner.Rows || owner.Rows.m_MeshVertices != &owner.Vertices || owner.Rows.m_PreparedMeshAncestrySerial != owner.Vertices.m_Ancestry.m_AncestrySerial || !owner.Rows.m_Published || !owner.Vertices.m_Composed || owner.Vertices.m_PreparationSerial != owner.Vertices.m_Ancestry.m_AncestrySerial || owner.Vertices.m_RowPreparationSerial != owner.Rows.m_PreparationSerial || numeric.data() != owner.Rows.m_PublishedNumeric.data() || numeric.size() != owner.Rows.m_PublishedNumeric.size()) {
			throw std::runtime_error("motion mesh mask packets require the same genuine completed row/numeric/vertex preparation");
		}
		// Empty child trees still have a real numeric and geometry phase; a zero row count cannot bypass it.
		for (std::uint32_t layer = 0; layer < owner.Vertices.m_Entries.size(); ++layer) {
			// This loop performs only local cell checks after the callback-bearing owner preflight above.
			owner.Vertices.RequireCellPhase(layer);
		}
	}

	std::size_t MotionSelectedMeshMaskPackets::Index(const MotionStencilRowState& row) const {
		static_cast<void>(row.Node());
		const auto found = m_RowIndices.find(&row);
		if (found == m_RowIndices.end()) {
			throw std::runtime_error("motion mesh mask row has no exact bound packet owner");
		}

		return found->second;
	}

	void MotionSelectedMeshMaskPackets::Build() {
		m_Built = false;
		for (std::size_t index = 0; index < m_Owners.size(); ++index) {
			RequireOwnerPhase(index);
		}
		for (std::size_t index = 0; index < m_Bindings.size(); ++index) {
			const auto& binding = m_Bindings[index];
			const auto& row = *binding.Row;
			const auto& node = row.Node();
			const auto& owner = m_Owners[binding.Owner];
			if (&owner.Vertices.Node(binding.Layer) != &node || row.StencilType() > 7) {
				throw std::runtime_error("motion mesh mask packet differs from its actual node or recovered stencil domain");
			}
			auto& packet = m_Nodes[index];
			packet.ModeFlags = static_cast<std::uint8_t>(row.StencilType());
			packet.Parent = row.Ancestor() == nullptr ? std::nullopt : std::optional<std::uint32_t>(static_cast<std::uint32_t>(Index(*row.Ancestor())));
			if (owner.Rows.m_CollectedThisFrame && row.Published() && node.Prepared->Type != 3) {
				const auto& source = owner.Vertices.m_Ancestry.m_Sources.State(binding.Layer);
				packet.Command.Geometry = source.Blank && !owner.Vertices.Allocated(binding.Layer) ? nullptr : &owner.Vertices.Geometry(binding.Layer);
				packet.Command.GeometryCount = owner.Vertices.m_Ancestry.m_Composition->Layers()[binding.Layer].Hierarchy.Attributes.Opacity;
				packet.Command.Hidden = source.Blank.has_value() || packet.Command.Geometry == nullptr;
			}
			if (node.Prepared->Type == 3 && (row.StencilType() & 3u) != 0 && (row.StencilType() & 4u) == 0) {
				throw std::runtime_error("motion child stencil requires its actual direct geometry consumer");
			}
			auto& settings = m_Settings[index];
			settings.Operation = row.ControlEnabled() ? 1 : 0;
			settings.ThresholdMinimum = row.Scale();
			settings.ThresholdExtent = row.Offset();
		}
		for (std::size_t index = 0; index < m_Bindings.size(); ++index) {
			auto& members = m_Members[index];
			members.clear();
			for (const auto* row : m_Bindings[index].Row->Members()) {
				if (row == nullptr || members.size() == members.capacity()) {
					throw std::runtime_error("motion mesh mask membership exceeds its actual stable prepared row storage");
				}
				members.push_back(m_Nodes[Index(*row)].Command);
			}
			m_Nodes[index].Members = members;
		}
		for (std::size_t index = 0; index < m_Owners.size(); ++index) {
			m_PreparationSerials[index] = m_Owners[index].Rows.m_PreparationSerial;
			m_VertexSerials[index] = m_Owners[index].Vertices.m_VertexSerial;
		}
		m_Built = true;
	}

	void MotionSelectedMeshMaskPackets::RequireLive() const {
		if (!m_Built) {
			throw std::runtime_error("motion mesh mask packets require completed actual geometry binding");
		}
		for (std::size_t index = 0; index < m_Owners.size(); ++index) {
			RequireOwnerPhase(index);
			if (m_PreparationSerials[index] != m_Owners[index].Rows.m_PreparationSerial || m_VertexSerials[index] != m_Owners[index].Vertices.m_VertexSerial) {
				throw std::runtime_error("motion mesh mask packet borrows expired at the next actual row preparation");
			}
		}
	}

	std::span<const MotionClipNode> MotionSelectedMeshMaskPackets::Nodes() const {
		RequireLive();

		return m_Nodes;
	}

	std::span<const MotionMaskSettings> MotionSelectedMeshMaskPackets::Settings() const {
		RequireLive();

		return m_Settings;
	}

} // namespace sgre::scene
