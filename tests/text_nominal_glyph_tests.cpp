#include "sgre/text/nominal_glyph.hpp"

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

struct NamedTableFixture {
	std::string_view Tag;
	std::vector<std::uint8_t> Bytes;
};

static std::vector<std::uint8_t> MakeFont(bool prefer_coverage, bool prefer_windows = false) {
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
	tables[6].Bytes.resize(20);
	WriteBig16(tables[6].Bytes, 2, 2);
	WriteBig16(tables[6].Bytes, 4, prefer_windows ? 3 : 0);
	WriteBig16(tables[6].Bytes, 6, prefer_windows ? 1 : 3);
	WriteBig32(tables[6].Bytes, 8, 20);
	WriteBig16(tables[6].Bytes, 12, 0);
	WriteBig16(tables[6].Bytes, 14, 4);
	WriteBig32(tables[6].Bytes, 16, 72);
	const auto segment = MakeSegmentMap();
	const auto coverage = MakeCoverageMap();
	const auto& first = prefer_coverage ? coverage : segment;
	const auto& second = prefer_coverage ? segment : coverage;
	tables[6].Bytes.insert(tables[6].Bytes.end(), first.begin(), first.end());
	tables[6].Bytes.insert(tables[6].Bytes.end(), second.begin(), second.end());

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

static void RequireGlyph(const sgre::resource::SfntFont& font, std::uint32_t codepoint, std::uint32_t glyph) {
	const auto result = sgre::text::QueryVectorFontNominalGlyph(font, codepoint);
	Require(result.has_value() && *result == glyph, "native nominal glyph differs");
}

static void TestSelectedSegmentGlyphsAndMissingPolicy() {
	const auto bytes = MakeFont(false);
	const sgre::resource::SfntFont font(bytes);
	RequireGlyph(font, 0x41, 1);
	RequireGlyph(font, 0x42, 2);
	RequireGlyph(font, 0x43, 3);
	RequireGlyph(font, 0x61, 3);
	RequireGlyph(font, 0xf040, 5);
	RequireGlyph(font, 0xf041, 6);
	for (const auto codepoint : {0U, 0x40U, 0x62U, 0xffffU}) {
		Require(!sgre::text::QueryVectorFontNominalGlyph(font, codepoint), "glyph zero became a successful nominal mapping");
	}
}

static void TestSelectedCoverageAndUnsignedInputs() {
	const auto bytes = MakeFont(true);
	const sgre::resource::SfntFont font(bytes);
	RequireGlyph(font, 0x41, 4);
	RequireGlyph(font, 0x1f600, 2);
	RequireGlyph(font, 0x1f601, 3);
	RequireGlyph(font, 0x10ffff, 7);
	for (const auto codepoint : {0U, 0x42U, 0xd800U, 0x1f602U, 0x110000U, 0x80000041U, 0xffffffffU}) {
		Require(!sgre::text::QueryVectorFontNominalGlyph(font, codepoint), "nominal callback truncated an unsigned key or broadened coverage");
	}
}

static void TestPreferredMapWithoutAlternateOrSymbolRetry() {
	const auto bytes = MakeFont(false, true);
	const sgre::resource::SfntFont font(bytes);
	Require(font.SelectedCharacterMap() == 0 && font.CharacterMaps()[0].PlatformId == 3, "native preferred Windows Unicode map was not used");
	Require(font.MapCharacter(0x1f600, 1) == 2, "independent alternate coverage fixture is missing");
	Require(!sgre::text::QueryVectorFontNominalGlyph(font, 0x1f600), "nominal callback switched to an alternate map");
	Require(!sgre::text::QueryVectorFontNominalGlyph(font, 0x40) && font.MapCharacter(0xf040) == 5, "Unicode nominal callback added the symbol retry");
}

static void TestBorrowedImmutableFontAndRepeatedQueries() {
	const auto bytes = MakeFont(false);
	const auto original = bytes;
	const sgre::resource::SfntFont font(bytes);
	for (std::uint32_t iteration = 0; iteration < 64; ++iteration) {
		RequireGlyph(font, 0x43, 3);
		Require(!sgre::text::QueryVectorFontNominalGlyph(font, 0x10043), "supplementary input aliased a BMP key");
		Require(!sgre::text::QueryVectorFontNominalGlyph(font, 0xffffffff), "outside input was treated as a successful glyph");
	}
	Require(bytes == original && font.GlyphCount() == 8 && font.SelectedCharacterMap() == 0, "nominal query mutated its borrowed font");
}

static std::uint32_t ReadWord() {
	std::uint32_t value = 0;
	for (std::uint32_t shift = 0; shift < 32; shift += 8) {
		const auto byte = std::cin.get();
		Require(byte != std::char_traits<char>::eof(), "truncated nominal glyph proof input");
		value |= static_cast<std::uint32_t>(byte) << shift;
	}

	return value;
}

static void WriteWord(std::uint32_t value) {
	for (std::uint32_t shift = 0; shift < 32; shift += 8) {
		std::cout.put(static_cast<char>((value >> shift) & 0xffU));
	}
}

static void WriteFixture(bool coverage) {
	const auto bytes = MakeFont(coverage);
	WriteWord(static_cast<std::uint32_t>(bytes.size()));
	for (const auto byte : bytes) {
		std::cout.put(static_cast<char>(byte));
	}
}

static void RunNativeDriver() {
	const auto font_size = ReadWord();
	Require(font_size <= 64 * 1024 * 1024, "nominal glyph proof font exceeds its byte limit");
	std::vector<std::uint8_t> bytes(font_size);
	for (auto& byte : bytes) {
		const auto value = std::cin.get();
		Require(value != std::char_traits<char>::eof(), "truncated nominal glyph proof font");
		byte = static_cast<std::uint8_t>(value);
	}
	const sgre::resource::SfntFont font(bytes);
	while (std::cin.peek() != std::char_traits<char>::eof()) {
		const auto first = ReadWord();
		const auto count = ReadWord();
		Require(count <= 4096 && (count == 0 || count - 1 <= 0xffffffff - first), "nominal glyph proof range exceeds its count limit or wraps");
		for (std::uint32_t offset = 0; offset < count; ++offset) {
			const auto glyph = sgre::text::QueryVectorFontNominalGlyph(font, first + offset);
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
		if (argc == 2 && (std::string_view(argv[1]) == "--fixture-4" || std::string_view(argv[1]) == "--fixture-12")) {
			WriteFixture(std::string_view(argv[1]) == "--fixture-12");

			return 0;
		}

		Require(argc == 1, "unsupported nominal glyph fixture arguments");
		TestSelectedSegmentGlyphsAndMissingPolicy();
		TestSelectedCoverageAndUnsignedInputs();
		TestPreferredMapWithoutAlternateOrSymbolRetry();
		TestBorrowedImmutableFontAndRepeatedQueries();
		std::cout << "VectorFont nominal glyph: four groups passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
