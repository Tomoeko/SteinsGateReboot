#pragma once

#include "sgre/scene/motion_random.hpp"

#include <cstddef>
#include <cstdint>

namespace sgre::scene {

	struct MotionParticleEmissionInput {
		bool Active = false;
		bool FrameChanged = false;
		std::int32_t Trigger = 0;
		float FrequencyMin = 0;
		float FrequencyMax = 0;
		float DeltaTicks = 0;
	};

	class MotionParticleEmissionState {
	public:
		static constexpr std::size_t kMaximumAttemptsPerUpdate = 65536;

		// Current-slot Trigger and evaluated frequencies are distinct native owners.
		// Uses the actual containing player's stream before any per-spawn random draws.
		[[nodiscard]] std::size_t Advance(const MotionParticleEmissionInput& input, MotionPlayerRandom& random);
		[[nodiscard]] bool Ready() const noexcept;
		[[nodiscard]] float TimerTicks() const noexcept;

	private:
		[[nodiscard]] std::size_t AdvanceInPlace(const MotionParticleEmissionInput& input, MotionPlayerRandom& random);

		bool m_Ready = false;
		float m_TimerTicks = 0;
	};

} // namespace sgre::scene
