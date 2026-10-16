#include "sgre/scene/motion_selected_quad_geometry.hpp"

#include "sgre/scene/motion_draw.hpp"
#include "sgre/scene/motion_stencil_row_policies.hpp"

#include <cmath>
#include <stdexcept>
#include <string_view>

namespace sgre::scene {

	static constexpr std::size_t kMaximumSelectedQuadLayers = 65536;

	static std::string_view QuadSourceName(std::string_view name) {
		return name.substr(0, name.find('\0'));
	}

	MotionSelectedQuadGeometry::MotionSelectedQuadGeometry(MotionSelectedLayerNodes& selected, MotionStencilRowPolicies& rows, const MotionSelectedQuadGeometry* containing)
	    : m_Selected(selected), m_Rows(rows), m_Containing(containing) {
		const auto nodes = selected.Nodes();
		if (&rows.m_Selected != &selected || (containing == nullptr ? rows.m_ContainingRows != nullptr : rows.m_ContainingRows != &containing->m_Rows) || nodes.size() > kMaximumSelectedQuadLayers || (selected.m_EnclosingChild == nullptr) != (containing == nullptr)) {
			throw std::runtime_error("motion quad geometry requires its bounded exact selected and containing owners");
		}
		if (selected.m_EnclosingChild != nullptr && selected.m_EnclosingChild->InheritedClip() != nullptr) {
			throw std::runtime_error("motion selected quads require the recovered absent inherited clip family");
		}
		if (containing != nullptr) {
			bool exact_child = false;
			for (const auto& node : containing->m_Selected.Nodes()) {
				if (node.Prepared->Type == 3 && containing->m_Selected.ChildNodes(node.NativeOrdinal - 1) == &selected && &containing->m_Selected.Child(node.NativeOrdinal - 1) == selected.m_EnclosingChild) {
					exact_child = true;
					break;
				}
			}
			if (!exact_child) {
				throw std::runtime_error("motion quad geometry requires this exact exclusive selected child context");
			}
		} else if (selected.m_Root.MatrixMode() != MotionRootMatrixMode::Recomputed) {
			throw std::runtime_error("motion standalone quad geometry needs its actual recomputed root or containing producer");
		}

		m_Entries.resize(nodes.size());
		for (std::size_t index = 0; index < nodes.size(); ++index) {
			const auto& layer = *nodes[index].Prepared;
			if ((layer.Type != 0 && layer.Type != 2 && layer.Type != 3 && layer.Type != 7 && layer.Type != 12) || layer.MeshTransform != 0 || layer.MeshCombinator) {
				throw std::runtime_error("motion selected quads require every actual undeformed supported layer; inherited mesh ownership remains open");
			}
			auto& entry = m_Entries[index];
			entry.Node = &nodes[index];
			entry.Geometry.Positions = entry.Positions;
		}
	}

