#include "sgre/text/variation_glyph.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

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
	std::array<NamedTableFixture, 7> tables;
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

static void RequireGlyph(const sgre::text::VectorFontVariationMap& query, std::uint32_t codepoint, std::uint32_t selector, std::uint32_t glyph) {
	const auto result = query.Query(codepoint, selector);
	Require(result.has_value() && *result == glyph, "native variation glyph differs");
}

static void TestDefaultExplicitPriorityAndMissing() {
	const auto bytes = MakeFont(0);
	const sgre::resource::SfntFont font(bytes);
	const sgre::text::VectorFontVariationMap query(font);
	RequireGlyph(query, 0x41, 0xfe00, 1);
	RequireGlyph(query, 0x43, 0xfe00, 3);
	RequireGlyph(query, 0x41, 0xfe01, 5);
	RequireGlyph(query, 0x44, 0xfe01, 6);
	RequireGlyph(query, 0, 0xfe01, 0xffff);
	RequireGlyph(query, 0x1f600, 0xfe01, 7);
	RequireGlyph(query, 0x41, 0xe0100, 1);
	RequireGlyph(query, 0xf040, 0xffffff, 5);
	Require(!query.Query(0x44, 0xe0100), "default missing glyph fell through to an explicit mapping");
	Require(!query.Query(0x42, 0xfe01), "explicit glyph zero became success");
	Require(!query.Query(0x1f600, 0xfe00), "default variation switched to alternate coverage map");
	for (const auto selector : {0U, 1U, 0xfe02U, 0x10fe00U, 0x8000fe01U, 0xffffffffU}) {
		Require(!query.Query(0x41, selector), "variation selector was truncated or broadened");
	}
	for (const auto codepoint : {0x110000U, 0x1000041U, 0x80000041U, 0xffffffffU}) {
		Require(!query.Query(codepoint, 0xfe01), "variation codepoint was truncated or broadened");
	}
}

static void TestCoverageAndOriginalOrderDiscovery() {
	for (const auto mode : {1U, 2U, 4U, 5U, 6U}) {
		const auto bytes = MakeFont(mode);
		const auto original = bytes;
		const sgre::resource::SfntFont font(bytes);
		const sgre::text::VectorFontVariationMap query(font);
		RequireGlyph(query, 0x41, 0xfe01, 5);
		RequireGlyph(query, 0xf040, 0xffffff, 5);
		RequireGlyph(query, 0x41, 0xfe00, mode == 1 ? 4U : 1U);
		if (mode == 1) {
			RequireGlyph(query, 0x1f600, 0xfe00, 2);
			RequireGlyph(query, 0x10ffff, 0xfe00, 7);
		}
		Require(bytes == original && font.SelectedCharacterMap() == 0, "borrowed variation query mutated font state");
	}
	const auto bytes = MakeFont(3);
	const sgre::resource::SfntFont font(bytes);
	const sgre::text::VectorFontVariationMap query(font);
	Require(!query.Query(0x41, 0xfe01), "font without variation map invented a mapping");
}

static std::uint32_t ReadBig32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
	std::uint32_t value = 0;
	for (std::size_t index = 0; index < 4; ++index) {
		value = (value << 8) | bytes.at(offset + index);
	}

	return value;
}

