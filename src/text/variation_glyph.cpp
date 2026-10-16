#include "sgre/text/variation_glyph.hpp"

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>

namespace sgre::text {

	static constexpr std::size_t kMaximumVariationValidationRecords = 1024 * 1024;

	static std::span<const std::uint8_t> ReadVariationRange(std::span<const std::uint8_t> bytes, std::size_t offset, std::size_t count, std::string_view field) {
		if (offset > bytes.size() || count > bytes.size() - offset) {
			throw std::runtime_error("sfnt variation " + std::string(field) + " exceeds cmap bytes");
		}

		return bytes.subspan(offset, count);
	}

	static std::uint32_t ReadVariationInteger(std::span<const std::uint8_t> bytes, std::size_t offset, std::size_t count) {
		const auto field = ReadVariationRange(bytes, offset, count, "integer");
		std::uint32_t value = 0;
		for (const auto byte : field) {
			value = (value << 8) | byte;
		}

		return value;
	}

	static std::span<const std::uint8_t> ReadVariationRecords(std::span<const std::uint8_t> bytes, std::size_t offset, std::size_t stride) {
		const auto count = ReadVariationInteger(bytes, offset, 4);
		static_cast<void>(ReadVariationRange(bytes, offset + 4, 0, "record header"));
		if (count > (bytes.size() - offset - 4) / stride) {
			throw std::runtime_error("sfnt variation record count exceeds cmap bytes");
		}

		return ReadVariationRange(bytes, offset + 4, std::size_t(count) * stride, "records");
	}

	static void ValidateDefaultVariationRanges(std::span<const std::uint8_t> bytes, std::size_t offset, std::size_t& remaining_records) {
		const auto records = ReadVariationRecords(bytes, offset, 4);
		if (records.size() / 4 > remaining_records) {
			throw std::runtime_error("sfnt variation validation exceeds record work limit");
		}
		remaining_records -= records.size() / 4;

		std::uint32_t next_codepoint = 0;
		for (std::size_t index = 0; index < records.size(); index += 4) {
			const auto start = ReadVariationInteger(records, index, 3);
			const auto end = start + records[index + 3];
			if (start < next_codepoint || end >= 0x110000) {
				throw std::runtime_error("sfnt variation default ranges overlap, are unordered, or exceed Unicode range");
			}
			next_codepoint = end + 1;
		}
	}

	static void ValidateExplicitVariationMappings(std::span<const std::uint8_t> bytes, std::size_t offset, std::size_t& remaining_records) {
		const auto records = ReadVariationRecords(bytes, offset, 5);
		if (records.size() / 5 > remaining_records) {
			throw std::runtime_error("sfnt variation validation exceeds record work limit");
		}
		remaining_records -= records.size() / 5;

		std::uint32_t next_codepoint = 0;
		for (std::size_t index = 0; index < records.size(); index += 5) {
			const auto codepoint = ReadVariationInteger(records, index, 3);
			if (codepoint < next_codepoint || codepoint >= 0x110000) {
				throw std::runtime_error("sfnt variation explicit mappings duplicate, are unordered, or exceed Unicode range");
			}
			next_codepoint = codepoint + 1;
		}
	}

