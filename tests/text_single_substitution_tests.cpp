#include "sgre/text/single_substitution.hpp"

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

static std::vector<sgre::text::SubstitutionGlyph> MakeGlyphs() {
	std::vector<sgre::text::SubstitutionGlyph> result;
	for (std::uint32_t index = 0; index < 8; ++index) {
		sgre::text::SubstitutionGlyph glyph;
		glyph.GlyphIndex = index;
		glyph.Mask = index == 3 ? 0 : 2;
		glyph.Cluster = index * 17;
		glyph.GlyphProperties = 0xf3f3;
		glyph.PrivateState = 0xa5f1;
		glyph.CharacterState = 0xcafe0000 | index;
		result.push_back(glyph);
	}

	return result;
}

static void TestMaskedRecordsAndGdefUpdate() {
	const auto fixture = MakeFont(MakeSubstitutionTable(), MakeDefinitionTable(true));
	const sgre::resource::SfntFont font(fixture.Bytes);
	const sgre::resource::OpenTypeLayoutFont layout(font);
	auto glyphs = MakeGlyphs();
	const auto original = glyphs;
	sgre::text::VectorFontScheduledLookup lookup;
	lookup.Index = 0;
	lookup.Mask = 2;
	Require(sgre::text::ApplyDirectSingleSubstitution(layout, lookup, glyphs), "covered format-one entry must report an application");
	Require(glyphs[1].GlyphIndex == 2 && glyphs[1].GlyphProperties == 0x72 && glyphs[3] == original[3], "mask filtering and replacement GDEF properties differ");
	for (std::size_t index = 0; index < glyphs.size(); ++index) {
		Require(glyphs[index].Mask == original[index].Mask && glyphs[index].Cluster == original[index].Cluster && glyphs[index].PrivateState == original[index].PrivateState && glyphs[index].CharacterState == original[index].CharacterState, "single substitution altered record state owned by another phase");
	}

	lookup.Index = 1;
	Require(sgre::text::ApplyDirectSingleSubstitution(layout, lookup, glyphs), "covered format-two entry must report an application");
	Require(glyphs[1].GlyphIndex == 5 && glyphs[1].GlyphProperties == 0x78 && glyphs[4].GlyphIndex == 6 && glyphs[4].GlyphProperties == 0x70, "replacement mark/unclassified properties differ");
}

static void TestAbsentGdefZeroMaskAndIdentityApplication() {
	auto substitution = MakeSubstitutionTable();
	WriteWord(substitution, 108, 0);
	const auto fixture = MakeFont(substitution, {});
	const sgre::resource::SfntFont font(fixture.Bytes);
	const sgre::resource::OpenTypeLayoutFont layout(font);
	auto glyphs = MakeGlyphs();
	const auto original = glyphs;
	sgre::text::VectorFontScheduledLookup lookup;
	lookup.Index = 0;
	lookup.Mask = 0;
	Require(!sgre::text::ApplyDirectSingleSubstitution(layout, lookup, glyphs) && glyphs == original, "zero lookup mask must preserve every field");
	lookup.Mask = 2;
	Require(sgre::text::ApplyDirectSingleSubstitution(layout, lookup, glyphs) && glyphs == original, "native covered identity replacement is an application and absent GDEF must preserve existing properties");
}

static std::vector<std::uint8_t> WrapSingleLookup(std::vector<std::uint8_t> table, std::size_t index) {
	const auto read = [&](std::size_t offset) {
		return std::uint16_t((std::uint16_t(table.at(offset)) << 8) | table.at(offset + 1));
	};
	const auto list = read(8);
	const auto lookup = std::size_t(list) + read(list + 2 + index * 2);
	const auto target = lookup + read(lookup + 6);
	Require(read(lookup + 4) == 1, "extension fixture wraps one real subtable");
	table.insert(table.begin() + static_cast<std::ptrdiff_t>(target), 8, 0);
	WriteWord(table, lookup, 7);
	WriteWord(table, target, 1);
	WriteWord(table, target + 2, 1);
	WriteDoubleWord(table, target + 4, 8);
	for (std::size_t following = index + 1; following < read(list); ++following) {
		WriteWord(table, list + 2 + following * 2, static_cast<std::uint16_t>(read(list + 2 + following * 2) + 8));
	}

	return table;
}

