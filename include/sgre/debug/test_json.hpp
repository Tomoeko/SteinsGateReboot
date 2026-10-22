#pragma once

#include <ostream>
#include <string_view>

namespace sgre::debug {

	// Test artifacts preserve UTF-8 and escape every JSON control byte, including embedded NUL.
	inline void WriteTestJsonString(std::ostream& output, std::string_view text) {
		constexpr std::string_view digits = "0123456789abcdef";
		output.put('"');
		for (const auto character : text) {
			const auto byte = static_cast<unsigned char>(character);
			if (character == '"' || character == '\\') {
				output.put('\\');
				output.put(character);
			} else if (byte < 32) {
				output << "\\u00" << digits[byte >> 4] << digits[byte & 15];
			} else {
				output.put(character);
			}
		}
		output.put('"');
	}

} // namespace sgre::debug
