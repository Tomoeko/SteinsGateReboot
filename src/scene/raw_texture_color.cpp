#include "sgre/scene/raw_texture_color.hpp"

#include <algorithm>
#include <cmath>

namespace sgre::scene {

	static std::uint32_t QuantizeRawComponent(float component) {
		const float scaled = component * 255.0f;
		std::uint32_t low_word = 0;
		if (std::isfinite(scaled) && scaled >= -0x1p63f && scaled < 0x1p63f) {
			low_word = static_cast<std::uint32_t>(static_cast<std::int64_t>(scaled));
		}

		// Original CVTTSS2SI64 followed by unsigned CMOVA compares only the low DWORD.
		return std::min(low_word, std::uint32_t{255});
	}

	std::uint32_t PackRawTextureColor(const std::array<float, 3>& rgb) {
		const auto red = QuantizeRawComponent(rgb[0]);
		const auto green = QuantizeRawComponent(rgb[1]);
		const auto blue = QuantizeRawComponent(rgb[2]);

		return ((red << 8 | green) << 8 | blue) << 8;
	}

	std::uint32_t PrepareRawTextureDrawColor(std::uint32_t packed_rgb, std::uint32_t opacity) {
		const auto rgba = packed_rgb | (opacity & 255);

		return ((rgba & 255) << 24) | (rgba >> 8);
	}

} // namespace sgre::scene
