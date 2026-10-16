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

static void TestMetadataAndExactFeatureSelection() {
	const auto fixture = MakeFont(MakeSubstitutionTable(), MakeDefinitionTable(false));
	const sgre::resource::SfntFont font(fixture.Bytes);
	const sgre::resource::OpenTypeLayoutFont layout(font);
	Require(layout.Scripts().size() == 2 && layout.Features().size() == 2 && layout.SubstitutionLookups().size() == 2, "OpenType layout lists lost their table counts");
	Require(layout.FindScript("DFLT") == 0 && layout.FindScript("hani") == 1 && !layout.FindScript("Hani"), "OpenType script tags were normalized or reordered");
	Require(layout.FindFeature(0, "", "vert") == 0 && layout.FindFeature(0, "", "vrt2") == 1, "default language feature indices changed");
	Require(layout.FindFeature(1, "", "vert") == 0 && !layout.FindFeature(1, "", "vrt2"), "script default feature set broadened");
	Require(layout.FindFeature(1, "JAN ", "vrt2") == 1 && !layout.FindFeature(1, "ENG ", "vert"), "exact language selection silently used another language");
	Require(layout.Scripts()[1].Languages[0].RequiredFeature == 1, "required language feature was lost");
	Require(layout.Features()[0].Lookups == std::vector<std::uint16_t>{0} && layout.Features()[1].Lookups == std::vector<std::uint16_t>{1}, "feature lookup order changed");
	Require(layout.ApplySingleSubstitution(0, 1) == 2 && layout.ApplySingleSubstitution(0, 3) == 4 && !layout.ApplySingleSubstitution(0, 2), "format 1 single substitution changed");
	Require(layout.ApplySingleSubstitution(1, 2) == 5 && layout.ApplySingleSubstitution(1, 4) == 6 && !layout.ApplySingleSubstitution(1, 1), "format 2 coverage-indexed substitution changed");
}

static void TestNativeSelectionFallbacks() {
	const std::array<std::string_view, 2> requested = {"xxxx", "hani"};
	const std::array<std::string_view, 1> missing = {"xxxx"};
	const auto fixture = MakeFont(MakeSubstitutionTable(), {});
	const sgre::resource::SfntFont font(fixture.Bytes);
	const sgre::resource::OpenTypeLayoutFont layout(font);
	const auto script = layout.SelectScript(requested);
	Require(script.Index == 1 && script.Tag == "hani" && script.MatchedRequested, "native script candidate order changed");
	const auto fallback = layout.SelectScript(missing);
	Require(fallback.Index == 0 && fallback.Tag == "DFLT" && !fallback.MatchedRequested, "native DFLT fallback changed");
	const auto japanese = layout.SelectLanguage(1, "JAN ");
	Require(japanese.Index == 0 && japanese.Tag == "JAN " && japanese.MatchedRequested, "native requested language selection changed");
	const auto english = layout.SelectLanguage(1, "ENG ");
	Require(!english.Index && english.Tag.empty() && !english.MatchedRequested, "native default language selection changed");

	auto table = MakeSubstitutionTable();
	WriteTag(table, 12, "dflt");
	WriteTag(table, 42, "dflt");
	const auto lowercase_fixture = MakeFont(table, {});
	const sgre::resource::SfntFont lowercase_font(lowercase_fixture.Bytes);
	const sgre::resource::OpenTypeLayoutFont lowercase(lowercase_font);
	Require(lowercase.SelectScript(missing).Tag == "dflt", "native lowercase script fallback was skipped");
	const auto language_fallback = lowercase.SelectLanguage(1, "ENG ");
	Require(language_fallback.Index == 0 && language_fallback.Tag == "dflt" && !language_fallback.MatchedRequested, "native explicit lowercase language fallback was skipped");
	WriteTag(table, 12, "hani");
	WriteTag(table, 18, "latn");
	const auto latin_fixture = MakeFont(table, {});
	const sgre::resource::SfntFont latin_font(latin_fixture.Bytes);
	const sgre::resource::OpenTypeLayoutFont latin(latin_font);
	Require(latin.SelectScript(missing).Tag == "latn", "native Latin script fallback was skipped");
	WriteTag(table, 18, "knaa");
	const auto absent_fixture = MakeFont(table, {});
	const sgre::resource::SfntFont absent_font(absent_fixture.Bytes);
	const sgre::resource::OpenTypeLayoutFont absent(absent_font);
	Require(!absent.SelectScript(missing).Index && absent.SelectScript(missing).Tag.empty(), "missing native script selection was synthesized");
}

