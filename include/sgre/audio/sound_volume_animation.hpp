#pragma once

#include <cstdint>
#include <optional>

namespace sgre::audio {
	struct SoundVolumeAnimationState {
		std::uint64_t Elapsed = 0;
		std::optional<std::uint32_t> Ordinal;
		std::optional<float> EmittedGain;
		bool Complete = false;
	};

	// Native392920/5356f0/5358a0 plus Sound393410/3938b0; endpoints are already interpreted linear gains.
	class SoundVolumeAnimation {
	public:
		SoundVolumeAnimation(float initial_linear, float target_linear, std::int32_t duration, std::int32_t curve);
		// Reads a caller's actual selected clock increment; it never advances the containing clock or audio samples.
		[[nodiscard]] std::optional<float> Advance(float actual_frame_ticks);
		[[nodiscard]] SoundVolumeAnimationState State() const noexcept;

	private:
		float m_Initial = 0;
		float m_Target = 0;
		std::uint32_t m_Duration = 0;
		std::uint32_t m_Count = 1;
		std::int32_t m_Curve = 0;
		SoundVolumeAnimationState m_State;
	};
} // namespace sgre::audio
