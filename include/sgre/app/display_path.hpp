#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace sgre::app {

	[[nodiscard]] std::string DisplayUserPath(const std::filesystem::path& path, const std::filesystem::path& home);
	[[nodiscard]] std::string QuotePosixUserPathArgument(const std::filesystem::path& path, const std::filesystem::path& home);
	[[nodiscard]] std::string RedactUserHome(std::string_view text, const std::filesystem::path& home);

} // namespace sgre::app