static void TestExtensionSingleUsesActualRecordOperation() {
	const auto direct_fixture = MakeFont(MakeSubstitutionTable(), MakeDefinitionTable(true));
	const auto extension_table = WrapSingleLookup(WrapSingleLookup(MakeSubstitutionTable(), 1), 0);
	const auto fixture = MakeFont(extension_table, MakeDefinitionTable(true));
	const sgre::resource::SfntFont direct_font(direct_fixture.Bytes);
	const sgre::resource::OpenTypeLayoutFont direct(direct_font);
	const sgre::resource::SfntFont font(fixture.Bytes);
	const sgre::resource::OpenTypeLayoutFont extension(font);
	auto expected = MakeGlyphs();
	auto actual = expected;
	for (std::uint16_t index = 0; index < 2; ++index) {
		sgre::text::VectorFontScheduledLookup lookup;
		lookup.Index = index;
		lookup.Mask = 2;
		Require(extension.Lookups()[index].Type == 7 && extension.CanApplySingleSubstitution(index), "actual format1 extension must resolve only to its real single payload");
		Require(sgre::text::ApplyDirectSingleSubstitution(direct, lookup, expected) == sgre::text::ApplyDirectSingleSubstitution(extension, lookup, actual) && actual == expected, "extension single formats1/2 must preserve exact masks, clusters, GDEF and opaque record words");
	}
	auto foreign_target = extension_table;
	const auto list = std::uint16_t((std::uint16_t(foreign_target[8]) << 8) | foreign_target[9]);
	const auto first = std::size_t(list) + 6;
	WriteWord(foreign_target, first + 10, 4);
	const auto foreign_fixture = MakeFont(foreign_target, {});
	const sgre::resource::SfntFont foreign_font(foreign_fixture.Bytes);
	const sgre::resource::OpenTypeLayoutFont foreign(foreign_font);
	Require(!foreign.CanApplySingleSubstitution(0), "extension to a non-single operation must remain unsupported");
	sgre::text::VectorFontScheduledLookup lookup;
	lookup.Mask = 2;
	RequireFailure([&] {
		static_cast<void>(sgre::text::ApplyDirectSingleSubstitution(foreign, lookup, actual));
	},
	    "flags-zero");
	actual.back().GlyphIndex = 8;
	const auto malformed = actual;
	RequireFailure([&] {
		static_cast<void>(sgre::text::ApplyDirectSingleSubstitution(extension, lookup, actual));
	},
	    "index");
	Require(actual == malformed, "extension preflight must not publish an earlier record before a later invalid glyph");
}

static void TestUnsupportedAndInputPreflight() {
	const auto fixture = MakeFont(MakeSubstitutionTable(), MakeDefinitionTable(false));
	const sgre::resource::SfntFont font(fixture.Bytes);
	const sgre::resource::OpenTypeLayoutFont layout(font);
	auto glyphs = MakeGlyphs();
	glyphs.back().GlyphIndex = 8;
	const auto invalid = glyphs;
	sgre::text::VectorFontScheduledLookup lookup;
	lookup.Mask = 2;
	RequireFailure([&] {
		(void)sgre::text::ApplyDirectSingleSubstitution(layout, lookup, glyphs);
	},
	    "index");
	Require(glyphs == invalid, "malformed later input must not publish an earlier replacement");
	lookup.Index = 2;
	RequireFailure([&] {
		(void)sgre::text::ApplyDirectSingleSubstitution(layout, lookup, glyphs);
	},
	    "lookup index");
	const sgre::resource::OpenTypeLayoutFont positioning(font, sgre::resource::OpenTypeLayoutKind::Positioning);
	RequireFailure([&] {
		(void)sgre::text::ApplyDirectSingleSubstitution(positioning, lookup, glyphs);
	},
	    "GSUB");
	auto filtered = MakeSubstitutionTable();
	WriteWord(filtered, 98, 2);
	const auto filtered_fixture = MakeFont(filtered, {});
	const sgre::resource::SfntFont filtered_font(filtered_fixture.Bytes);
	const sgre::resource::OpenTypeLayoutFont filtered_layout(filtered_font);
	lookup.Index = 0;
	RequireFailure([&] {
		(void)sgre::text::ApplyDirectSingleSubstitution(filtered_layout, lookup, {});
	},
	    "flags-zero");
	std::vector<sgre::text::SubstitutionGlyph> oversized(65537);
	RequireFailure([&] {
		(void)sgre::text::ApplyDirectSingleSubstitution(layout, lookup, oversized);
	},
	    "count limit");
}

