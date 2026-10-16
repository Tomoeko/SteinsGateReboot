#pragma once

#include <cstdint>

namespace sgre::text {

	// Matches the native callback's low-16-bit lookup; an unmapped input retains its full 32 bits.
	[[nodiscard]] std::uint32_t MirrorUnicodeCodepoint(std::uint32_t codepoint);

} // namespace sgre::text
