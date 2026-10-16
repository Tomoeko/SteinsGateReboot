#include "sgre/scene/emote_blink_control.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace sgre::scene {

	static constexpr std::uint64_t kEmotePcgMultiplier = 0x5851f42d4c957f2d;

	EmoteBlinkRandom::EmoteBlinkRandom(std::uint32_t entropy_word) noexcept {
		m_Increment = (~static_cast<std::uint64_t>(entropy_word) * 2) | 1;
		m_State = (m_Increment + entropy_word) * kEmotePcgMultiplier + m_Increment;
	}

	std::uint32_t EmoteBlinkRandom::Next() noexcept {
		const auto old = m_State;
		m_State = old * kEmotePcgMultiplier + (m_Increment | 1);
		const auto value = static_cast<std::uint32_t>((old >> 45) ^ (old >> 27));
		return std::rotr(value, static_cast<int>(old >> 59));
	}

	float EmoteBlinkRandom::Unit() noexcept {
		return static_cast<float>(Next() >> 8) * 5.9604644775390625e-8f;
	}

	static void RequireBlinkParameters(const EmoteBlinkParameters& parameters) {
		for (const auto value : {parameters.IntervalMinimum, parameters.IntervalMaximum, parameters.CloseTicks, parameters.HoldTicks, parameters.OpenTicks}) {
			if (!std::isfinite(value) || value < 0) {
				throw std::runtime_error("Emote blink timing requires finite nonnegative actual tick values");
			}
		}
		if (!std::isfinite(parameters.CloseTicks + parameters.HoldTicks + parameters.OpenTicks) || !std::isfinite(parameters.IntervalMinimum + parameters.IntervalMaximum)) {
			throw std::runtime_error("Emote blink timing exceeds its finite actual interval domain");
		}
	}

	EmoteBlinkControl::EmoteBlinkControl(EmoteBlinkRandom& random, EmoteBlinkParameters parameters) : m_Random(random), m_Parameters(parameters) {
		RequireBlinkParameters(parameters);
		m_NextInterval = random.Unit() * (parameters.IntervalMaximum - parameters.IntervalMinimum) + parameters.IntervalMinimum;
	}

	void EmoteBlinkControl::SetSuppressed(bool suppressed) noexcept {
		m_Suppressed = suppressed;
	}

	void EmoteBlinkControl::ResetIdleClock() noexcept {
		if (!m_Active) {
			m_Elapsed = 0;
		}
	}

	void EmoteBlinkControl::SetParameters(EmoteBlinkParameters parameters, bool reset_idle_clock) {
		RequireBlinkParameters(parameters);
		m_Parameters = parameters;
		if (reset_idle_clock) {
			ResetIdleClock();
		}
	}

	void EmoteBlinkControl::Step(float elapsed_ticks) {
		if (!std::isfinite(elapsed_ticks) || elapsed_ticks < 0 || !std::isfinite(m_Elapsed + elapsed_ticks)) {
			throw std::runtime_error("Emote blink update requires finite nonnegative actual elapsed ticks");
		}
		const auto total = m_Parameters.CloseTicks + m_Parameters.HoldTicks + m_Parameters.OpenTicks;
		if (m_Active) {
			m_Elapsed += elapsed_ticks;
			const auto time = std::min(total, m_Elapsed);
			if (time < m_Parameters.CloseTicks) {
				m_Amount = time / m_Parameters.CloseTicks;
			} else if (time > m_Parameters.CloseTicks + m_Parameters.HoldTicks) {
				m_Amount = (total - time) / m_Parameters.OpenTicks;
			} else {
				m_Amount = 1;
			}
			if (m_Elapsed >= m_NextInterval) {
				m_Elapsed = 0;
				m_Active = false;
				m_NextInterval = m_Random.Unit() * (m_Parameters.IntervalMaximum - m_Parameters.IntervalMinimum) + m_Parameters.IntervalMinimum;
			}
			return;
		}
		m_Amount = 0;
		if (!m_Suppressed && m_Parameters.IntervalMinimum + m_Parameters.IntervalMaximum != 0 && total != 0) {
			m_Elapsed += elapsed_ticks;
			if (m_Elapsed >= m_NextInterval) {
				m_Active = true;
				m_Elapsed = 0;
				m_NextInterval = total;
			}
		}
	}

	float EmoteBlinkControl::Amount() const noexcept {
		return m_Amount;
	}

	bool EmoteBlinkControl::Active() const noexcept {
		return m_Active;
	}

	float EmoteBlinkControl::NextInterval() const noexcept {
		return m_NextInterval;
	}

	float EmoteBlinkControl::Elapsed() const noexcept {
		return m_Elapsed;
	}

	float ApplyEmoteBlinkFrame(float frame, float lower, float upper, float amount, std::int32_t parameter_index) {
		if (!std::isfinite(frame) || !std::isfinite(lower) || !std::isfinite(upper) || !std::isfinite(amount)) {
			throw std::runtime_error("Emote blink frame modulation requires finite actual control values");
		}
		if (parameter_index != 0 && ((frame >= lower && frame <= upper) || (frame >= upper && frame <= lower))) {
			return frame + (upper - frame) * amount;
		}
		return frame;
	}

} // namespace sgre::scene