static std::uint32_t ReadProtocolWord(std::span<const std::uint8_t> bytes, std::size_t offset) {
	if (offset > bytes.size() || bytes.size() - offset < 4) {
		throw std::runtime_error("single-substitution protocol range exceeds input");
	}

	return std::uint32_t(bytes[offset]) | (std::uint32_t(bytes[offset + 1]) << 8) | (std::uint32_t(bytes[offset + 2]) << 16) | (std::uint32_t(bytes[offset + 3]) << 24);
}

static void WriteProtocolWord(std::uint32_t word) {
	const std::array<std::uint8_t, 4> bytes{static_cast<std::uint8_t>(word), static_cast<std::uint8_t>(word >> 8), static_cast<std::uint8_t>(word >> 16), static_cast<std::uint8_t>(word >> 24)};
	std::cout.write(reinterpret_cast<const char*>(bytes.data()), 4);
}

static void RunNativeProtocol() {
	std::vector<std::uint8_t> bytes;
	char value = 0;
	while (std::cin.get(value)) {
		if (bytes.size() >= 32U * 1024U * 1024U) {
			throw std::runtime_error("single-substitution protocol exceeds bounded stdin");
		}

		bytes.push_back(static_cast<std::uint8_t>(value));
	}

	const auto font_size = ReadProtocolWord(bytes, 0);
	if (font_size > bytes.size() - 4) {
		throw std::runtime_error("single-substitution protocol font exceeds input");
	}
	const sgre::resource::SfntFont font(std::span(bytes).subspan(4, font_size));
	const sgre::resource::OpenTypeLayoutFont layout(font);
	const auto header = std::size_t(font_size) + 4;
	const auto count = ReadProtocolWord(bytes, header + 8);
	if (count > 65536 || header + 12 > bytes.size() || bytes.size() - header - 12 != std::size_t(count) * 20) {
		throw std::runtime_error("single-substitution protocol record count is invalid");
	}

	sgre::text::VectorFontScheduledLookup lookup;
	const auto lookup_word = ReadProtocolWord(bytes, header);
	lookup.Index = static_cast<std::uint16_t>(lookup_word);
	lookup.Options = static_cast<std::uint16_t>(lookup_word >> 16);
	lookup.Mask = ReadProtocolWord(bytes, header + 4);
	std::vector<sgre::text::SubstitutionGlyph> glyphs(count);
	for (std::size_t index = 0; index < glyphs.size(); ++index) {
		const auto offset = header + 12 + index * 20;
		auto& glyph = glyphs[index];
		glyph.GlyphIndex = ReadProtocolWord(bytes, offset);
		glyph.Mask = ReadProtocolWord(bytes, offset + 4);
		glyph.Cluster = ReadProtocolWord(bytes, offset + 8);
		const auto properties = ReadProtocolWord(bytes, offset + 12);
		glyph.GlyphProperties = static_cast<std::uint16_t>(properties);
		glyph.PrivateState = static_cast<std::uint16_t>(properties >> 16);
		glyph.CharacterState = ReadProtocolWord(bytes, offset + 16);
	}

	WriteProtocolWord(sgre::text::ApplyDirectSingleSubstitution(layout, lookup, glyphs) ? 1 : 0);
	for (const auto& glyph : glyphs) {
		WriteProtocolWord(glyph.GlyphIndex);
		WriteProtocolWord(glyph.Mask);
		WriteProtocolWord(glyph.Cluster);
		WriteProtocolWord(std::uint32_t(glyph.GlyphProperties) | (std::uint32_t(glyph.PrivateState) << 16));
		WriteProtocolWord(glyph.CharacterState);
	}
}

int main(int argc, char** argv) {
	try {
		if (argc == 2 && std::string_view(argv[1]) == "--native") {
			RunNativeProtocol();

			return 0;
		}

		TestMaskedRecordsAndGdefUpdate();
		TestAbsentGdefZeroMaskAndIdentityApplication();
		TestExtensionSingleUsesActualRecordOperation();
		TestUnsupportedAndInputPreflight();
		std::cout << "Direct single substitution: three groups passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
