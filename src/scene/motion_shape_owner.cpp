#include "sgre/scene/motion_shape_owner.hpp"

#include "sgre/scene/motion_layer_composition.hpp"
#include "sgre/scene/motion_local_frames.hpp"
#include "sgre/scene/motion_selected_layer_nodes.hpp"
#include "sgre/scene/motion_selected_mesh_vertices.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <variant>

namespace sgre::scene {
	static std::uint32_t ReadShapeType(const resource::MotionLayer& layer) {
		for (const auto& [name, value] : layer.Properties) {
			if (name == "shape") {
				if (const auto* boolean = std::get_if<bool>(&value.Data)) {
					return *boolean ? 1u : 0u;
				}
				if (const auto* integer = std::get_if<std::int64_t>(&value.Data); integer != nullptr && *integer >= std::numeric_limits<std::int32_t>::min() && *integer <= std::numeric_limits<std::uint32_t>::max()) {
					return static_cast<std::uint32_t>(*integer);
				}
				if (const auto* real = std::get_if<double>(&value.Data); real != nullptr && std::isfinite(*real) && *real >= -2147483648.0 && *real < 2147483648.0) {
					return static_cast<std::uint32_t>(static_cast<std::int32_t>(*real));
				}
				throw std::runtime_error("motion shape requires its actual bounded constructor shape word");
			}
		}
		throw std::runtime_error("motion shape constructor requires its actual shape property");
	}

	bool MotionShapeSnapshot::Contains(resource::MotionPoint point) const {
		if (!std::isfinite(point.X) || !std::isfinite(point.Y)) {
			throw std::runtime_error("motion shape contains requires a finite query point");
		}
		if (Type == 1) {
			const float dx = Values[0] - point.X;
			const float dy = Values[1] - point.Y;
			const float distance = dy * dy + dx * dx;
			return Values[2] * Values[2] >= distance;
		}
		if (Type == 2) {
			return !(point.X < Values[0]) && !(Values[2] <= point.X) && !(point.Y < Values[1]) && !(Values[3] <= point.Y);
		}
		if (Type == 3) {
			const float dx = Values[0] - Values[4];
			const float dy = Values[5] - Values[1];
			const float first = dx * Values[3] + dy * Values[2];
			const float second = dx * Values[1] + dy * Values[0];
			const float orientation = -second + first;
			const float sign = orientation >= 0 ? 1.0f : -1.0f;
			for (std::size_t index = 0; index != 4; ++index) {
				const auto next = (index + 1) & 3;
				const float edge_x = Values[index * 2] - Values[next * 2];
				const float edge_y = Values[next * 2 + 1] - Values[index * 2 + 1];
				const float baseline = Values[index * 2 + 1] * edge_x + Values[index * 2] * edge_y;
				const float query = edge_x * point.Y + edge_y * point.X;
				if ((-baseline + query) * sign > 0) {
					return false;
				}
			}
			return true;
		}
		return false;
	}

	MotionLayerShapeOwner::MotionLayerShapeOwner(MotionSelectedLayerNodes& selected, std::uint32_t layer)
	    : m_Selected(selected), m_Layer(layer) {
		if (layer >= selected.m_Nodes.size() || selected.ShapeOwner(layer) != nullptr || selected.m_Nodes[layer].Raw == nullptr || selected.m_Nodes[layer].Prepared == nullptr || selected.m_Nodes[layer].Raw->Type != 1 || selected.m_Nodes[layer].Prepared->Type != 1) {
			throw std::runtime_error("motion type1 shape requires its exact exclusive actual selected node");
		}
		m_Snapshot.Type = ReadShapeType(*selected.m_Nodes[layer].Raw);
	}

	void MotionLayerShapeOwner::RequireOwner() const {
		if (m_Selected.ShapeOwner(m_Layer) != this || m_Layer >= m_Selected.m_Nodes.size() || m_Selected.m_Nodes[m_Layer].Prepared->Type != 1) {
			throw std::runtime_error("motion shape borrow requires its exact live exclusive selected owner");
		}
	}

	void MotionLayerShapeOwner::Compose(const MotionLayerComposition& composition, const MotionLocalFrames& local, const MotionSelectedMeshVertices& vertices) {
		RequireOwner();
		m_Composition = nullptr;
		if (composition.m_SelectedOwner != &m_Selected || &composition.LocalFrames() != &local || m_Selected.m_VertexOwner != &vertices || m_Layer >= composition.Layers().size()) {
			throw std::runtime_error("motion shape composition requires actual current numeric, local and deformation owners");
		}
		// Native5245d0 runs527070 first, then tests geometry Changed(+42), independently of numeric Active(+355).
		const auto position = vertices.DeformedCoordinate(m_Layer);
		const auto& numeric = composition.Layers()[m_Layer];
		if (vertices.GeometryChanged(m_Layer)) {
			auto next = m_Snapshot;
			if (next.Type == 0 || next.Type == 1) {
				next.Values[0] = position.X;
				next.Values[1] = position.Y;
			}
			const auto& attributes = numeric.Hierarchy.Attributes;
			if (next.Type == 1 || next.Type == 2) {
				const float scaled_x = attributes.ZoomX * 16.0f;
				const float half_x = scaled_x * 0.5f;
				if (next.Type == 1) {
					next.Values[2] = half_x;
				} else {
					const float scaled_y = attributes.ZoomY * 16.0f;
					const float half_y = scaled_y * 0.5f;
					next.Values[0] = position.X - half_x;
					next.Values[1] = position.Y - half_y;
					next.Values[2] = position.X + half_x;
					next.Values[3] = position.Y + half_y;
				}
			}
			if (next.Type == 3) {
				const auto current = local.State(m_Layer).CurrentIndex;
				const auto& frame = m_Selected.m_Nodes[m_Layer].Prepared->Frames.at(current).Attributes;
				const auto& matrix = numeric.Hierarchy.Transform;
				const float ox = matrix.XY * frame.OriginY + matrix.XX * frame.OriginX;
				const float oy = matrix.YY * frame.OriginY + matrix.YX * frame.OriginX;
				const float ax = 8.0f * matrix.XX;
				const float bx = 8.0f * matrix.XY;
				const float cy = 8.0f * matrix.YX;
				const float dy = 8.0f * matrix.YY;
				next.Values = {((-ax - bx) - ox) + position.X, ((-cy - dy) - oy) + position.Y, ((ax - bx) - ox) + position.X, ((cy - dy) - oy) + position.Y, ((ax + bx) - ox) + position.X, ((cy + dy) - oy) + position.Y, ((bx - ax) - ox) + position.X, ((dy - cy) - oy) + position.Y};
			}
			for (const auto value : next.Values) {
				if (!std::isfinite(value)) {
					throw std::runtime_error("motion shape composition exceeds its finite supported payload domain");
				}
			}
			m_Snapshot = next;
		}
		m_Composition = &composition;
		m_CompositionSerial = composition.m_CompositionSerial;
	}

	MotionShapeSnapshot MotionLayerShapeOwner::Snapshot() const {
		RequireOwner();
		return m_Snapshot;
	}

	void MotionLayerShapeOwner::RequireCurrentComposition(const MotionLayerComposition& composition) const {
		RequireOwner();
		if (m_Composition != &composition || m_CompositionSerial == 0 || m_CompositionSerial != composition.m_CompositionSerial || composition.m_SelectedOwner != &m_Selected || composition.Layers().empty()) {
			throw std::runtime_error("motion shape finish requires its actual current completed numeric/deformation phase");
		}
	}
} // namespace sgre::scene
