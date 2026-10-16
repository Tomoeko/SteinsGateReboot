#include "sgre/audio/pcm_output_leveler.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace sgre::audio {
	PcmOutputLeveler::PcmOutputLeveler(std::uint32_t sample_rate, std::uint32_t channels, float gain, float ceiling, float release_seconds) : m_Channels(channels), m_Gain(gain), m_Ceiling(ceiling), m_Release(0) {
		if (sample_rate < 8000 || sample_rate > 192000 || channels == 0 || channels > 8 || !std::isfinite(gain) || gain <= 0 || gain > 16 || !std::isfinite(ceiling) || ceiling <= 0 || ceiling > 1 || !std::isfinite(release_seconds) || release_seconds < 0.001f || release_seconds > 10) {
			throw std::runtime_error("PCM output leveler requires bounded rate, channels, gain, ceiling and release time");
		}
		m_Release = std::exp(-1.0 / (static_cast<double>(sample_rate) * release_seconds));
	}

	bool PcmOutputLeveler::Process(std::span<float> interleaved) noexcept {
		if (interleaved.size() % m_Channels != 0 || std::any_of(interleaved.begin(), interleaved.end(), [](float sample) {
			    return !std::isfinite(sample);
		    })) {
			return false;
		}
		for (std::size_t frame = 0; frame != interleaved.size(); frame += m_Channels) {
			double peak = 0;
			for (std::size_t channel = 0; channel != m_Channels; ++channel) {
				peak = std::max(peak, std::abs(static_cast<double>(interleaved[frame + channel])));
			}
			const auto required = peak == 0 ? 1.0 : std::min(1.0, static_cast<double>(m_Ceiling) / (peak * m_Gain));
			// Link all channels to preserve their balance, with immediate protection and a smooth release.
			m_LimiterGain = required < m_LimiterGain ? required : required + (m_LimiterGain - required) * m_Release;
			const auto gain = m_Gain * m_LimiterGain;
			for (std::size_t channel = 0; channel != m_Channels; ++channel) {
				interleaved[frame + channel] = std::clamp(static_cast<float>(interleaved[frame + channel] * gain), -m_Ceiling, m_Ceiling);
			}
		}
		return true;
	}
} // namespace sgre::audio
