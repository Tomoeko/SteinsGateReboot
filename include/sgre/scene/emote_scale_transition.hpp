#pragma once

#include "sgre/scene/emote_timeline_transition.hpp"

namespace sgre::scene {
	// Native2CF570 owns one noncyclic component, initialized to1 before metadata selection.
	class EmoteScaleTransition {
	public:
		EmoteScaleTransition();
		void Set(float scale, float duration = 0, float easing = 0);
		[[nodiscard]] bool Changing() const noexcept;
		[[nodiscard]] float Current() const noexcept;
		[[nodiscard]] float Advance(float elapsed_ticks);

	private:
		EmoteTimelineTransition m_Component;
	};
} // namespace sgre::scene
