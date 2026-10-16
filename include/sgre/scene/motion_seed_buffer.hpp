#pragma once

#include "sgre/scene/motion_random.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace sgre::scene {
	class MotionSeedBuffer {
	public:
		// Original construction clears all 2,496 bytes, then writes only the counter's low halfword.
		explicit MotionSeedBuffer(std::uint64_t actual_initial_counter) noexcept;
		MotionSeedBuffer(const MotionSeedBuffer&) = delete;
		MotionSeedBuffer& operator=(const MotionSeedBuffer&) = delete;
		MotionSeedBuffer(MotionSeedBuffer&&) = delete;
		MotionSeedBuffer& operator=(MotionSeedBuffer&&) = delete;
		// Native caller-stack inputs remain explicit. This component supplies no substitute platform entropy.
		// The actual containing frame owner serializes mixing and seed snapshots; it registers the hook separately.
		void MixHookInput(std::uint64_t actual_counter, std::span<const std::uint16_t, 3> actual_caller_halfwords) noexcept;
		// Borrowed words remain attached to this stable owner. MotionSharedRandom copies them when constructed.
		[[nodiscard]] std::span<const std::uint32_t, kMotionSharedSeedWords> Words() const noexcept;
		[[nodiscard]] std::uint32_t HalfwordIndex() const noexcept;
		[[nodiscard]] std::uint32_t Rotation() const noexcept;

	private:
		std::array<std::uint32_t, kMotionSharedSeedWords> m_Words{};
		std::uint32_t m_HalfwordIndex = 0;
		std::uint32_t m_Rotation = 0;
	};
} // namespace sgre::scene
