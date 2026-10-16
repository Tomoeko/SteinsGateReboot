#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace sgre::resource {
	struct MzsPayload {
		std::uint32_t DecodedSize = 0;
		std::vector<std::uint8_t> CompressedBytes;
	};

	// Removes the MZS XOR wrapper; CompressedBytes remains a Zstandard frame.
	[[nodiscard]] MzsPayload DecodeMzsShell(std::span<const std::uint8_t> bytes, std::string_view filename, std::string_view seed, std::size_t period, std::size_t max_decoded_size);
} // namespace sgre::resource
