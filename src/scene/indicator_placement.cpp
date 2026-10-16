#include "sgre/scene/indicator_placement.hpp"

#include <cmath>
#include <stdexcept>

namespace sgre::scene {

	static void RequireFiniteIndicatorPoint(LayerPoint point) {
		if (!std::isfinite(point.X) || !std::isfinite(point.Y)) {
			throw std::runtime_error("indicator placement requires finite logical coordinates and measured extents");
		}
	}

	void IndicatorPlacement::UpdateMeasuredSize(LayerPoint size) {
		if (m_Detached) {
			return;
		}

		RequireFiniteIndicatorPoint(size);
		m_Size = size;
	}

	void IndicatorPlacement::RequestSize(LayerPoint size) {
		// The concrete MIndicator size-request vtable entry is original empty function 0x140030f20.
		static_cast<void>(size);
	}

	void IndicatorPlacement::SetCoordinate(LayerPoint coordinate) {
		if (m_Detached) {
			return;
		}

		RequireFiniteIndicatorPoint(coordinate);
		m_Coordinate = coordinate;
	}

	void IndicatorPlacement::SetEdge(IndicatorEdge edge, float value) {
		if (m_Detached) {
			return;
		}

		auto coordinate = m_Coordinate;
		switch (edge) {
		case IndicatorEdge::Left:
			coordinate.X = value;
			break;
		case IndicatorEdge::Top:
			coordinate.Y = value;
			break;
		case IndicatorEdge::Right:
			coordinate.X = value - m_Size.X;
			break;
		case IndicatorEdge::Bottom:
			coordinate.Y = value - m_Size.Y;
			break;
		default:
			throw std::runtime_error("indicator placement received an invalid edge");
		}

		SetCoordinate(coordinate);
	}

	void IndicatorPlacement::SetVisible(bool visible) {
		if (!m_Detached) {
			m_Visible = visible;
		}
	}

	void IndicatorPlacement::Detach() {
		m_Detached = true;
	}

	bool IndicatorPlacement::IsDetached() const noexcept {
		return m_Detached;
	}

	bool IndicatorPlacement::Visible() const noexcept {
		return !m_Detached && m_Visible;
	}

	LayerPoint IndicatorPlacement::Size() const noexcept {
		return m_Detached ? LayerPoint{} : m_Size;
	}

	LayerClip IndicatorPlacement::Bounds() const {
		if (m_Detached) {
			return {0, 0, 0, 0};
		}

		LayerClip bounds;
		bounds.Left = m_Coordinate.X;
		bounds.Top = m_Coordinate.Y;
		bounds.Right = m_Coordinate.X + m_Size.X;
		bounds.Bottom = m_Coordinate.Y + m_Size.Y;
		RequireFiniteIndicatorPoint({bounds.Right, bounds.Bottom});

		return bounds;
	}

} // namespace sgre::scene
