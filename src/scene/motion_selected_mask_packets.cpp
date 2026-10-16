#include "sgre/scene/motion_selected_mask_packets.hpp"

#include <algorithm>
#include <cstdint>
#include <stdexcept>

namespace sgre::scene {

	static constexpr std::size_t kMaximumMaskPacketRows = 65536;
	static constexpr std::size_t kMaximumMaskPacketMembers = 1048576;

	MotionSelectedMaskPackets::MotionSelectedMaskPackets(std::span<const MotionMaskOwnerFrame> owners) {
		if (owners.empty() || owners.size() > kMaximumMaskPacketRows) {
			throw std::runtime_error("motion mask packets require a bounded actual owner graph");
		}
		if (owners[0].Rows.m_ContainingRows != nullptr || owners[0].Geometry.m_Containing != nullptr) {
			throw std::runtime_error("motion mask packet graph must start at its actual ultimate row/geometry owner");
		}
		for (std::size_t index = 0; index < owners.size(); ++index) {
			const auto& owner = owners[index];
			if (&owner.Rows.m_Selected != &owner.Geometry.m_Selected || &owner.Rows != &owner.Geometry.m_Rows) {
				throw std::runtime_error("motion mask packets require exact selected row and geometry context identities");
			}
			if (index > 0) {
				bool containing = false;
				for (std::size_t parent = 0; parent < index; ++parent) {
					if (owner.Rows.m_ContainingRows == &owners[parent].Rows && owner.Geometry.m_Containing == &owners[parent].Geometry && owners[parent].Rows.HasBoundRows(owner.Rows)) {
						containing = true;
						break;
					}
				}
				if (!containing) {
					throw std::runtime_error("motion mask packets require the exact bound containing child graph");
				}
			}
			for (const auto* child : owner.Rows.m_BoundRows) {
				if (child != nullptr && std::none_of(owners.begin(), owners.end(), [&](const auto& entry) {
					    return &entry.Rows == child;
				    })) {
					throw std::runtime_error("motion mask packet graph cannot omit an actual bound selected child owner");
				}
			}
		}
		std::size_t rows = 0;
		for (const auto& owner : owners) {
			if (owner.Rows.Size() > kMaximumMaskPacketRows - rows) {
				throw std::runtime_error("motion mask packet rows exceed their total bound");
			}
			rows += owner.Rows.Size();
			m_Owners.push_back(&owner.Rows);
		}
		m_PreparationSerials.resize(owners.size());
		m_Bindings.reserve(rows);
		m_Nodes.resize(rows);
		m_Settings.resize(rows);
		m_Members.resize(rows);
		std::size_t members = 0;
		for (std::size_t owner_index = 0; owner_index < owners.size(); ++owner_index) {
			const auto& owner = owners[owner_index];
			for (std::size_t row_index = 0; row_index < owner.Rows.Size(); ++row_index) {
				const auto& row = owner.Rows.Row(row_index);
				const auto layer = row.Node().NativeOrdinal - 1;
				if (&owner.Geometry.Node(layer) != &row.Node()) {
					throw std::runtime_error("motion mask geometry must belong to this exact selected row owner");
				}
				if (row.m_Members.capacity() > kMaximumMaskPacketMembers - members) {
					throw std::runtime_error("motion mask packets exceed their total member storage bound");
				}
				members += row.m_Members.capacity();
				Binding binding;
				binding.Row = &row;
				binding.Geometry = &owner.Geometry;
				binding.Owner = owner_index;
				binding.Layer = layer;
				m_Bindings.push_back(binding);
				m_Members[m_Bindings.size() - 1].reserve(row.m_Members.capacity());
				if (!m_RowIndices.emplace(&row, m_Bindings.size() - 1).second) {
					throw std::runtime_error("motion mask packet graph duplicates an actual persistent row owner");
				}
			}
		}
	}

	std::size_t MotionSelectedMaskPackets::Index(const MotionStencilRowState& row) const {
		static_cast<void>(row.Node());
		const auto found = m_RowIndices.find(&row);
		if (found == m_RowIndices.end()) {
			throw std::runtime_error("motion mask row reference has no actual bound packet owner");
		}

		return found->second;
	}

