#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

namespace sgre::platform {

	[[nodiscard]] std::string ContentSha256(std::span<const std::uint8_t> bytes);
	// Startup-only identity over the executable and script/scenario archive pairs.
	[[nodiscard]] std::string OriginalCheckpointResourceIdentity(const std::filesystem::path& installation);

} // namespace sgre::platform
