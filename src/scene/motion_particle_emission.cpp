#include "sgre/scene/motion_particle_emission.hpp"

#include <cmath>
#include <stdexcept>

namespace sgre::scene {

	static float SampleParticleRange(float first, float second, MotionPlayerRandom& random) {
		if (first == second) {
			return first;
		}

		const auto difference = second - first;
		const auto scaled = random.NextUnitFloat() * difference;

		return scaled + first;
	}

	static float ParticlePeriod(const MotionParticleEmissionInput& input, MotionPlayerRandom& random) {
		const auto first = 60.0f / input.FrequencyMin;
		const auto second = 60.0f / input.FrequencyMax;
		const auto period = SampleParticleRange(first, second, random);
		if (!std::isfinite(period) || period <= 0) {
			throw std::runtime_error("motion particle automatic period requires finite positive reciprocal frequencies");
		}

		return period;
	}

	std::size_t MotionParticleEmissionState::Advance(const MotionParticleEmissionInput& input, MotionPlayerRandom& random) {
		if (!std::isfinite(input.DeltaTicks) || !std::isfinite(input.FrequencyMin) || !std::isfinite(input.FrequencyMax)) {
			throw std::runtime_error("motion particle emission requires finite actual clock and evaluated frequencies");
		}

		// Rejecting an unbounded receiving domain must not partly consume the player's stream.
		auto pending = *this;
		auto pending_random = random;
		const auto attempts = pending.AdvanceInPlace(input, pending_random);
		*this = pending;
		random = pending_random;

		return attempts;
	}

	std::size_t MotionParticleEmissionState::AdvanceInPlace(const MotionParticleEmissionInput& input, MotionPlayerRandom& random) {
		if (!input.Active) {
			m_Ready = false;

			return 0;
		}

		if (input.Trigger == 0 && input.FrequencyMin == 0) {
			return 0;
		}

		if (input.Trigger == 0 && (input.FrequencyMin <= 0 || input.FrequencyMax <= 0)) {
			throw std::runtime_error("motion particle automatic emission requires positive evaluated frequencies");
		}

		if (!m_Ready) {
			m_Ready = true;
			if (input.Trigger == 0) {
				const auto initial = ParticlePeriod(input, random) + m_TimerTicks;
				m_TimerTicks = initial > 0 ? initial : 0.0f;
			}
		}

		std::size_t attempts = 0;
		if (input.Trigger == 0) {
			m_TimerTicks -= input.DeltaTicks;
			if (!std::isfinite(m_TimerTicks)) {
				throw std::runtime_error("motion particle emission timer exceeds its finite receiving domain");
			}

			while (m_TimerTicks <= 0) {
				if (attempts == kMaximumAttemptsPerUpdate) {
					throw std::runtime_error("motion particle emission attempts exceed their bounded update budget");
				}

				++attempts;
				m_TimerTicks = ParticlePeriod(input, random) + m_TimerTicks;
				if (!std::isfinite(m_TimerTicks)) {
					throw std::runtime_error("motion particle emission timer exceeds its finite receiving domain");
				}
			}

			const auto maximum = 60.0f / input.FrequencyMin;
			m_TimerTicks = maximum < m_TimerTicks ? maximum : m_TimerTicks;
		} else if (input.Trigger == 1 && input.FrameChanged) {
			const auto sampled = SampleParticleRange(input.FrequencyMin, input.FrequencyMax, random);
			const auto truncated = std::trunc(sampled);
			if (!std::isfinite(truncated) || truncated < -2147483648.0f || static_cast<double>(truncated) > 2147483647.0) {
				throw std::runtime_error("motion particle burst count exceeds its signed native conversion domain");
			}

			if (truncated > static_cast<float>(kMaximumAttemptsPerUpdate)) {
				throw std::runtime_error("motion particle emission attempts exceed their bounded update budget");
			}

			if (truncated > 0) {
				attempts = static_cast<std::size_t>(truncated);
			}
		}

		return attempts;
	}

	bool MotionParticleEmissionState::Ready() const noexcept {
		return m_Ready;
	}

	float MotionParticleEmissionState::TimerTicks() const noexcept {
		return m_TimerTicks;
	}

} // namespace sgre::scene
