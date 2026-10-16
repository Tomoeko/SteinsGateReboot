#pragma once

#include "sgre/scene/emote_timeline_transition.hpp"

#include <array>
#include <cstdint>

namespace sgre::scene {
	enum class EmoteCoordinateMode : std::uint8_t {
		Pixels = 0,
		Meters = 1
	};

	class EmoteCoordinateTransition {
	public:
		// Native2CDF50/2BEC20 scale the dim2 transition only in coordinate mode1.
		explicit EmoteCoordinateTransition(EmoteCoordinateMode coordinate_mode = EmoteCoordinateMode::Pixels, float units_per_meter = 1);
		void Set(const std::array<float, 2>& coordinate, float duration = 0, float easing = 0);
		// Native export/import copies pixel-valued controls while retaining the destination coordinate policy.
		void AssignState(const EmoteCoordinateTransition& source) noexcept;
		void FinishTransitions() noexcept;
		[[nodiscard]] bool Changing() const noexcept;
		[[nodiscard]] std::array<float, 2> Current() const;
		// The property phase consumes pixel units; script getters consume Current instead.
		[[nodiscard]] std::array<float, 2> Advance(float elapsed_ticks);

	private:
		std::array<EmoteTimelineTransition, 2> m_Components;
		EmoteCoordinateMode m_Mode;
		float m_UnitsPerMeter;
	};

	// Default native origin/pivot, scale1 and rotation0. Other property configurations remain gated.
	[[nodiscard]] std::array<float, 2> PrepareEmoteDefaultCoordinatePublication(const std::array<float, 2>& coordinate);
} // namespace sgre::scene
