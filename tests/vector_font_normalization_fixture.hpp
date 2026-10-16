#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

static void WriteBig16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
	bytes.at(offset) = static_cast<std::uint8_t>(value >> 8);
	bytes.at(offset + 1) = static_cast<std::uint8_t>(value & 0xffU);
}

static void WriteBig32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
	WriteBig16(bytes, offset, static_cast<std::uint16_t>(value >> 16));
	WriteBig16(bytes, offset + 2, static_cast<std::uint16_t>(value & 0xffffU));
}

static std::vector<std::uint8_t> MakeSegmentMap() {
	std::vector<std::uint8_t> bytes(52);
	WriteBig16(bytes, 0, 4);
	WriteBig16(bytes, 2, 52);
	WriteBig16(bytes, 6, 8);
	WriteBig16(bytes, 8, 8);
	WriteBig16(bytes, 10, 2);
	const std::array<std::uint16_t, 19> fields = {0x43, 0x62, 0xf041, 0xffff, 0, 0x41, 0x61, 0xf040, 0xffff, 0xffc0, 2, 0x0fc5, 1, 0, 6, 0, 0, 1, 0};
	for (std::size_t index = 0; index < fields.size(); ++index) {
		WriteBig16(bytes, 14 + index * 2, fields[index]);
	}

	return bytes;
}

static std::vector<std::uint8_t> MakeCoverageMap() {
	std::vector<std::uint8_t> bytes(52);
	WriteBig16(bytes, 0, 12);
	WriteBig32(bytes, 4, 52);
	WriteBig32(bytes, 12, 3);
	const std::array<std::uint32_t, 9> fields = {0x41, 0x41, 4, 0x1f600, 0x1f601, 2, 0x10ffff, 0x10ffff, 7};
	for (std::size_t index = 0; index < fields.size(); ++index) {
		WriteBig32(bytes, 16 + index * 4, fields[index]);
	}

	return bytes;
}

static void WriteBig24(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
	bytes.at(offset) = static_cast<std::uint8_t>(value >> 16);
	WriteBig16(bytes, offset + 1, static_cast<std::uint16_t>(value & 0xffffU));
}

static std::vector<std::uint8_t> MakeVariationMap(bool alternate = false, bool short_length = false) {
	std::vector<std::uint8_t> bytes(10 + 5 * 11);
	WriteBig16(bytes, 0, 14);
	WriteBig32(bytes, 6, 5);
	const std::array<std::uint32_t, 5> selectors = {1, 0xfe00, 0xfe01, 0xe0100, 0xffffff};
	for (std::size_t index = 0; index < selectors.size(); ++index) {
		WriteBig24(bytes, 10 + index * 11, selectors[index]);
	}

	const auto default_offset = bytes.size();
	bytes.resize(bytes.size() + 16);
	WriteBig32(bytes, default_offset, 3);
	const std::array<std::uint32_t, 3> starts = {0x41, 0x1f600, 0x10ffff};
	const std::array<std::uint8_t, 3> extra = {3, 1, 0};
	for (std::size_t index = 0; index < starts.size(); ++index) {
		WriteBig24(bytes, default_offset + 4 + index * 4, starts[index]);
		bytes[default_offset + 7 + index * 4] = extra[index];
	}
	WriteBig32(bytes, 24, static_cast<std::uint32_t>(default_offset));
	WriteBig32(bytes, 46, static_cast<std::uint32_t>(default_offset));

	const auto explicit_offset = bytes.size();
	bytes.resize(bytes.size() + 34);
	WriteBig32(bytes, explicit_offset, 6);
	const std::array<std::uint32_t, 6> codepoints = {0, 0x41, 0x42, 0x44, 0x1f600, 0x10ffff};
	const std::array<std::uint16_t, 6> glyphs = {0xffff, static_cast<std::uint16_t>(alternate ? 6 : 5), 0, 6, 7, 4};
	for (std::size_t index = 0; index < codepoints.size(); ++index) {
		WriteBig24(bytes, explicit_offset + 4 + index * 5, codepoints[index]);
		WriteBig16(bytes, explicit_offset + 7 + index * 5, glyphs[index]);
	}
	WriteBig32(bytes, 39, static_cast<std::uint32_t>(explicit_offset));
	WriteBig32(bytes, 50, static_cast<std::uint32_t>(explicit_offset));

	const auto highest_offset = bytes.size();
	bytes.resize(bytes.size() + 9);
	WriteBig32(bytes, highest_offset, 1);
	WriteBig24(bytes, highest_offset + 4, 0xf040);
	WriteBig16(bytes, highest_offset + 7, 5);
	WriteBig32(bytes, 61, static_cast<std::uint32_t>(highest_offset));
	WriteBig32(bytes, 2, static_cast<std::uint32_t>(short_length ? highest_offset + 4 : bytes.size()));

	return bytes;
}

