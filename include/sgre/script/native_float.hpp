#pragma once

#include <cstddef>
#include <optional>
#include <string>

namespace sgre::script {
	// Matches the recovered CRT's promoted float32 formatting; callers handle width and optional sign prefixes.
	[[nodiscard]] std::string FormatNativeFloat(float number, char conversion = 'g', std::optional<std::size_t> precision = {}, bool alternate = false);
} // namespace sgre::script
