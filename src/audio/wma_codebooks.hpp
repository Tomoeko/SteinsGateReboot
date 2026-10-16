#pragma once

#include "sgre/audio/wma_coefficient.hpp"

#include <cstdint>
#include <span>

namespace sgre::audio::detail {

	struct WmaCoefficientTableEntry {
		WmaHuffmanCode Code;
		std::uint16_t ZeroRun = 0;
		std::uint16_t Magnitude = 0;
	};

	[[nodiscard]] std::span<const WmaCoefficientTableEntry> WmaCoefficientTable(WmaCoefficientBook book);

} // namespace sgre::audio::detail
