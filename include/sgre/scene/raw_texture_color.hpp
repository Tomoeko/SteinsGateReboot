#pragma once

#include <array>
#include <cstdint>

namespace sgre::scene {

	// The native raw view stores RRGGBB00; opacity is a separate full DWORD and draws through its low byte.
	[[nodiscard]] std::uint32_t PackRawTextureColor(const std::array<float, 3>& rgb);
	[[nodiscard]] std::uint32_t PrepareRawTextureDrawColor(std::uint32_t packed_rgb, std::uint32_t opacity);

} // namespace sgre::scene
