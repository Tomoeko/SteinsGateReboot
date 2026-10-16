#include "sgre/scene/emote_rotation_transition.hpp"

#include "sgre/scene/emote_easing.hpp"

#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace sgre::scene {
	static constexpr float kEmoteRotationTau = 6.28318548f;
	static constexpr float kEmoteRotationPi = 3.14159274f;
	static constexpr std::size_t kMaximumRotationNormalizationSteps = 65536;

	static float NormalizeEmoteRotation(float radians) {
		if (!std::isfinite(radians)) {
			throw std::runtime_error("Emote rotation requires finite angular controls");
		}
		// Native2d8e90/2d8bf0 add the float period repeatedly; fmod changes their rounding.
		std::size_t steps = 0;
		while (radians < 0) {
			if (steps++ == kMaximumRotationNormalizationSteps) {
				throw std::runtime_error("Emote rotation normalization exceeds bounded angular work");
			}
			radians += kEmoteRotationTau;
		}
		while (radians >= kEmoteRotationTau) {
			if (steps++ == kMaximumRotationNormalizationSteps) {
				throw std::runtime_error("Emote rotation normalization exceeds bounded angular work");
			}
			radians += -kEmoteRotationTau;
		}
		return radians;
	}

	void EmoteRotationTransition::Set(float radians, float duration, float easing) {
		if (!std::isfinite(duration) || !std::isfinite(easing)) {
			throw std::runtime_error("Emote rotation requires finite angular controls");
		}
		const auto target = NormalizeEmoteRotation(radians);
		if (!(duration > 0)) {
			m_Pending.reset();
			m_Current = target;
			m_Active = false;
			return;
		}
		if (!std::isfinite(1.0f / duration)) {
			throw std::runtime_error("Emote rotation duration requires a finite reciprocal");
		}
		if (m_Active && NormalizeEmoteRotation(m_Target) == target) {
			const float remaining = (1.0f - m_Progress) / m_ReciprocalDuration;
			if (duration > remaining) {
				return;
			}
		}
		m_Pending = Request{target, duration, easing};
		m_Active = false;
	}

	float EmoteRotationTransition::AdvanceCurrent(float elapsed) {
		if (!m_Active && m_Pending) {
			const auto request = *m_Pending;
			m_Pending.reset();
			m_Start = m_Current;
			m_Target = request.Target;
			if (m_Target > m_Start) {
				if (m_Target - m_Start > kEmoteRotationPi) {
					m_Target -= kEmoteRotationTau;
				}
			} else if (m_Start - m_Target > kEmoteRotationPi) {
				m_Target += kEmoteRotationTau;
			}
			m_Easing = request.Easing;
			m_Progress = 0;
			m_ReciprocalDuration = 1.0f / request.Duration;
			m_Active = true;
		}
		if (m_Active) {
			const float step = elapsed * m_ReciprocalDuration;
			m_Progress = step + m_Progress;
			if (m_Progress - 1.0f >= -0.0001f) {
				m_Progress = 1;
				m_Current = NormalizeEmoteRotation(m_Target);
				m_Active = false;
			} else {
				const auto ratio = PrepareEmoteEasing(m_Easing, m_Progress);
				const float difference = m_Target - m_Start;
				const float offset = ratio * difference;
				m_Current = NormalizeEmoteRotation(offset + m_Start);
			}
		}
		return m_Current;
	}

	void EmoteRotationTransition::FinishTransitions() {
		// Native2d9060 consumes the last queued target without starting or normalizing that job.
		if (m_Pending) {
			m_Current = m_Pending->Target;
			m_Active = false;
			m_Pending.reset();
		} else if (m_Active) {
			const auto target = NormalizeEmoteRotation(m_Target);
			m_Active = false;
			m_Current = target;
		}
	}

	float EmoteRotationTransition::Advance(float elapsed) {
		if (!std::isfinite(elapsed) || elapsed < 0) {
			throw std::runtime_error("Emote rotation requires finite nonnegative elapsed ticks");
		}
		auto next = *this;
		const auto current = next.AdvanceCurrent(elapsed);
		*this = next;
		return current;
	}

	bool EmoteRotationTransition::Changing() const noexcept {
		return m_Active || m_Pending.has_value();
	}

	float EmoteRotationTransition::Current() const noexcept {
		return m_Current;
	}
} // namespace sgre::scene