	static void ValidateVariationMap(std::span<const std::uint8_t> bytes) {
		static_cast<void>(ReadVariationRange(bytes, 0, 10, "format 14 header"));
		const auto length = ReadVariationInteger(bytes, 2, 4);
		const auto count = ReadVariationInteger(bytes, 6, 4);
		if (length < 10 || length > bytes.size() || count > (length - 10) / 11) {
			throw std::runtime_error("sfnt variation selector records exceed format 14 length");
		}

		if (count > kMaximumVariationValidationRecords) {
			throw std::runtime_error("sfnt variation selectors exceed record work limit");
		}

		std::size_t remaining_records = kMaximumVariationValidationRecords - count;
		std::uint32_t next_selector = 1;
		for (std::size_t index = 0; index < count; ++index) {
			const auto record = 10 + index * 11;
			const auto selector = ReadVariationInteger(bytes, record, 3);
			const auto default_offset = ReadVariationInteger(bytes, record + 3, 4);
			const auto explicit_offset = ReadVariationInteger(bytes, record + 7, 4);
			if (selector < next_selector || default_offset >= length || explicit_offset >= length) {
				throw std::runtime_error("sfnt variation selectors are unordered or their offsets exceed format 14 length");
			}
			next_selector = selector + 1;
			// Native validation bounds referenced records by the containing cmap, not the declared subtable end.
			if (default_offset != 0) {
				ValidateDefaultVariationRanges(bytes, default_offset, remaining_records);
			}
			if (explicit_offset != 0) {
				ValidateExplicitVariationMappings(bytes, explicit_offset, remaining_records);
			}
		}
	}

	static std::optional<std::size_t> FindVariationRecord(std::span<const std::uint8_t> records, std::size_t stride, std::uint32_t key) {
		std::size_t first = 0;
		std::size_t last = records.size() / stride;
		while (first < last) {
			const auto middle = first + (last - first) / 2;
			const auto value = ReadVariationInteger(records, middle * stride, 3);
			if (key < value) {
				last = middle;
			} else if (key > value) {
				first = middle + 1;
			} else {
				return middle * stride;
			}
		}

		return std::nullopt;
	}

	static bool IsDefaultVariation(std::span<const std::uint8_t> records, std::uint32_t codepoint) {
		std::size_t first = 0;
		std::size_t last = records.size() / 4;
		while (first < last) {
			const auto middle = first + (last - first) / 2;
			const auto start = ReadVariationInteger(records, middle * 4, 3);
			const auto end = start + records[middle * 4 + 3];
			if (codepoint < start) {
				last = middle;
			} else if (codepoint > end) {
				first = middle + 1;
			} else {
				return true;
			}
		}

		return false;
	}

	VectorFontVariationMap::VectorFontVariationMap(const resource::SfntFont& font)
	    : m_Font(&font) {
		const auto maps = font.CharacterMaps();
		for (const auto& map : maps) {
			if (map.PlatformId != 0 || map.EncodingId != 5 || map.Format != 14) {
				continue;
			}

			const auto cmap = font.FindTable("cmap");
			if (!cmap) {
				throw std::runtime_error("sfnt variation map lacks its containing cmap");
			}
			static_cast<void>(ReadVariationRange(*cmap, map.Offset, map.Size, "subtable"));
			m_Map = cmap->subspan(map.Offset);
			ValidateVariationMap(m_Map);

			return;
		}
	}

	std::optional<std::uint32_t> VectorFontVariationMap::Query(std::uint32_t codepoint, std::uint32_t selector) const {
		if (m_Map.empty()) {
			return std::nullopt;
		}

		const auto selector_count = ReadVariationInteger(m_Map, 6, 4);
		const auto selectors = m_Map.subspan(10, std::size_t(selector_count) * 11);
		const auto record = FindVariationRecord(selectors, 11, selector);
		if (!record) {
			return std::nullopt;
		}

		const auto default_offset = ReadVariationInteger(selectors, *record + 3, 4);
		const auto explicit_offset = ReadVariationInteger(selectors, *record + 7, 4);
		std::uint32_t glyph = 0;
		if (default_offset != 0 && IsDefaultVariation(ReadVariationRecords(m_Map, default_offset, 4), codepoint)) {
			glyph = m_Font->MapCharacter(codepoint);
		} else if (explicit_offset != 0) {
			const auto mappings = ReadVariationRecords(m_Map, explicit_offset, 5);
			if (const auto mapping = FindVariationRecord(mappings, 5, codepoint)) {
				glyph = ReadVariationInteger(mappings, *mapping + 3, 2);
			}
		}
		if (glyph == 0) {
			return std::nullopt;
		}

		return glyph;
	}

} // namespace sgre::text
