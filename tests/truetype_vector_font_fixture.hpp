#pragma once

#include "sgre/resource/sfnt.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace truetype_vector_test {
	inline void RequireFixture(bool condition, std::string_view message) {
		if (!condition) {
			throw std::runtime_error(std::string(message));
		}
	}

	inline void Write16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
		bytes.at(offset) = static_cast<std::uint8_t>(value >> 8);
		bytes.at(offset + 1) = static_cast<std::uint8_t>(value);
	}

	inline void Write32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
		Write16(bytes, offset, static_cast<std::uint16_t>(value >> 16));
		Write16(bytes, offset + 2, static_cast<std::uint16_t>(value));
	}

	inline void Append16(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
		bytes.push_back(static_cast<std::uint8_t>(value >> 8));
		bytes.push_back(static_cast<std::uint8_t>(value));
	}

	inline void AppendSigned16(std::vector<std::uint8_t>& bytes, std::int16_t value) {
		Append16(bytes, std::bit_cast<std::uint16_t>(value));
	}

	inline std::uint32_t SumWords(std::span<const std::uint8_t> bytes) {
		std::uint32_t result = 0;
		for (std::size_t offset = 0; offset < bytes.size(); offset += 4) {
			std::uint32_t word = 0;
			for (std::size_t index = 0; index < 4; ++index) {
				word = (word << 8) | (offset + index < bytes.size() ? bytes[offset + index] : 0u);
			}

			result += word;
		}

		return result;
	}

	struct GlyphDefinition {
		std::vector<std::uint8_t> Bytes;
		sgre::resource::SfntGlyphMetrics Horizontal{200, 10};
		sgre::resource::SfntGlyphMetrics Vertical{300, 20};
	};

	struct FontFixture {
		std::vector<std::uint8_t> Bytes;
		std::map<std::string, std::size_t> Offsets;
	};

	inline FontFixture MakeFont(std::span<const GlyphDefinition> glyphs, bool vertical = true, bool long_loca = false, std::string_view extra_table = {}) {
		RequireFixture(!glyphs.empty() && glyphs.size() <= 128, "fixture glyph count is outside its small bound");
		std::map<std::string, std::vector<std::uint8_t>> tables;
		// One valid Unicode format4 map containing only the sentinel; tests select actual glyph IDs.
		tables["cmap"].resize(36);
		Write16(tables["cmap"], 2, 1);
		Write16(tables["cmap"], 6, 3);
		Write32(tables["cmap"], 8, 12);
		Write16(tables["cmap"], 12, 4);
		Write16(tables["cmap"], 14, 24);
		Write16(tables["cmap"], 18, 2);
		Write16(tables["cmap"], 20, 2);
		Write16(tables["cmap"], 26, 0xffff);
		Write16(tables["cmap"], 30, 0xffff);
		Write16(tables["cmap"], 32, 1);
		tables["head"].resize(54);
		Write32(tables["head"], 0, 0x00010000);
		Write32(tables["head"], 12, 0x5f0f3cf5);
		Write16(tables["head"], 18, 1000);
		Write16(tables["head"], 50, long_loca ? 1 : 0);
		tables["hhea"].resize(36);
		Write32(tables["hhea"], 0, 0x00010000);
		Write16(tables["hhea"], 34, static_cast<std::uint16_t>(glyphs.size()));
		tables["maxp"].resize(32);
		Write32(tables["maxp"], 0, 0x00010000);
		Write16(tables["maxp"], 4, static_cast<std::uint16_t>(glyphs.size()));
		tables["glyf"] = {};
		tables["loca"].resize((glyphs.size() + 1) * (long_loca ? 4u : 2u));
		if (vertical) {
			tables["vhea"].resize(36);
			Write32(tables["vhea"], 0, 0x00010000);
			Write16(tables["vhea"], 34, static_cast<std::uint16_t>(glyphs.size()));
		}

		for (std::size_t index = 0; index < glyphs.size(); ++index) {
			const auto offset = tables["glyf"].size();
			RequireFixture(offset <= 65534, "fixture glyf storage exceeds its small bound");
			if (long_loca) {
				Write32(tables["loca"], index * 4, static_cast<std::uint32_t>(offset));
			} else {
				Write16(tables["loca"], index * 2, static_cast<std::uint16_t>(offset / 2));
			}

			tables["glyf"].insert(tables["glyf"].end(), glyphs[index].Bytes.begin(), glyphs[index].Bytes.end());
			if (tables["glyf"].size() % 2 != 0) {
				tables["glyf"].push_back(0);
			}

			Append16(tables["hmtx"], glyphs[index].Horizontal.Advance);
			AppendSigned16(tables["hmtx"], glyphs[index].Horizontal.SideBearing);
			if (vertical) {
				Append16(tables["vmtx"], glyphs[index].Vertical.Advance);
				AppendSigned16(tables["vmtx"], glyphs[index].Vertical.SideBearing);
			}
		}

		if (long_loca) {
			Write32(tables["loca"], glyphs.size() * 4, static_cast<std::uint32_t>(tables["glyf"].size()));
		} else {
			Write16(tables["loca"], glyphs.size() * 2, static_cast<std::uint16_t>(tables["glyf"].size() / 2));
		}
		if (!extra_table.empty()) {
			tables[std::string(extra_table)] = {0};
		}

		FontFixture fixture;
		fixture.Bytes.resize(12 + tables.size() * 16);
		Write32(fixture.Bytes, 0, 0x00010000);
		Write16(fixture.Bytes, 4, static_cast<std::uint16_t>(tables.size()));
		std::size_t index = 0;
		for (const auto& [tag, table] : tables) {
			while (fixture.Bytes.size() % 4 != 0) {
				fixture.Bytes.push_back(0);
			}

			const auto record = 12 + index++ * 16;
			fixture.Offsets[tag] = fixture.Bytes.size();
			std::copy(tag.begin(), tag.end(), fixture.Bytes.begin() + static_cast<std::ptrdiff_t>(record));
			Write32(fixture.Bytes, record + 4, SumWords(table));
			Write32(fixture.Bytes, record + 8, static_cast<std::uint32_t>(fixture.Bytes.size()));
			Write32(fixture.Bytes, record + 12, static_cast<std::uint32_t>(table.size()));
			fixture.Bytes.insert(fixture.Bytes.end(), table.begin(), table.end());
		}

		Write32(fixture.Bytes, fixture.Offsets.at("head") + 8, 0xb1b0afba - SumWords(fixture.Bytes));

		return fixture;
	}

	inline std::vector<std::uint8_t> Header(std::int16_t contours, std::int16_t left, std::int16_t bottom, std::int16_t right, std::int16_t top) {
		std::vector<std::uint8_t> bytes;
		for (const auto value : {contours, left, bottom, right, top}) {
			AppendSigned16(bytes, value);
		}

		return bytes;
	}

	inline GlyphDefinition Rectangle() {
		GlyphDefinition result;
		result.Bytes = Header(1, 0, 0, 100, 50);
		// Four on-curve points: (0,0),(100,0),(100,50),(0,50).
		const std::array<std::uint8_t, 11> payload{0, 3, 0, 0, 0x31, 0x33, 0x35, 0x23, 100, 100, 50};
		result.Bytes.insert(result.Bytes.end(), payload.begin(), payload.end());

		return result;
	}

} // namespace truetype_vector_test
