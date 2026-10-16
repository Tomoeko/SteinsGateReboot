#pragma once

#include <cstdint>

namespace sgre::text {

	enum class UnicodeCompositionStatus : std::uint8_t {
		UnmappedInput = 0,
		MissingPair = 1,
		Composed = 2
	};

	struct UnicodeCompositionResult {
		// UnmappedInput preserves the native caller output; MissingPair writes zero; Composed writes Codepoint.
		UnicodeCompositionStatus Status = UnicodeCompositionStatus::UnmappedInput;
		std::uint32_t Codepoint = 0;
	};

	// Ordered pair query from the game's default Unicode callback, including its original Hangul gates.
	[[nodiscard]] UnicodeCompositionResult ComposeUnicodeCodepoints(std::uint32_t first, std::uint32_t second);

} // namespace sgre::text