static void TestGlyphClassesAndMissingTables() {
	for (const auto array_format : {false, true}) {
		const auto fixture = MakeFont({}, MakeDefinitionTable(array_format));
		const sgre::resource::SfntFont font(fixture.Bytes);
		const sgre::resource::OpenTypeLayoutFont layout(font);
		Require(layout.GlyphClass(0) == sgre::resource::OpenTypeGlyphClass::Unclassified && layout.GlyphClass(1) == sgre::resource::OpenTypeGlyphClass::Base && layout.GlyphClass(4) == sgre::resource::OpenTypeGlyphClass::Base && layout.GlyphClass(5) == sgre::resource::OpenTypeGlyphClass::Mark && layout.GlyphClass(7) == sgre::resource::OpenTypeGlyphClass::Unclassified, "OpenType class range or absent-glyph behavior changed");
		Require(layout.Scripts().empty() && !layout.FindScript("DFLT"), "missing substitution table synthesized metadata");
	}
	const auto missing = MakeFont({}, {});
	const sgre::resource::SfntFont font(missing.Bytes);
	const sgre::resource::OpenTypeLayoutFont layout(font);
	Require(layout.GlyphClass(5) == sgre::resource::OpenTypeGlyphClass::Unclassified, "missing definition table synthesized a glyph class");
	RequireFailure([&] {
		static_cast<void>(layout.GlyphClass(8));
	},
	    "font");
}

static void TestUnsupportedLookupAndSignedDelta() {
	auto table = MakeSubstitutionTable();
	// Lookup-list offset 90, first lookup at 96, its single-substitution delta at 108.
	WriteWord(table, 108, 0xffff);
	const auto fixture = MakeFont(table, {});
	const sgre::resource::SfntFont font(fixture.Bytes);
	const sgre::resource::OpenTypeLayoutFont layout(font);
	Require(layout.ApplySingleSubstitution(0, 1) == 0 && layout.ApplySingleSubstitution(0, 3) == 2, "negative substitution delta lost modulo-65536 behavior");
	WriteWord(table, 98, 8);
	const auto filtered_fixture = MakeFont(table, {});
	const sgre::resource::SfntFont filtered_font(filtered_fixture.Bytes);
	const sgre::resource::OpenTypeLayoutFont filtered(filtered_font);
	RequireFailure([&] {
		static_cast<void>(filtered.ApplySingleSubstitution(0, 1));
	},
	    "unsupported");
}

static void TestMalformedMetadata() {
	const auto reject = [](std::vector<std::uint8_t> table, std::size_t offset, std::uint16_t value, std::string_view context) {
		WriteWord(table, offset, value);
		const auto fixture = MakeFont(table, {});
		RequireFailure([&] {
			const sgre::resource::SfntFont font(fixture.Bytes);
			const sgre::resource::OpenTypeLayoutFont layout(font);
			static_cast<void>(layout);
		},
		    context);
	};
	const auto table = MakeSubstitutionTable();
	reject(table, 4, 0, "offset");
	reject(table, 82, 99, "lookup index");
	reject(table, 34, 99, "language feature");
	reject(table, 114, 8, "coverage");
	reject(table, 130, 1, "replacement count");
	reject(table, 132, 8, "replacement glyph");
	auto definition = MakeDefinitionTable(false);
	WriteWord(definition, 18, 5);
	const auto fixture = MakeFont({}, definition);
	RequireFailure([&] {
		const sgre::resource::SfntFont font(fixture.Bytes);
		const sgre::resource::OpenTypeLayoutFont layout(font);
		static_cast<void>(layout);
	},
	    "class");
}

static void TestAliasedValidationWorkLimit() {
	std::vector<std::uint8_t> table(10);
	WriteDoubleWord(table, 0, 0x00010000);
	WriteWord(table, 4, 10);
	WriteWord(table, 6, 12);
	WriteWord(table, 8, 14);
	Append(table, Words({0, 0, 32}));
	for (std::size_t index = 0; index < 32; ++index) {
		Append(table, Words({66}));
	}
	Append(table, Words({1, 0, 1, 8, 1, 6, 0, 1, 65535}));
	for (std::uint32_t glyph = 0; glyph < 65535; ++glyph) {
		Append(table, Words({static_cast<std::uint16_t>(glyph)}));
	}
	const auto fixture = MakeFont(table, {}, 65535);
	RequireFailure([&] {
		const sgre::resource::SfntFont font(fixture.Bytes);
		const sgre::resource::OpenTypeLayoutFont layout(font);
		static_cast<void>(layout);
	},
	    "preparation limit");
}

int main() {
	try {
		TestMetadataAndExactFeatureSelection();
		TestNativeSelectionFallbacks();
		TestGlyphClassesAndMissingTables();
		TestUnsupportedLookupAndSignedDelta();
		TestMalformedMetadata();
		TestAliasedValidationWorkLimit();
		std::cout << "OpenType layout tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << "OpenType layout test failure: " << error.what() << '\n';

		return 1;
	}
}
