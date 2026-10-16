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

static std::vector<std::uint8_t> MakePositioningTable() {
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

	// A single-position lookup and an opaque extension lookup preserve distinct header identities.
	auto lookups = Words({2, 6, 28});
	Append(lookups, Words({1, 0, 1, 8, 1, 6, 0, 1, 2, 1, 3}));
	Append(lookups, Words({9, 0, 1, 8, 1, 2, 0, 8, 1, 0}));
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

static LayoutFixture MakeFont(const std::vector<std::uint8_t>& positioning, const std::vector<std::uint8_t>& definition, std::uint16_t glyph_count = 8) {
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
	if (!positioning.empty()) {
		tables["GPOS"] = positioning;
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

static void TestActualPositioningMetadata() {
	const auto fixture = MakeFont(MakePositioningTable(), MakeDefinitionTable(false));
	sgre::resource::SfntFont font(fixture.Bytes);
	sgre::resource::OpenTypeLayoutFont positioning(font, sgre::resource::OpenTypeLayoutKind::Positioning);
	Require(positioning.Kind() == sgre::resource::OpenTypeLayoutKind::Positioning && positioning.FeatureVariationsOffset() == 0 && positioning.Scripts().size() == 2 && positioning.Features().size() == 2 && positioning.Lookups().size() == 2, "GPOS must read its own actual metadata without aliasing GSUB");
	Require(positioning.Lookups()[0].Type == 1 && positioning.Lookups()[1].Type == 9 && positioning.Lookups()[1].Flags == 0 && positioning.Lookups()[1].SubtableOffsets.size() == 1, "GPOS supports metadata for its distinct extension lookup type nine");
	const std::array<std::string_view, 2> requested{"xxxx", "hani"};
	const auto script = positioning.SelectScript(requested);
	Require(script.Index == 1 && script.MatchedRequested && script.Tag == "hani", "original requested-script selection must work on the GPOS table");
	const auto language = positioning.SelectLanguage(*script.Index, "JAN ");
	Require(language.Index == 0 && language.MatchedRequested && positioning.FindFeature(*script.Index, language.Tag, "vrt2") == 1 && !positioning.FindFeature(*script.Index, language.Tag, "vert"), "GPOS language selection must keep its own required and ordered feature indices");
	Require(positioning.Scripts()[1].Languages[0].RequiredFeature == 1 && positioning.GlyphClass(5) == sgre::resource::OpenTypeGlyphClass::Mark, "required language feature and common GDEF glyph classes must survive table-kind selection");
}

static void TestTableIdentityAndUnsupportedApplication() {
	const auto fixture = MakeFont(MakePositioningTable(), MakeDefinitionTable(true));
	sgre::resource::SfntFont font(fixture.Bytes);
	sgre::resource::OpenTypeLayoutFont default_substitution(font);
	sgre::resource::OpenTypeLayoutFont positioning(font, sgre::resource::OpenTypeLayoutKind::Positioning);
	Require(default_substitution.Kind() == sgre::resource::OpenTypeLayoutKind::Substitution && default_substitution.Scripts().empty() && default_substitution.Lookups().empty() && !positioning.Lookups().empty(), "missing GSUB remains absent even when GPOS is present");
	RequireFailure([&] {
		(void)positioning.SubstitutionLookups();
	},
	    "no substitution lookup catalog");
	RequireFailure([&] {
		(void)positioning.ApplySingleSubstitution(0, 1);
	},
	    "cannot apply a substitution");
	RequireFailure([&] {
		sgre::resource::OpenTypeLayoutFont invalid(font, static_cast<sgre::resource::OpenTypeLayoutKind>(9));
	},
	    "table kind");
}

static void TestVersionAndVariationMetadata() {
	auto table = MakePositioningTable();
	table.insert(table.begin() + 10, 4, 0);
	WriteDoubleWord(table, 0, 0x00010001);
	for (const auto offset : {4, 6, 8}) {
		const auto old = std::uint16_t((std::uint16_t(table[static_cast<std::size_t>(offset)]) << 8) | table[static_cast<std::size_t>(offset + 1)]);
		WriteWord(table, static_cast<std::size_t>(offset), static_cast<std::uint16_t>(old + 4));
	}

	const auto variation_offset = static_cast<std::uint32_t>(table.size());
	WriteDoubleWord(table, 10, variation_offset);
	Append(table, Words({1, 0, 0, 0}));
	const auto fixture = MakeFont(table, {});
	sgre::resource::SfntFont font(fixture.Bytes);
	sgre::resource::OpenTypeLayoutFont positioning(font, sgre::resource::OpenTypeLayoutKind::Positioning);
	Require(positioning.FeatureVariationsOffset() == variation_offset && positioning.Features().size() == 2, "GPOS 1.1 must expose its actual feature-variation offset without applying it");
	WriteDoubleWord(table, 10, 0xffffffff);
	const auto invalid_fixture = MakeFont(table, {});
	sgre::resource::SfntFont invalid_font(invalid_fixture.Bytes);
	RequireFailure([&] {
		sgre::resource::OpenTypeLayoutFont invalid(invalid_font, sgre::resource::OpenTypeLayoutKind::Positioning);
	},
	    "range exceeds");
}

static void TestMalformedPositioningMetadata() {
	auto table = MakePositioningTable();
	WriteWord(table, 8, 0xffff);
	const auto fixture = MakeFont(table, {});
	sgre::resource::SfntFont font(fixture.Bytes);
	RequireFailure([&] {
		sgre::resource::OpenTypeLayoutFont invalid(font, sgre::resource::OpenTypeLayoutKind::Positioning);
	},
	    "outside its table");
	const auto absent_fixture = MakeFont({}, {});
	sgre::resource::SfntFont absent_font(absent_fixture.Bytes);
	sgre::resource::OpenTypeLayoutFont absent(absent_font, sgre::resource::OpenTypeLayoutKind::Positioning);
	Require(absent.Lookups().empty() && absent.Features().empty() && absent.Scripts().empty(), "genuinely absent GPOS remains explicitly empty");
}

int main() {
	try {
		TestActualPositioningMetadata();
		TestTableIdentityAndUnsupportedApplication();
		TestVersionAndVariationMetadata();
		TestMalformedPositioningMetadata();
		std::cout << "Four independent GPOS metadata and table-identity groups passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
