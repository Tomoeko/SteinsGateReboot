#pragma once

#include "sgre/resource/psb.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace sgre::resource {

	// Both owners enclose later borrowed bitmap-font values and encoded image ranges.
	struct ResidentMeterFontInput {
		std::shared_ptr<const std::vector<std::uint8_t>> Backing;
		std::shared_ptr<const PsbDocument> Document;
		std::uint64_t FileOffset = 0;
	};

	// Reads the original PE-backed meter font; no extracted font is published.
	[[nodiscard]] ResidentMeterFontInput ReadResidentMeterFont(const std::filesystem::path& executable);

} // namespace sgre::resource