	void MotionSelectedQuadGeometry::Compose(std::span<const MotionComposedLayer> numeric, std::span<const std::size_t> current_indices, std::span<const MotionStaticSourceBinding* const> sources, std::span<const std::uint8_t> changed, float depth_projection) {
		m_Composed = false;
		const auto nodes = m_Selected.Nodes();
		if (nodes.size() != m_Entries.size() || numeric.size() != nodes.size() || current_indices.size() != nodes.size() || sources.size() != nodes.size() || changed.size() != nodes.size() || !std::isfinite(depth_projection)) {
			throw std::runtime_error("motion selected quad phase requires every actual numeric/current/source/change input");
		}
		if (m_Rows.m_PreparationSerial == 0 || m_Rows.m_PreparedNumeric.data() != numeric.data() || m_Rows.m_PreparedNumeric.size() != numeric.size()) {
			throw std::runtime_error("motion selected quad geometry requires its exact actual prepared row numeric phase");
		}
		if (m_Containing != nullptr && (!m_Containing->m_Composed || m_Containing->m_ComposedSerial != m_Containing->m_Rows.m_PreparationSerial || m_Rows.m_ContainingRows != &m_Containing->m_Rows || m_Rows.m_ContainingPreparationSerial != m_Containing->m_Rows.m_PreparationSerial)) {
			throw std::runtime_error("motion child quad geometry requires its completed actual containing geometry phase");
		}

		if (m_Containing != nullptr) {
			m_Containing->RequirePhase();
		}

		for (std::size_t index = 0; index < nodes.size(); ++index) {
			const auto& node = nodes[index];
			if (&node != m_Entries[index].Node || current_indices[index] >= node.Prepared->Frames.size() || changed[index] > 1 || numeric[index].FrameHidden != node.Prepared->Frames[current_indices[index]].Hidden) {
				throw std::runtime_error("motion selected quad current input differs from its exact live selected node");
			}
			if (node.Prepared->Type == 2 || node.Prepared->Type == 3 || node.Prepared->Type == 7) {
				if (sources[index] != nullptr) {
					throw std::runtime_error("motion structural and child nodes do not own ordinary source quads");
				}
				continue;
			}
			if (sources[index] == nullptr) {
				throw std::runtime_error("motion selected quad requires its actual drawing source binding");
			}
			const auto& source = sources[index]->State();
			const auto& frame = node.Prepared->Frames[current_indices[index]];
			if (!frame.Hidden && (source.Source != QuadSourceName(frame.Attributes.Source) || source.Icon != QuadSourceName(frame.Attributes.Icon))) {
				throw std::runtime_error("motion selected quad source differs from the actual current frame");
			}
			if (!changed[index] || frame.Hidden || !numeric[index].Active || !source.Ready) {
				continue;
			}
			if (!source.Binding || !source.Texture || source.Binding->Icon == nullptr || source.Binding->Source == nullptr || source.Texture->Slot.Owner == nullptr || source.Texture->Texture.Owner == nullptr) {
				throw std::runtime_error("motion selected quad needs its genuine retained icon and registered texture owner");
			}

			auto& entry = m_Entries[index];
			entry.Positions = EvaluateMotionSpriteQuad(numeric[index].Hierarchy, *source.Binding->Icon, depth_projection);
			entry.Geometry.CachedBounds = MotionBounds{};
			entry.Geometry.Calculated = false;
			entry.Allocated = true;
		}
		m_ComposedSerial = m_Rows.m_PreparationSerial;
		m_ContainingSerial = m_Containing == nullptr ? 0 : m_Containing->m_Rows.m_PreparationSerial;
		m_Numeric = numeric;
		m_Composed = true;
	}

	void MotionSelectedQuadGeometry::RequirePhase() const {
		if (!m_Composed || m_ComposedSerial != m_Rows.m_PreparationSerial || m_Selected.Nodes().size() != m_Entries.size() || (m_Containing != nullptr && m_ContainingSerial != m_Containing->m_Rows.m_PreparationSerial)) {
			throw std::runtime_error("motion selected quad geometry requires its actual live completed selected owner");
		}
		if (m_Containing != nullptr) {
			m_Containing->RequirePhase();
		}
	}

	void MotionSelectedQuadGeometry::RequireLive(std::size_t layer) const {
		RequirePhase();
		const auto nodes = m_Selected.Nodes();
		if (layer >= nodes.size() || &nodes[layer] != m_Entries[layer].Node) {
			throw std::runtime_error("motion selected quad geometry requires its actual live completed selected owner");
		}
	}

	bool MotionSelectedQuadGeometry::Allocated(std::size_t layer) const {
		RequireLive(layer);

		return m_Entries[layer].Allocated;
	}

	MotionClipGeometry& MotionSelectedQuadGeometry::Geometry(std::size_t layer) {
		RequireLive(layer);
		if (!m_Entries[layer].Allocated) {
			throw std::runtime_error("motion selected quad has no actual prepared geometry allocation");
		}

		return m_Entries[layer].Geometry;
	}

	const MotionSelectedLayerNode& MotionSelectedQuadGeometry::Node(std::size_t layer) const {
		RequireLive(layer);

		return *m_Entries[layer].Node;
	}

} // namespace sgre::scene
