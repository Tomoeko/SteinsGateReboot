#include "sgre/resource/opentype_layout.hpp"

#include <algorithm>
#include <array>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static void RequireFailure(const std::function<void()>& operation, std::string_view context) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, "OpenType failure lost its context");

		return;
	}

	throw std::runtime_error("invalid OpenType layout was accepted");
}

static void WriteWord(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t word) {
	bytes.at(offset) = static_cast<std::uint8_t>(word >> 8);
	bytes.at(offset + 1) = static_cast<std::uint8_t>(word);
}

static void WriteDoubleWord(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t word) {
	WriteWord(bytes, offset, static_cast<std::uint16_t>(word >> 16));
	WriteWord(bytes, offset + 2, static_cast<std::uint16_t>(word));
}

static void WriteTag(std::vector<std::uint8_t>& bytes, std::size_t offset, std::string_view tag) {
	std::copy(tag.begin(), tag.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset));
}

static std::vector<std::uint8_t> Words(std::initializer_list<std::uint16_t> words) {
	std::vector<std::uint8_t> bytes(words.size() * 2);
	std::size_t offset = 0;
	for (const auto word : words) {
		WriteWord(bytes, offset, word);
		offset += 2;
	}

	return bytes;
}

static void Append(std::vector<std::uint8_t>& bytes, std::span<const std::uint8_t> appended) {
	bytes.insert(bytes.end(), appended.begin(), appended.end());
}

static std::vector<std::uint8_t> MakeSubstitutionTable() {
	std::vector<std::uint8_t> scripts(14);
	WriteWord(scripts, 0, 2);
	WriteTag(scripts, 2, "DFLT");
	WriteWord(scripts, 6, 14);
	WriteTag(scripts, 8, "hani");
	WriteWord(scripts, 12, 28);
	Append(scripts, Words({4, 0, 0, 0xffff, 2, 0, 1}));
	std::vector<std::uint8_t> hani(10);
	WriteWord(hani, 0, 10);
	WriteWord(hani, 2, 1);
	WriteTag(hani, 4, "JAN ");
	WriteWord(hani, 8, 18);
	Append(hani, Words({0, 0xffff, 1, 0}));
	Append(hani, Words({0, 1, 1, 1}));
	Append(scripts, hani);

	std::vector<std::uint8_t> features(14);
	WriteWord(features, 0, 2);
	WriteTag(features, 2, "vert");
	WriteWord(features, 6, 14);
	WriteTag(features, 8, "vrt2");
	WriteWord(features, 12, 20);
	Append(features, Words({0, 1, 0}));
	Append(features, Words({0, 1, 1}));

	// Single format 1 covers glyphs 1/3 with delta +1; format 2 covers 2/4 with outputs 5/6.
	auto lookups = Words({2, 6, 28});
	Append(lookups, Words({1, 0, 1, 8, 1, 6, 1, 1, 2, 1, 3}));
	Append(lookups, Words({1, 0, 1, 8, 2, 10, 2, 5, 6, 2, 2, 2, 2, 0, 4, 4, 1}));
	std::vector<std::uint8_t> table(10);
	WriteDoubleWord(table, 0, 0x00010000);
	WriteWord(table, 4, static_cast<std::uint16_t>(table.size()));
	Append(table, scripts);
	WriteWord(table, 6, static_cast<std::uint16_t>(table.size()));
	Append(table, features);
	WriteWord(table, 8, static_cast<std::uint16_t>(table.size()));
	Append(table, lookups);

	return table;
}

static std::vector<std::uint8_t> MakeDefinitionTable(bool class_array) {
	std::vector<std::uint8_t> table(12);
	WriteDoubleWord(table, 0, 0x00010000);
	WriteWord(table, 4, 12);
	Append(table, class_array ? Words({1, 1, 5, 1, 1, 1, 1, 3}) : Words({2, 2, 1, 4, 1, 5, 5, 3}));

	return table;
}

struct LayoutFixture {
	std::vector<std::uint8_t> Bytes;
	std::map<std::string, std::size_t> TableOffsets;
};

