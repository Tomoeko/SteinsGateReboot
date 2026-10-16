#pragma once

#include <cstdint>

namespace sgre::scene {

	class EmoteBlinkRandom {
	public:
		// Native 2c2b30 takes one real rand_s word per eye-setup, independently of Motion's 624-word seed.
		explicit EmoteBlinkRandom(std::uint32_t entropy_word) noexcept;
		[[nodiscard]] std::uint32_t Next() noexcept;
		[[nodiscard]] float Unit() noexcept;

	private:
		std::uint64_t m_State = 0;
		std::uint64_t m_Increment = 1;
	};

	struct EmoteBlinkParameters {
		float IntervalMinimum = 0;
		float IntervalMaximum = 0;
		float CloseTicks = 0;
		float HoldTicks = 0;
		float OpenTicks = 0;
	};

	class EmoteBlinkControl {
	public:
		// The actual containing Emote eye owner must outlive this borrowed generator.
		// Parameter selection/range words require their genuine constructor producer, not metadata guesses.
		EmoteBlinkControl(EmoteBlinkRandom& random, EmoteBlinkParameters parameters);
		// Copies native eye270..27d state while retaining destination parameters and random borrow.
		void AssignState(const EmoteBlinkControl& source) noexcept;
		void SetSuppressed(bool suppressed) noexcept;
		void ResetIdleClock() noexcept;
		void SetParameters(EmoteBlinkParameters parameters, bool reset_idle_clock);
		void Step(float elapsed_ticks);
		[[nodiscard]] float Amount() const noexcept;
		[[nodiscard]] bool Active() const noexcept;
		[[nodiscard]] float NextInterval() const noexcept;
		[[nodiscard]] float Elapsed() const noexcept;

	private:
		EmoteBlinkRandom& m_Random;
		EmoteBlinkParameters m_Parameters;
		float m_Amount = 0;
		float m_NextInterval = 0;
		float m_Elapsed = 0;
		bool m_Active = false;
		bool m_Suppressed = false;
	};

	[[nodiscard]] float ApplyEmoteBlinkFrame(float frame, float lower, float upper, float amount, std::int32_t parameter_index);

} // namespace sgre::scene
