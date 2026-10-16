#include "sgre/scene/motion_blank_source.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace sgre::scene {
	static constexpr std::size_t kMaximumBlankIconBytes = 65536;

	static bool DecimalWhitespace(char value) {
		return value == ' ' || value == '\t' || value == '\n' || value == '\r' || value == '\v' || value == '\f';
	}

	static std::string_view DecimalField(std::string_view value) {
		while (!value.empty() && DecimalWhitespace(value.front())) {
			value.remove_prefix(1);
		}
		while (!value.empty() && DecimalWhitespace(value.back())) {
			value.remove_suffix(1);
		}
		if (!value.empty() && value.front() == '+') {
			value.remove_prefix(1);
			if (!value.empty() && (value.front() == '+' || value.front() == '-')) {
				throw std::runtime_error("motion blank source decimal field has more than one leading sign");
			}
		}
		if (value.empty()) {
			throw std::runtime_error("motion blank source requires four nonempty canonical decimal fields");
		}

		return value;
	}

	static std::int32_t ReadDimension(std::string_view value) {
		value = DecimalField(value);
		std::int32_t result = 0;
		const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result, 10);
		if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
			throw std::runtime_error("motion blank source dimension requires a complete signed int32 decimal field");
		}

		return result;
	}

	static float ReadOrigin(std::string_view value) {
		value = DecimalField(value);
		bool has_digit = false;
		for (const char character : value) {
			if (character >= '0' && character <= '9') {
				has_digit = true;
			} else if (character != '-' && character != '+' && character != '.' && character != 'e' && character != 'E') {
				throw std::runtime_error("motion blank source origin requires its supported decimal syntax");
			}
		}
		double result = 0;
		std::istringstream input{std::string(value)};
		input.imbue(std::locale::classic());
		input >> result;
		if (!has_digit || input.fail() || input.peek() != std::char_traits<char>::eof() || !std::isfinite(result)) {
			throw std::runtime_error("motion blank source origin requires a complete finite double decimal field");
		}
		if (std::abs(result) > static_cast<double>(std::numeric_limits<float>::max())) {
			throw std::runtime_error("motion blank source origin exceeds its bounded float32 conversion domain");
		}
		const auto narrowed = static_cast<float>(result);
		if (!std::isfinite(narrowed)) {
			throw std::runtime_error("motion blank source origin exceeds its native finite float32 extent");
		}

		return narrowed;
	}

	MotionBlankSourceRectangle ReadMotionBlankSourceRectangle(std::string_view icon) {
		icon = icon.substr(0, icon.find('\0'));
		if (icon.empty() || icon.size() > kMaximumBlankIconBytes) {
			throw std::runtime_error("motion blank source icon exceeds its nonempty prepared string bound");
		}
		std::array<std::string_view, 4> fields;
		for (std::size_t index = 0; index < fields.size(); ++index) {
			const auto delimiter = icon.find(':');
			if ((index + 1 < fields.size()) != (delimiter != std::string_view::npos)) {
				throw std::runtime_error("motion blank source requires exactly four colon-separated descriptor fields");
			}
			fields[index] = icon.substr(0, delimiter);
			if (delimiter != std::string_view::npos) {
				icon.remove_prefix(delimiter + 1);
			}
		}

		MotionBlankSourceRectangle rectangle;
		rectangle.Width = ReadDimension(fields[0]);
		rectangle.Height = ReadDimension(fields[1]);
		rectangle.OriginX = ReadOrigin(fields[2]);
		rectangle.OriginY = ReadOrigin(fields[3]);

		return rectangle;
	}
} // namespace sgre::scene
