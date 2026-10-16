#pragma once

#include <cstdint>

namespace sgre::platform {

	// One checked OS random word for native Emote eye-setup's distinct rand_s seed boundary.
	// The containing constructor invokes this once; ordinary updates advance its owned PCG instead.
	[[nodiscard]] std::uint32_t ReadEmoteEntropyWord();

} // namespace sgre::platform