struct CharacterMapFixture {
	std::uint16_t Platform = 0;
	std::uint16_t Encoding = 0;
	std::vector<std::uint8_t> Bytes;
};

struct NamedTableFixture {
	std::string_view Tag;
	std::vector<std::uint8_t> Bytes;
};

static std::vector<std::uint8_t> MakeFont(std::uint32_t mode) {
	std::vector<NamedTableFixture> tables(7);
	tables[0].Tag = "head";
	tables[0].Bytes.resize(54);
	WriteBig32(tables[0].Bytes, 0, 0x00010000);
	WriteBig32(tables[0].Bytes, 12, 0x5f0f3cf5);
	WriteBig16(tables[0].Bytes, 18, 1000);
	tables[1].Tag = "maxp";
	tables[1].Bytes.resize(32);
	WriteBig32(tables[1].Bytes, 0, 0x00010000);
	WriteBig16(tables[1].Bytes, 4, 8);
	tables[2].Tag = "hhea";
	tables[2].Bytes.resize(36);
	WriteBig32(tables[2].Bytes, 0, 0x00010000);
	WriteBig16(tables[2].Bytes, 34, 1);
	tables[3].Tag = "hmtx";
	tables[3].Bytes.resize(18);
	WriteBig16(tables[3].Bytes, 0, 500);
	tables[4].Tag = "glyf";
	tables[5].Tag = "loca";
	tables[5].Bytes.resize(18);
	tables[6].Tag = "cmap";
	const auto segment = MakeSegmentMap();
	const auto coverage = MakeCoverageMap();
	std::vector<CharacterMapFixture> maps;
	maps.push_back({static_cast<std::uint16_t>(mode == 6 ? 3 : 0), static_cast<std::uint16_t>(mode == 6 ? 1 : 3), mode == 1 ? coverage : segment});
	maps.push_back({0, 4, mode == 1 ? segment : coverage});
	if (mode == 4) {
		maps.push_back({3, 5, MakeVariationMap(true)});
		maps.push_back({0, 4, MakeVariationMap(true)});
		maps.push_back({0, 5, coverage});
	}
	if (mode != 3) {
		maps.push_back({0, 5, MakeVariationMap(false, mode == 5)});
		if (mode == 2) {
			maps.push_back({0, 5, MakeVariationMap(true)});
		}
	}
	tables[6].Bytes.resize(4 + maps.size() * 8);
	WriteBig16(tables[6].Bytes, 2, static_cast<std::uint16_t>(maps.size()));
	for (std::size_t index = 0; index < maps.size(); ++index) {
		WriteBig16(tables[6].Bytes, 4 + index * 8, maps[index].Platform);
		WriteBig16(tables[6].Bytes, 6 + index * 8, maps[index].Encoding);
		WriteBig32(tables[6].Bytes, 8 + index * 8, static_cast<std::uint32_t>(tables[6].Bytes.size()));
		tables[6].Bytes.insert(tables[6].Bytes.end(), maps[index].Bytes.begin(), maps[index].Bytes.end());
	}

	if (mode == 7 || mode == 8) {
		NamedTableFixture definition;
		definition.Tag = "GDEF";
		definition.Bytes.resize(mode == 8 ? 34 : 12);
		WriteBig16(definition.Bytes, 0, 1);
		if (mode == 8) {
			WriteBig16(definition.Bytes, 4, 12);
			WriteBig16(definition.Bytes, 12, 1);
			WriteBig16(definition.Bytes, 16, 8);
			const std::array<std::uint16_t, 8> classes = {0, 1, 2, 3, 1, 2, 3, 4};
			for (std::size_t index = 0; index < classes.size(); ++index) {
				WriteBig16(definition.Bytes, 18 + index * 2, classes[index]);
			}
		}
		tables.push_back(std::move(definition));
	}
	if (mode == 9) {
		NamedTableFixture positioning;
		positioning.Tag = "GPOS";
		positioning.Bytes.resize(10);
		WriteBig16(positioning.Bytes, 0, 1);
		tables.push_back(std::move(positioning));
	}

	std::vector<std::uint8_t> bytes(12 + tables.size() * 16);
	WriteBig32(bytes, 0, 0x00010000);
	WriteBig16(bytes, 4, static_cast<std::uint16_t>(tables.size()));
	for (std::size_t index = 0; index < tables.size(); ++index) {
		while (bytes.size() % 4 != 0) {
			bytes.push_back(0);
		}
		const auto& table = tables[index];
		const auto record = 12 + index * 16;
		for (std::size_t character = 0; character < 4; ++character) {
			bytes[record + character] = static_cast<std::uint8_t>(table.Tag[character]);
		}
		WriteBig32(bytes, record + 8, static_cast<std::uint32_t>(bytes.size()));
		WriteBig32(bytes, record + 12, static_cast<std::uint32_t>(table.Bytes.size()));
		bytes.insert(bytes.end(), table.Bytes.begin(), table.Bytes.end());
	}

	return bytes;
}
