#include "sgre/scene/emote_timeline_transition.hpp"

#include "sgre/scene/emote_easing.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace sgre::scene {
	void EmoteTimelineTransition::Set(float target, float duration, float easing) {
		if (!std::isfinite(target) || !std::isfinite(duration) || !std::isfinite(easing)) {
			throw std::runtime_error("Emote timeline transition requires finite scalar controls");
		}
		if (!(duration > 0)) {
			m_Current = target;
			m_Changing = false;
			return;
		}
		m_Start = m_Current;
		m_Target = target;
		m_Duration = duration;
		m_Easing = easing;
		m_Elapsed = 0;
		m_Changing = true;
	}
	float EmoteTimelineTransition::Advance(float elapsed) {
		if (!std::isfinite(elapsed) || elapsed < 0) {
			throw std::runtime_error("Emote timeline transition requires finite nonnegative elapsed ticks");
		}
		if (m_Changing) {
			m_Elapsed += elapsed;
			auto ratio = std::min(m_Elapsed / m_Duration, 1.0f);
			if (!(ratio < 1)) {
				m_Changing = false;
			}
			ratio = PrepareEmoteEasing(m_Easing, ratio);
			const float before = (1.0f - ratio) * m_Start;
			const float after = ratio * m_Target;
			m_Current = before + after;
		}
		return m_Current;
	}
	float EmoteTimelineTransition::Current() const noexcept {
		return m_Current;
	}
	bool EmoteTimelineTransition::Changing() const noexcept {
		return m_Changing;
	}
} // namespace sgre::scene
