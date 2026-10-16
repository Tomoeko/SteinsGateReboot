#include "sgre/scene/emote_scale_transition.hpp"

#include <cmath>
#include <stdexcept>

namespace sgre::scene {
	EmoteScaleTransition::EmoteScaleTransition() {
		m_Component.Set(1);
	}
	void EmoteScaleTransition::Set(float scale, float duration, float easing) {
		// Native2D7350 retains signed and zero targets; no percentage/unit conversion or clamp.
		m_Component.Set(scale, duration, easing);
	}
	void EmoteScaleTransition::FinishTransitions() noexcept {
		m_Component.FinishTransitions();
	}

	bool EmoteScaleTransition::Changing() const noexcept {
		return m_Component.Changing();
	}

	float EmoteScaleTransition::Current() const noexcept {
		return m_Component.Current();
	}
	float EmoteScaleTransition::Advance(float elapsed_ticks) {
		auto next = m_Component;
		const auto scale = next.Advance(elapsed_ticks);
		if (!std::isfinite(scale)) {
			throw std::runtime_error("Emote scale advance requires a finite recovered property result");
		}
		m_Component = next;

		return scale;
	}
} // namespace sgre::scene
