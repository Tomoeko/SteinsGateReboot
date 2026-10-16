#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace sgre::scene {

	inline constexpr std::size_t kMotionSharedSeedWords = 624;

	class MotionSharedRandom {
	public:
		// Native4f9790 uses this separate explicit scalar initialization for the script Random class.
		explicit MotionSharedRandom(std::uint32_t seed);
		// These are the actual runtime's 624 seed words; no default seed/entropy source is supplied.
		explicit MotionSharedRandom(std::span<const std::uint32_t, kMotionSharedSeedWords> seed);
		MotionSharedRandom(const MotionSharedRandom&) = delete;
		MotionSharedRandom& operator=(const MotionSharedRandom&) = delete;
		// The actual manager serializes access; its lock and lazy-owner installation remain external phases.
		[[nodiscard]] std::uint32_t NextWord() noexcept;
		[[nodiscard]] float NextUnitFloat() noexcept;
		[[nodiscard]] const std::array<std::uint32_t, kMotionSharedSeedWords>& State() const noexcept;

	private:
		std::array<std::uint32_t, kMotionSharedSeedWords> m_State{};
		std::size_t m_Index = 0;
	};

	class MotionPlayerRandom {
	public:
		// Native child construction consumes exactly one actual manager unit sample before initializing this owner.
		explicit MotionPlayerRandom(float manager_sample);
		[[nodiscard]] float NextUnitFloat() noexcept;
		[[nodiscard]] const std::array<std::uint32_t, 4>& State() const noexcept;

	private:
		std::array<std::uint32_t, 4> m_State{};
	};

} // namespace sgre::scene
