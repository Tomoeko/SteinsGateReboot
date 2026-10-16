#pragma once

#include <cstdint>
#include <span>

namespace sgre::audio {
	// Authored output policy, separate from recovered source/group/master volume arithmetic.
	class PcmOutputLeveler {
	public:
		PcmOutputLeveler(std::uint32_t sample_rate, std::uint32_t channels, float gain, float ceiling = 0.98f, float release_seconds = 0.1f);
		// Serialized consumer only. No allocation or latency; invalid input leaves samples and state unchanged.
		[[nodiscard]] bool Process(std::span<float> interleaved) noexcept;

	private:
		std::uint32_t m_Channels;
		double m_Gain;
		float m_Ceiling;
		double m_Release;
		double m_LimiterGain = 1;
	};
} // namespace sgre::audio
