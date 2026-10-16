#include "sgre/text/encoding.hpp"

#include <cstddef>
#include <stdexcept>
#include <string>

namespace sgre::text {

	static constexpr std::size_t kMaximumTextBytes = 1024 * 1024;

	static std::runtime_error TextEncodingError(std::size_t offset, std::string_view reason) {
		return std::runtime_error("text UTF-8 byte " + std::to_string(offset) + ": " + std::string(reason));
	}

	static std::uint32_t ReadScalar(std::string_view utf8, std::size_t& offset) {
		const std::size_t start = offset;
		const auto first = static_cast<std::uint8_t>(utf8[offset]);
		std::size_t count = 0;
		std::uint32_t minimum = 0;
		std::uint32_t scalar = 0;
		if (first < 0x80) {
			count = 1;
			scalar = first;
		} else if (first >= 0xc2 && first <= 0xdf) {
			count = 2;
			minimum = 0x80;
			scalar = first & 0x1f;
		} else if (first >= 0xe0 && first <= 0xef) {
			count = 3;
			minimum = 0x800;
			scalar = first & 0x0f;
		} else if (first >= 0xf0 && first <= 0xf4) {
			count = 4;
			minimum = 0x10000;
			scalar = first & 0x07;
		} else {
			throw TextEncodingError(start, "invalid leading byte");
		}

		if (count > utf8.size() - start) {
			throw TextEncodingError(start, "truncated sequence");
		}

		for (std::size_t index = 1; index < count; ++index) {
			const auto next = static_cast<std::uint8_t>(utf8[start + index]);
			if ((next & 0xc0) != 0x80) {
				throw TextEncodingError(start + index, "invalid continuation byte");
			}

			scalar = (scalar << 6) | (next & 0x3f);
		}

		if (scalar < minimum || scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff)) {
			throw TextEncodingError(start, "invalid Unicode scalar");
		}

		offset += count;

		return scalar;
	}

	std::vector<std::uint16_t> DecodeTextCodeUnits(std::string_view utf8) {
		if (utf8.size() > kMaximumTextBytes) {
			throw TextEncodingError(0, "input exceeds text byte limit");
		}

		std::vector<std::uint16_t> units;
		units.reserve(utf8.size());
		std::size_t offset = 0;
		while (offset < utf8.size()) {
			const auto scalar = ReadScalar(utf8, offset);
			if (scalar < 0x10000) {
				units.push_back(static_cast<std::uint16_t>(scalar));
			} else {
				const auto supplementary = scalar - 0x10000;
				units.push_back(static_cast<std::uint16_t>(0xd800 | (supplementary >> 10)));
				units.push_back(static_cast<std::uint16_t>(0xdc00 | (supplementary & 0x3ff)));
			}
		}

		return units;
	}

} // namespace sgre::text
