#include "sgre/scene/emote_color_transition.hpp"

#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace sgre::scene {
	static std::uint32_t EmoteColorWord(float value) noexcept {
		// Native2cc599 converts to signed64, then packs the low32 without masking each byte.
		// The x86 integer-indefinite result has low32 zero, including finite overflow.
		if (!std::isfinite(value) || value < -0x1p63f || value >= 0x1p63f) {
			return 0;
		}
		return static_cast<std::uint32_t>(static_cast<std::int64_t>(value));
	}

	std::uint32_t PackEmoteColor(const std::array<float, 4>& components) noexcept {
		std::array<std::uint32_t, 4> words{};
		for (std::size_t index = 0; index != words.size(); ++index) {
			words[index] = EmoteColorWord(components[index]);
		}
		return (((words[3] << 8) | words[2]) << 8 | words[1]) << 8 | words[0];
	}

	EmoteColorTransition::EmoteColorTransition() {
		Set(0x808080ffu);
	}

	void EmoteColorTransition::Set(std::uint32_t color, float duration, float easing) {
		if (!std::isfinite(duration) || !std::isfinite(easing)) {
			throw std::runtime_error("Emote color transition requires finite duration and easing");
		}
		for (std::size_t index = 0; index != m_Components.size(); ++index) {
			const auto component = (color >> (8 * index)) & 0xffu;
			m_Components[index].Set(static_cast<float>(component), duration, easing);
		}
	}

	std::uint32_t EmoteColorTransition::Advance(float elapsed_ticks) {
		if (!std::isfinite(elapsed_ticks) || elapsed_ticks < 0) {
			throw std::runtime_error("Emote color transition requires finite nonnegative elapsed ticks");
		}
		std::array<float, 4> components{};
		for (std::size_t index = 0; index != components.size(); ++index) {
			components[index] = m_Components[index].Advance(elapsed_ticks);
		}
		return PackEmoteColor(components);
	}
} // namespace sgre::scene