static void TestMalformedVariationTables() {
	const auto baseline = MakeFont(0);
	const auto cmap = ReadBig32(baseline, 12 + 6 * 16 + 8);
	const auto variation = cmap + ReadBig32(baseline, cmap + 8 + 2 * 8);
	const auto defaults = variation + ReadBig32(baseline, variation + 24);
	const auto mappings = variation + ReadBig32(baseline, variation + 39);
	for (std::uint32_t fault = 0; fault < 10; ++fault) {
		auto bytes = baseline;
		switch (fault) {
		case 0:
			WriteBig32(bytes, variation + 6, 0xffffffff);
			break;
		case 1:
			WriteBig24(bytes, variation + 10, 0);
			break;
		case 2:
			WriteBig24(bytes, variation + 21, 1);
			break;
		case 3:
			WriteBig32(bytes, variation + 24, ReadBig32(bytes, variation + 2));
			break;
		case 4:
			WriteBig32(bytes, defaults, 0xffffffff);
			break;
		case 5:
			WriteBig24(bytes, defaults + 8, 0x44);
			break;
		case 6:
			bytes.at(defaults + 15) = 1;
			break;
		case 7:
			WriteBig32(bytes, mappings, 0xffffffff);
			break;
		case 8:
			WriteBig24(bytes, mappings + 9, 0);
			break;
		case 9:
			WriteBig24(bytes, mappings + 29, 0x110000);
			break;
		}
		bool rejected = false;
		try {
			const sgre::resource::SfntFont font(bytes);
			const sgre::text::VectorFontVariationMap query(font);
			static_cast<void>(query);
		} catch (const std::runtime_error& error) {
			rejected = std::string_view(error.what()).find("sfnt variation") != std::string_view::npos;
		}
		Require(rejected, "malformed variation table did not fail with context");
	}
}

static std::uint32_t ReadWord() {
	std::uint32_t value = 0;
	for (std::uint32_t shift = 0; shift < 32; shift += 8) {
		const auto byte = std::cin.get();
		Require(byte != std::char_traits<char>::eof(), "truncated variation glyph proof input");
		value |= static_cast<std::uint32_t>(byte) << shift;
	}

	return value;
}

static void WriteWord(std::uint32_t value) {
	for (std::uint32_t shift = 0; shift < 32; shift += 8) {
		std::cout.put(static_cast<char>((value >> shift) & 0xffU));
	}
}

static void WriteFixture(std::uint32_t mode) {
	const auto bytes = MakeFont(mode);
	WriteWord(static_cast<std::uint32_t>(bytes.size()));
	for (const auto byte : bytes) {
		std::cout.put(static_cast<char>(byte));
	}
}

static void RunNativeDriver() {
	const auto font_size = ReadWord();
	Require(font_size <= 64 * 1024 * 1024, "variation glyph proof font exceeds byte limit");
	std::vector<std::uint8_t> bytes(font_size);
	for (auto& byte : bytes) {
		const auto value = std::cin.get();
		Require(value != std::char_traits<char>::eof(), "truncated variation glyph proof font");
		byte = static_cast<std::uint8_t>(value);
	}
	const sgre::resource::SfntFont font(bytes);
	const sgre::text::VectorFontVariationMap query(font);
	while (std::cin.peek() != std::char_traits<char>::eof()) {
		const auto count = ReadWord();
		Require(count <= 4096, "variation glyph proof count exceeds limit");
		for (std::uint32_t index = 0; index < count; ++index) {
			const auto codepoint = ReadWord();
			const auto selector = ReadWord();
			const auto glyph = query.Query(codepoint, selector);
			WriteWord(glyph.has_value() ? 1U : 0U);
			WriteWord(glyph.has_value() ? *glyph : 0xabcd1234U);
		}
		std::cout.flush();
	}
}

int main(int argc, char** argv) {
	try {
		if (argc == 2 && std::string_view(argv[1]) == "--native") {
			RunNativeDriver();

			return 0;
		}
		if (argc == 3 && std::string_view(argv[1]) == "--fixture") {
			Require(argv[2][0] >= '0' && argv[2][0] <= '6' && argv[2][1] == '\0', "invalid variation fixture mode");
			WriteFixture(static_cast<std::uint32_t>(argv[2][0] - '0'));

			return 0;
		}

		Require(argc == 1, "unsupported variation glyph fixture arguments");
		TestDefaultExplicitPriorityAndMissing();
		TestCoverageAndOriginalOrderDiscovery();
		TestMalformedVariationTables();
		std::cout << "VectorFont variation glyph: three groups passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
