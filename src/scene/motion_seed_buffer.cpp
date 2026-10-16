#include "sgre/scene/motion_seed_buffer.hpp"

#include <bit>
#include <cstddef>

namespace sgre::scene {
	MotionSeedBuffer::MotionSeedBuffer(std::uint64_t actual_initial_counter) noexcept {
		m_Words.front() = static_cast<std::uint16_t>(actual_initial_counter);
	}

	void MotionSeedBuffer::MixHookInput(std::uint64_t actual_counter, std::span<const std::uint16_t, 3> actual_caller_halfwords) noexcept {
		auto folded = static_cast<std::uint16_t>(actual_counter);
		for (const auto halfword : actual_caller_halfwords) {
			folded ^= halfword;
		}
		folded = std::rotr(folded, static_cast<int>(m_Rotation));

		// Decode the original little-endian halfword selection without aliasing a native word array.
		auto& word = m_Words[m_HalfwordIndex / 2];
		const auto shift = (m_HalfwordIndex & 1u) * 16u;
		word ^= static_cast<std::uint32_t>(folded) << shift;
		if (++m_HalfwordIndex == kMotionSharedSeedWords * 2) {
			m_HalfwordIndex = 0;
			m_Rotation = (m_Rotation + 1) & 15u;
		}
	}

	std::span<const std::uint32_t, kMotionSharedSeedWords> MotionSeedBuffer::Words() const noexcept {
		return m_Words;
	}

	std::uint32_t MotionSeedBuffer::HalfwordIndex() const noexcept {
		return m_HalfwordIndex;
	}

	std::uint32_t MotionSeedBuffer::Rotation() const noexcept {
		return m_Rotation;
	}
} // namespace sgre::scene
