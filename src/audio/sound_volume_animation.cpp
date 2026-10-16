#include "sgre/audio/sound_volume_animation.hpp"

#include "sgre/audio/sound_volume.hpp"

#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>

namespace sgre::audio {
	static std::uint32_t SoundCurvePower(std::uint32_t value, std::uint32_t factor, std::uint32_t denominator, std::uint32_t iterations) {
		if (denominator == 0) {
			throw std::runtime_error("Sound volume curve has no actual integer interval");
		}
		for (std::uint32_t iteration = 0; iteration < iterations; ++iteration) {
			// Native5358a0 keeps the low32 IMUL product before zero-extension and signed64 IDIV.
			const std::uint32_t product = value * factor;
			value = static_cast<std::uint32_t>(static_cast<std::uint64_t>(product) / denominator);
		}

		return value;
	}

	static std::uint32_t SoundCurveOrdinal(std::uint32_t ordinal, std::uint32_t count, std::int32_t curve) {
		if (curve == 0) {
			return ordinal;
		}
		const auto last = count - 1;
		if (curve >= -10 && curve <= 10) {
			if (curve > 0) {
				return SoundCurvePower(ordinal, ordinal, last, static_cast<std::uint32_t>(curve));
			}
			const auto complement = last - ordinal;

			return last - SoundCurvePower(complement, complement, last, static_cast<std::uint32_t>(-curve));
		}
		const auto half = count >> 1;
		if (curve > 10) {
			const auto power = static_cast<std::uint32_t>(curve - 10);
			if (ordinal < half) {
				return SoundCurvePower(ordinal, ordinal, half - 1, power);
			}
			const auto complement = last - ordinal;

			return last - SoundCurvePower(complement, complement, count - half - 1, power);
		}
		const auto power = static_cast<std::uint32_t>(-10 - curve);
		if (ordinal < half) {
			const auto complement = half - 1 - ordinal;

			return half - 1 - SoundCurvePower(complement, complement, half - 1, power);
		}
		const auto relative = ordinal - half;

		return half + SoundCurvePower(relative, relative, count - half - 1, power);
	}

	SoundVolumeAnimation::SoundVolumeAnimation(float initial_linear, float target_linear, std::int32_t duration, std::int32_t curve) : m_Initial(initial_linear), m_Target(target_linear), m_Duration(static_cast<std::uint32_t>(duration)), m_Count(duration == 0 ? 1u : static_cast<std::uint32_t>(duration) * 16u), m_Curve(curve) {
		if (!std::isfinite(initial_linear) || !std::isfinite(target_linear) || duration < 0 || duration > std::numeric_limits<std::int32_t>::max() / 16 || curve < -64 || curve > 64) {
			throw std::runtime_error("Sound volume animation requires finite actual linear endpoints and bounded native duration/curve");
		}
	}

	std::optional<float> SoundVolumeAnimation::Advance(float actual_frame_ticks) {
		if (m_State.Complete) {
			return {};
		}
		if (m_Duration == 0) {
			m_State.EmittedGain = m_Target;
			m_State.Complete = true;

			return m_Target;
		}
		if (!std::isfinite(actual_frame_ticks) || actual_frame_ticks < 1 || actual_frame_ticks > 4294967296.0f) {
			throw std::runtime_error("Sound volume animation requires its bounded actual shared frame delta");
		}
		m_State.Elapsed += static_cast<std::uint64_t>(actual_frame_ticks);
		if (m_State.Elapsed >= m_Duration) {
			m_State.Elapsed = m_Duration;
			m_State.Complete = true;
		}
		const auto base = static_cast<std::uint32_t>((static_cast<std::uint64_t>(m_Count - 1) * m_State.Elapsed) / m_Duration);
		const auto ordinal = SoundCurveOrdinal(base, m_Count, m_Curve);
		if (m_State.Ordinal && *m_State.Ordinal == ordinal) {
			return {};
		}
		m_State.Ordinal = ordinal;
		m_State.EmittedGain = m_Target;
		if (ordinal != m_Count - 1) {
			const auto ratio = static_cast<float>(ordinal) / static_cast<float>(m_Count - 1);
			// Native393410/3938b0 always interpolate signed loudness coordinates, independently of the setting flag.
			const auto initial = SoundVolumeFromLinear(m_Initial);
			const auto target = SoundVolumeFromLinear(m_Target);
			const auto initial_part = initial * (1.0f - ratio);
			const auto target_part = target * ratio;
			m_State.EmittedGain = SoundVolumeToLinear(initial_part + target_part, SoundVolumeUnit::Loudness);
		}

		return m_State.EmittedGain;
	}

	SoundVolumeAnimationState SoundVolumeAnimation::State() const noexcept {
		return m_State;
	}

} // namespace sgre::audio
