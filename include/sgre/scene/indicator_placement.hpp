#pragma once

#include "sgre/scene/layers.hpp"

#include <cstdint>

namespace sgre::scene {

	enum class IndicatorEdge : std::uint8_t {
		Left,
		Top,
		Right,
		Bottom
	};

	class IndicatorPlacement {
	public:
		// Intrinsic size comes from native text measurement; ordinary LayerView resize requests are no-ops.
		void UpdateMeasuredSize(LayerPoint size);
		void RequestSize(LayerPoint size);
		void SetCoordinate(LayerPoint coordinate);
		void SetEdge(IndicatorEdge edge, float value);
		void SetVisible(bool visible);
		// The owning Layer borrows its view registry. Retained views become inert after Layer disposal.
		void Detach();
		[[nodiscard]] bool IsDetached() const noexcept;
		[[nodiscard]] bool Visible() const noexcept;
		[[nodiscard]] LayerPoint Size() const noexcept;
		[[nodiscard]] LayerClip Bounds() const;

	private:
		LayerPoint m_Coordinate;
		LayerPoint m_Size;
		bool m_Visible = false;
		bool m_Detached = false;
	};

} // namespace sgre::scene
