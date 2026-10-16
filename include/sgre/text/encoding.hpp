#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace sgre::text {

	// Layout preserves the original UTF-16 code-unit boundary behind this UTF-8 interface.
	[[nodiscard]] std::vector<std::uint16_t> DecodeTextCodeUnits(std::string_view utf8);

} // namespace sgre::text
