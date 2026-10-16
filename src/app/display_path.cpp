#include "sgre/app/display_path.hpp"

#include <algorithm>

namespace sgre::app {

	static std::string PathUtf8(const std::filesystem::path& path) {
		const auto encoded = path.generic_u8string();

		return std::string(encoded.begin(), encoded.end());
	}

	static std::string QuoteArgument(std::string_view value) {
		if (!value.empty() && std::all_of(value.begin(), value.end(), [](char character) {
			    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9') || character == '/' || character == '.' || character == '_' || character == '-';
		    })) {
			return std::string(value);
		}

		std::string result = "'";
		for (const char character : value) {
			if (character == '\'') {
				result += "'\\''";
			} else {
				result += character;
			}
		}
		result += '\'';

		return result;
	}

	std::string DisplayUserPath(const std::filesystem::path& path, const std::filesystem::path& home) {
		if (!path.is_absolute() || !home.is_absolute() || home == home.root_path()) {
			return PathUtf8(path);
		}
		const auto relative = path.lexically_relative(home);
		if (relative.empty() || *relative.begin() == "..") {
			return PathUtf8(path);
		}
		if (relative == ".") {
			return "~";
		}

		return "~/" + PathUtf8(relative);
	}

	std::string QuotePosixUserPathArgument(const std::filesystem::path& path, const std::filesystem::path& home) {
		const auto display = DisplayUserPath(path, home);
		if (display != PathUtf8(path)) {
			if (display == "~") {
				return display;
			}
			// The shell expands the unquoted home prefix; the remaining path is one quoted argument.
			if (display.starts_with("~/")) {
				return "~/" + QuoteArgument(std::string_view(display).substr(2));
			}
		}

		return QuoteArgument(display);
	}

	std::string RedactUserHome(std::string_view text, const std::filesystem::path& home) {
		std::string result(text);
		if (!home.is_absolute() || home == home.root_path()) {
			return result;
		}
		const auto encoded = PathUtf8(home);
		std::size_t position = 0;
		while ((position = result.find(encoded, position)) != std::string::npos) {
			result.replace(position, encoded.size(), "~");
			++position;
		}

		return result;
	}

} // namespace sgre::app