	void MotionSelectedMaskPackets::Build(std::span<const MotionMaskOwnerFrame> owners) {
		m_Built = false;
		if (owners.size() != m_Owners.size()) {
			throw std::runtime_error("motion mask packet phase differs from its prepared actual owner graph");
		}
		for (std::size_t index = 0; index < owners.size(); ++index) {
			if (!owners[index].Rows.m_Published) {
				throw std::runtime_error("motion mask packets require their exact live published selected owner");
			}
			owners[index].Geometry.RequirePhase();
			if (&owners[index].Rows != m_Owners[index] || owners[index].Numeric.data() != owners[index].Geometry.m_Numeric.data() || owners[index].Numeric.size() != owners[index].Geometry.m_Numeric.size() || owners[index].Numeric.data() != owners[index].Rows.m_PublishedNumeric.data() || owners[index].Numeric.size() != owners[index].Rows.m_PublishedNumeric.size()) {
				throw std::runtime_error("motion mask packet owner identity differs from its prepared graph");
			}
		}

		for (std::size_t index = 0; index < m_Bindings.size(); ++index) {
			const auto& binding = m_Bindings[index];
			const auto& row = *binding.Row;
			const auto& node = row.Node();
			const auto& owner = owners[binding.Owner];
			if (&owner.Geometry != binding.Geometry || &owner.Geometry.Node(binding.Layer) != &node || binding.Layer >= owner.Numeric.size() || row.StencilType() > 7) {
				throw std::runtime_error("motion mask packet phase requires its exact geometry/numeric node and recovered stencil flags");
			}
			auto& packet = m_Nodes[index];
			packet.ModeFlags = static_cast<std::uint8_t>(row.StencilType());
			packet.Parent = row.Ancestor() == nullptr ? std::nullopt : std::optional<std::uint32_t>(static_cast<std::uint32_t>(Index(*row.Ancestor())));
			if (owner.Rows.m_CollectedThisFrame && row.Published() && node.Prepared->Type != 3) {
				if (!owner.Geometry.Allocated(binding.Layer)) {
					throw std::runtime_error("motion published mask row requires actual owned source-derived geometry");
				}
				packet.Command.Geometry = &owner.Geometry.Geometry(binding.Layer);
				packet.Command.GeometryCount = owner.Numeric[binding.Layer].Hierarchy.Attributes.Opacity;
				packet.Command.Hidden = false;
			}
			if (node.Prepared->Type == 3 && (row.StencilType() & 3u) != 0 && (row.StencilType() & 4u) == 0) {
				throw std::runtime_error("motion non-composite child stencil requires its actual direct geometry consumer");
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
					throw std::runtime_error("motion mask packet member requires its actual prepared stable row capacity");
				}
				members.push_back(m_Nodes[Index(*row)].Command);
			}
			m_Nodes[index].Members = members;
		}
		for (std::size_t index = 0; index < owners.size(); ++index) {
			m_PreparationSerials[index] = owners[index].Rows.m_PreparationSerial;
		}
		m_Built = true;
	}

	void MotionSelectedMaskPackets::RequireLive() const {
		if (!m_Built) {
			throw std::runtime_error("motion mask packets require a completed actual geometry binding phase");
		}
		for (std::size_t index = 0; index < m_Owners.size(); ++index) {
			if (m_Owners[index]->m_PreparationSerial != m_PreparationSerials[index]) {
				throw std::runtime_error("motion mask packets require their completed actual prepared packet phase");
			}
			if (!m_Owners[index]->m_Published) {
				throw std::runtime_error("motion mask packets require their exact live published selected owner");
			}
		}
		for (const auto& binding : m_Bindings) {
			static_cast<void>(binding.Row->Node());
			static_cast<void>(binding.Geometry->Node(binding.Layer));
		}
	}

	std::span<const MotionClipNode> MotionSelectedMaskPackets::Nodes() const {
		RequireLive();

		return m_Nodes;
	}

	std::span<const MotionMaskSettings> MotionSelectedMaskPackets::Settings() const {
		RequireLive();

		return m_Settings;
	}

} // namespace sgre::scene
