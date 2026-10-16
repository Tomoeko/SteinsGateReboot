#pragma once

#include "sgre/scene/emote_timeline_transition.hpp"

#include <array>
#include <cstdint>

namespace sgre::scene {
	[[nodiscard]] std::uint32_t PackEmoteColor(const std::array<float, 4>& components) noexcept;

	// Native2cdcc0/2cc510 use a noncyclic four-component EPTransitionControl.
	class EmoteColorTransition {
	public:
		EmoteColorTransition();
		void Set(std::uint32_t color, float duration = 0, float easing = 0);
		// Publication remains the containing plain owner's separate property phase.
		[[nodiscard]] std::uint32_t Advance(float elapsed_ticks);

	private:
		std::array<EmoteTimelineTransition, 4> m_Components;
	};
} // namespace sgre::scene
