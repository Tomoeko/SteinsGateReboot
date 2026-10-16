#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace sgre::resource {

	// RFC 8878 frames without external dictionaries. The output must match expected_size exactly.
	[[nodiscard]] std::vector<std::uint8_t> DecodeZstd(std::span<const std::uint8_t> bytes, std::size_t expected_size, std::size_t max_decoded_size);

} // namespace sgre::resource