static LayoutFixture MakeFont(const std::vector<std::uint8_t>& substitution, const std::vector<std::uint8_t>& definition, std::uint16_t glyph_count = 8) {
	std::map<std::string, std::vector<std::uint8_t>> tables;
	tables["CFF "] = {1, 0, 4, 4};
	tables["head"].resize(54);
	WriteDoubleWord(tables["head"], 0, 0x00010000);
	WriteDoubleWord(tables["head"], 12, 0x5f0f3cf5);
	WriteWord(tables["head"], 18, 1000);
	tables["hhea"].resize(36);
	WriteDoubleWord(tables["hhea"], 0, 0x00010000);
	WriteWord(tables["hhea"], 34, 1);
	tables["hmtx"].resize(4 + std::size_t(glyph_count - 1) * 2);
	WriteWord(tables["hmtx"], 0, 1000);
	tables["maxp"] = Words({0, 0x5000, glyph_count});
	tables["cmap"].resize(40);
	WriteWord(tables["cmap"], 2, 1);
	WriteWord(tables["cmap"], 6, 3);
	WriteDoubleWord(tables["cmap"], 8, 12);
	WriteWord(tables["cmap"], 12, 12);
	WriteDoubleWord(tables["cmap"], 16, 28);
	WriteDoubleWord(tables["cmap"], 24, 1);
	WriteDoubleWord(tables["cmap"], 28, 0x41);
	WriteDoubleWord(tables["cmap"], 32, 0x44);
	WriteDoubleWord(tables["cmap"], 36, 1);
	if (!substitution.empty()) {
		tables["GSUB"] = substitution;
	}
	if (!definition.empty()) {
		tables["GDEF"] = definition;
	}

	LayoutFixture fixture;
	fixture.Bytes.resize(12 + tables.size() * 16);
	WriteDoubleWord(fixture.Bytes, 0, 0x4f54544f);
	WriteWord(fixture.Bytes, 4, static_cast<std::uint16_t>(tables.size()));
	std::size_t index = 0;
	for (const auto& [tag, table] : tables) {
		while (fixture.Bytes.size() % 4 != 0) {
			fixture.Bytes.push_back(0);
		}
		const auto record = 12 + index++ * 16;
		WriteTag(fixture.Bytes, record, tag);
		WriteDoubleWord(fixture.Bytes, record + 8, static_cast<std::uint32_t>(fixture.Bytes.size()));
		WriteDoubleWord(fixture.Bytes, record + 12, static_cast<std::uint32_t>(table.size()));
		fixture.TableOffsets[tag] = fixture.Bytes.size();
		Append(fixture.Bytes, table);
	}

	return fixture;
}

static std::vector<std::uint8_t> MakePropertiesTable(bool array_format) {
	std::vector<std::uint8_t> table(12);
	WriteDoubleWord(table, 0, 0x10000);
	WriteWord(table, 4, 12);
	Append(table, array_format ? Words({1, 0, 8, 0, 1, 2, 3, 4, 3, 1, 0}) : Words({2, 5, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 3}));
	WriteWord(table, 10, static_cast<std::uint16_t>(table.size()));
	Append(table, array_format ? Words({1, 0, 8, 7, 11, 12, 0x1234, 13, 0xffff, 0x100, 0}) : Words({2, 4, 0, 0, 7, 3, 3, 0x1234, 5, 5, 0xffff, 6, 6, 0x100}));

	return table;
}

static void TestBothClassFormatsAndPackedWidth() {
	for (const bool array_format : {false, true}) {
		const auto fixture = MakeFont(MakeSubstitutionTable(), MakePropertiesTable(array_format));
		const sgre::resource::SfntFont font(fixture.Bytes);
		const sgre::resource::OpenTypeLayoutFont layout(font);
		Require(layout.HasGlyphClasses(), "present glyph-class offset must remain observable");
		Require(layout.GlyphProperties(0) == 0 && layout.GlyphProperties(1) == 2 && layout.GlyphProperties(2) == 4, "native unclassified/base/ligature property mapping changed");
		Require(layout.GlyphProperties(3) == 0x3408 && layout.GlyphProperties(4) == 0 && layout.GlyphProperties(5) == 0xff08, "native mark packing must keep the low attachment byte and component class must map to zero");
		Require(layout.MarkAttachmentClass(0) == 7 && layout.MarkAttachmentClass(3) == 0x1234 && layout.MarkAttachmentClass(6) == 0x100, "attachment classes remain full serialized words independently of packed mark properties");
	}
}

