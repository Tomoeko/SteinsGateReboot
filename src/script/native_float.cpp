#include "sgre/script/native_float.hpp"

#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace sgre::script {
	static void PreserveGeneralZeros(std::string& text, std::size_t precision) {
		const auto exponent = text.find('e');
		const auto mantissa_end = exponent == std::string::npos ? text.size() : exponent;
		bool significant = false;
		std::size_t digits = 0;
		for (std::size_t index = 0; index < mantissa_end; ++index) {
			const auto character = text[index];
			if (character >= '0' && character <= '9') {
				if (character != '0' || significant) {
					significant = true;
					++digits;
				}
			}
		}

		if (!significant) {
			digits = 1;
		}

		std::size_t insertion = mantissa_end;
		if (text.find('.') == std::string::npos) {
			text.insert(insertion++, 1, '.');
		}

		if (digits < precision) {
			text.insert(insertion, precision - digits, '0');
		}
	}

	std::string FormatNativeFloat(float number, char conversion, std::optional<std::size_t> precision, bool alternate) {
		if (std::string_view("eEfgG").find(conversion) == std::string_view::npos || (precision && *precision > 99)) {
			throw std::runtime_error("invalid native floating format");
		}

		std::string text;
		const auto bits = std::bit_cast<std::uint32_t>(number);
		if (std::isnan(number)) {
			text = bits == 0xffc00000 ? "-nan(ind)" : ((bits & 0x80000000) != 0 ? "-nan" : "nan");
		} else if (std::isinf(number)) {
			text = (bits & 0x80000000) != 0 ? "-inf" : "inf";
		} else {
			const bool general = conversion == 'g' || conversion == 'G';
			const auto digits = general && precision && *precision == 0 ? 1 : precision.value_or(6);
			const auto mode = general ? std::chars_format::general : (conversion == 'f' ? std::chars_format::fixed : std::chars_format::scientific);
			std::array<char, 1024> buffer{};
			const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), static_cast<double>(number), mode, static_cast<int>(digits));
			if (result.ec != std::errc{}) {
				throw std::runtime_error("native floating format conversion failed");
			}

			text.assign(buffer.data(), result.ptr);
			if (alternate && general) {
				PreserveGeneralZeros(text, digits);
			} else if (alternate && text.find('.') == std::string::npos) {
				const auto exponent = text.find('e');
				text.insert(exponent == std::string::npos ? text.size() : exponent, 1, '.');
			}
		}

		if (conversion == 'E' || conversion == 'G') {
			for (auto& character : text) {
				if (character >= 'a' && character <= 'z') {
					character = static_cast<char>(character - 'a' + 'A');
				}
			}
		}

		return text;
	}
} // namespace sgre::script
