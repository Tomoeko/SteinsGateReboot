#pragma once

#include <cstdint>
#include <optional>

namespace sgre::text {

	struct UnicodeCanonicalDecomposition {
		std::uint32_t First = 0;
		// The original primitive uses zero when a successful decomposition contains only First.
		std::uint32_t Second = 0;
	};

	// One canonical step from the game's default Unicode callback; no recursive normalization.
	[[nodiscard]] std::optional<UnicodeCanonicalDecomposition> DecomposeUnicodeCodepoint(std::uint32_t codepoint);

} // namespace sgre::text