static void TestMissingTablesAndAttachmentDefinition() {
	const auto absent = MakeFont({}, {});
	const sgre::resource::SfntFont absent_font(absent.Bytes);
	const sgre::resource::OpenTypeLayoutFont absent_layout(absent_font);
	Require(!absent_layout.HasGlyphClasses() && absent_layout.GlyphProperties(1) == 0 && absent_layout.MarkAttachmentClass(1) == 0, "absent GDEF must not synthesize glyph classes");
	const auto fixture = MakeFont({}, MakeDefinitionTable(true));
	const sgre::resource::SfntFont font(fixture.Bytes);
	const sgre::resource::OpenTypeLayoutFont layout(font, sgre::resource::OpenTypeLayoutKind::Positioning);
	Require(layout.HasGlyphClasses() && layout.GlyphProperties(1) == 2 && layout.GlyphProperties(5) == 8 && layout.MarkAttachmentClass(5) == 0, "GDEF properties are shared metadata even without GSUB/GPOS or attachment classes");
}

static void TestGlyphAndMalformedAttachmentBounds() {
	const auto fixture = MakeFont({}, MakePropertiesTable(true));
	const sgre::resource::SfntFont font(fixture.Bytes);
	const sgre::resource::OpenTypeLayoutFont layout(font);
	RequireFailure([&] {
		(void)layout.MarkAttachmentClass(8);
	},
	    "index");
	RequireFailure([&] {
		(void)layout.GlyphProperties(8);
	},
	    "index");
	for (std::size_t offset : {std::size_t(0), std::size_t(2), std::size_t(4)}) {
		auto definition = MakePropertiesTable(true);
		const auto attachment = std::size_t((std::uint16_t(definition[10]) << 8) | definition[11]);
		WriteWord(definition, attachment + offset, offset == 0 ? 3 : offset == 2 ? 9
		                                                                         : 9);
		const auto invalid = MakeFont({}, definition);
		const sgre::resource::SfntFont invalid_font(invalid.Bytes);
		RequireFailure([&] {
			const sgre::resource::OpenTypeLayoutFont invalid_layout(invalid_font);
		},
		    "class");
	}
}

static void WriteFontProperties(bool include_attachment = false) {
	std::vector<std::uint8_t> bytes;
	char value = 0;
	while (std::cin.get(value)) {
		if (bytes.size() >= 32U * 1024U * 1024U) {
			throw std::runtime_error("glyph-property local font exceeds bounded stdin");
		}

		bytes.push_back(static_cast<std::uint8_t>(value));
	}

	const sgre::resource::SfntFont font(bytes);
	const sgre::resource::OpenTypeLayoutFont layout(font);
	const std::array<char, 4> present{layout.HasGlyphClasses() ? char(1) : char(0), 0, 0, 0};
	std::cout.write(present.data(), 4);
	for (std::uint32_t glyph = 0; glyph < font.GlyphCount(); ++glyph) {
		if (include_attachment) {
			const auto attachment = layout.MarkAttachmentClass(glyph);
			const std::array<std::uint8_t, 2> attachment_bytes{static_cast<std::uint8_t>(attachment), static_cast<std::uint8_t>(attachment >> 8)};
			std::cout.write(reinterpret_cast<const char*>(attachment_bytes.data()), 2);
		}

		const auto properties = layout.GlyphProperties(glyph);
		const std::array<std::uint8_t, 2> result{static_cast<std::uint8_t>(properties), static_cast<std::uint8_t>(properties >> 8)};
		std::cout.write(reinterpret_cast<const char*>(result.data()), 2);
	}
}

int main(int argc, char** argv) {
	try {
		if (argc == 2 && (std::string_view(argv[1]) == "--font" || std::string_view(argv[1]) == "--classes")) {
			WriteFontProperties(std::string_view(argv[1]) == "--classes");

			return 0;
		}

		TestBothClassFormatsAndPackedWidth();
		TestMissingTablesAndAttachmentDefinition();
		TestGlyphAndMalformedAttachmentBounds();
		std::cout << "OpenType GDEF glyph properties: three groups passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
