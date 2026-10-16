#include "sgre/scene/motion_random.hpp"

#include <bit>
#include <cmath>
#include <stdexcept>

namespace sgre::scene {

	static float RandomUnitFloat(std::uint32_t word) noexcept {
		return std::bit_cast<float>((word & 0x007fffffu) | 0x3f800000u) - 1.0f;
	}

	MotionSharedRandom::MotionSharedRandom(std::uint32_t seed) {
		m_State[0] = seed;
		for (std::size_t index = 1; index < m_State.size(); ++index) {
			const auto previous = m_State[index - 1];
			m_State[index] = (previous ^ (previous >> 30)) * 0x6c078965u + static_cast<std::uint32_t>(index);
		}

	}

	MotionSharedRandom::MotionSharedRandom(std::span<const std::uint32_t, kMotionSharedSeedWords> seed)
	    : MotionSharedRandom(0x012bd6aau) {
		std::size_t index = 1;
		for (std::size_t seed_index = 0; seed_index < seed.size(); ++seed_index) {
			const auto previous = m_State[index - 1];
			m_State[index] = (m_State[index] ^ ((previous ^ (previous >> 30)) * 0x0019660du)) + seed[seed_index] + static_cast<std::uint32_t>(seed_index);
			if (++index == m_State.size()) {
				m_State[0] = m_State.back();
				index = 1;
			}
		}

		for (std::size_t count = 0; count < m_State.size() - 1; ++count) {
			const auto previous = m_State[index - 1];
			m_State[index] = (m_State[index] ^ ((previous ^ (previous >> 30)) * 0x5d588b65u)) - static_cast<std::uint32_t>(index);
			if (++index == m_State.size()) {
				m_State[0] = m_State.back();
				index = 1;
			}
		}

		m_State[0] = 0x80000000u;
	}

	std::uint32_t MotionSharedRandom::NextWord() noexcept {
		const auto following = m_Index + 1 == m_State.size() ? 0 : m_Index + 1;
		const auto distant = m_Index < 227 ? m_Index + 397 : m_Index - 227;
		const auto combined = (m_State[m_Index] & 0x80000000u) | (m_State[following] & 0x7fffffffu);
		m_State[m_Index] = m_State[distant] ^ (combined >> 1) ^ ((combined & 1u) != 0 ? 0x9908b0dfu : 0u);
		auto output = m_State[m_Index];
		if (++m_Index == m_State.size()) {
			m_Index = 0;
		}

		output ^= output >> 11;
		output ^= (output & 0xff3a58adu) << 7;
		output ^= (output & 0xffffdf8cu) << 15;
		output ^= output >> 18;

		return output;
	}

	float MotionSharedRandom::NextUnitFloat() noexcept {
		return RandomUnitFloat(NextWord());
	}

	const std::array<std::uint32_t, kMotionSharedSeedWords>& MotionSharedRandom::State() const noexcept {
		return m_State;
	}

	MotionPlayerRandom::MotionPlayerRandom(float manager_sample) {
		const auto units = manager_sample * 8388608.0f;
		if (!std::isfinite(manager_sample) || manager_sample < 0 || manager_sample >= 1 || std::signbit(manager_sample) || std::trunc(units) != units) {
			throw std::runtime_error("motion player random requires its actual manager's finite 23-bit unit sample");
		}

		const auto fourth = manager_sample * 4294967296.0f;
		m_State = {123456789u, 362436069u, 521288629u, static_cast<std::uint32_t>(fourth)};
	}

	float MotionPlayerRandom::NextUnitFloat() noexcept {
		const auto shifted = m_State[0] ^ (m_State[0] << 11);
		m_State[0] = m_State[1];
		m_State[1] = m_State[2];
		m_State[2] = m_State[3];
		m_State[3] = m_State[3] ^ (m_State[3] >> 19) ^ shifted ^ (shifted >> 8);

		return RandomUnitFloat(m_State[3]);
	}

	const std::array<std::uint32_t, 4>& MotionPlayerRandom::State() const noexcept {
		return m_State;
	}

} // namespace sgre::scene
