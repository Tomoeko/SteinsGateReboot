#include "sgre/text/shaping_font_map.hpp"

#include "sgre/resource/opentype_layout.hpp"
#include "vector_font_fixture.hpp"

#include <array>
#include <cstdint>
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

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view message) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(message) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("VectorFont feature-map fixture expected failure");
}

static std::vector<std::uint8_t> WordBytes(std::initializer_list<std::uint16_t> values) {
	std::vector<std::uint8_t> result;
	for (const auto value : values) {
		result.push_back(static_cast<std::uint8_t>(value >> 8));
		result.push_back(static_cast<std::uint8_t>(value));
	}

	return result;
}

static void WriteTag(std::vector<std::uint8_t>& bytes, std::size_t offset, std::string_view tag) {
	std::copy(tag.begin(), tag.end(), bytes.data() + offset);
}

static std::vector<std::uint8_t> MakeLayoutTable(std::string_view first_tag, std::uint16_t default_feature, std::uint16_t english_feature, std::string_view explicit_language = "ENG ") {
	std::vector<std::uint8_t> script(10);
	vector_font_test::WriteWord(script, 0, 10);
	vector_font_test::WriteWord(script, 2, 1);
	WriteTag(script, 4, explicit_language);
	vector_font_test::WriteWord(script, 8, 18);
	vector_font_test::Append(script, WordBytes({0, 0xffff, 1, default_feature}));
	vector_font_test::Append(script, WordBytes({0, 0xffff, 1, english_feature}));
	std::vector<std::uint8_t> scripts(8);
	vector_font_test::WriteWord(scripts, 0, 1);
	WriteTag(scripts, 2, "DFLT");
	vector_font_test::WriteWord(scripts, 6, 8);
	vector_font_test::Append(scripts, script);
	std::vector<std::uint8_t> features(14);
	vector_font_test::WriteWord(features, 0, 2);
	WriteTag(features, 2, first_tag);
	vector_font_test::WriteWord(features, 6, 14);
	WriteTag(features, 8, "vert");
	vector_font_test::WriteWord(features, 12, 20);
	vector_font_test::Append(features, WordBytes({0, 1, 0, 0, 1, 0}));
	const auto lookups = WordBytes({1, 4, 2, 0, 1, 8, 1});
	std::vector<std::uint8_t> table(10);
	vector_font_test::WriteDoubleWord(table, 0, 0x10000);
	vector_font_test::WriteWord(table, 4, static_cast<std::uint16_t>(table.size()));
	vector_font_test::Append(table, scripts);
	vector_font_test::WriteWord(table, 6, static_cast<std::uint16_t>(table.size()));
	vector_font_test::Append(table, features);
	vector_font_test::WriteWord(table, 8, static_cast<std::uint16_t>(table.size()));
	vector_font_test::Append(table, lookups);

	return table;
}

static std::vector<std::uint8_t> MakeFontWithLayouts(bool feature_variations = false, std::string_view explicit_language = "ENG ") {
	const auto base = vector_font_test::MakeFont();
	sgre::resource::SfntFont font(base);
	std::map<std::string, std::vector<std::uint8_t>> tables;
	for (const auto& table : font.Tables()) {
		const auto bytes = *font.FindTable(table.Tag);
		tables[table.Tag] = std::vector<std::uint8_t>(bytes.begin(), bytes.end());
	}

	tables["GSUB"] = MakeLayoutTable("liga", 1, 0, explicit_language);
	tables["GPOS"] = MakeLayoutTable("kern", 0, 1, explicit_language);
	if (feature_variations) {
		auto& positioning = tables["GPOS"];
		positioning.insert(positioning.begin() + 10, 4, 0);
		vector_font_test::WriteDoubleWord(positioning, 0, 0x10001);
		for (std::size_t offset = 4; offset <= 8; offset += 2) {
			const auto original = std::uint16_t((std::uint16_t(positioning[offset]) << 8) | positioning[offset + 1]);
			vector_font_test::WriteWord(positioning, offset, static_cast<std::uint16_t>(original + 4));
		}

		vector_font_test::WriteDoubleWord(positioning, 10, static_cast<std::uint32_t>(positioning.size()));
		vector_font_test::Append(positioning, WordBytes({1, 0, 0, 0}));
	}
	std::vector<std::uint8_t> result(12 + tables.size() * 16);
	vector_font_test::WriteDoubleWord(result, 0, 0x4f54544f);
	vector_font_test::WriteWord(result, 4, static_cast<std::uint16_t>(tables.size()));
	std::size_t index = 0;
	for (const auto& [tag, bytes] : tables) {
		while (result.size() % 4 != 0) {
			result.push_back(0);
		}

		const auto record = 12 + index++ * 16;
		WriteTag(result, record, tag);
		vector_font_test::WriteDoubleWord(result, record + 8, static_cast<std::uint32_t>(result.size()));
		vector_font_test::WriteDoubleWord(result, record + 12, static_cast<std::uint32_t>(bytes.size()));
		vector_font_test::Append(result, bytes);
	}

	return result;
}

static void TestMissingTablesAndNeutralProfiles() {
	const auto bytes = vector_font_test::MakeFont();
	sgre::resource::SfntFont font(bytes);
	for (const auto script : {sgre::text::VectorFontScript::Latin, sgre::text::VectorFontScript::Cyrillic, sgre::text::VectorFontScript::Han}) {
		for (const auto direction : {sgre::text::VectorFontDirection::LeftToRight, sgre::text::VectorFontDirection::RightToLeft, sgre::text::VectorFontDirection::TopToBottom}) {
			sgre::text::VectorFontConfiguration configuration;
			configuration.Script = script;
			configuration.Direction = direction;
			const auto map = sgre::text::PrepareVectorFontFeatureMap(font, configuration);
			Require(map.GlobalMask == 2 && map.Features.size() == (direction == sgre::text::VectorFontDirection::TopToBottom ? 0 : 1), "missing actual layout tables must retain only the original horizontal kern fallback");
		}
	}
}

static void TestGenuineLayoutAndLanguageSelections() {
	const auto bytes = MakeFontWithLayouts();
	sgre::resource::SfntFont font(bytes);
	sgre::text::VectorFontConfiguration configuration;
	configuration.Script = sgre::text::VectorFontScript::Han;
	configuration.Direction = sgre::text::VectorFontDirection::TopToBottom;
	const auto english = sgre::text::PrepareVectorFontFeatureMap(font, configuration);
	Require(english.Features.size() == 1 && english.Features[0].Tag == "vert" && !english.Features[0].SubstitutionFeature && english.Features[0].PositioningFeature == 1, "actual selected English GPOS vert must not acquire an unselected GSUB index merely because table search is allowed");
	configuration.HasLanguage = false;
	const auto default_language = sgre::text::PrepareVectorFontFeatureMap(font, configuration);
	Require(default_language.Features.size() == 1 && default_language.Features[0].SubstitutionFeature == 1 && !default_language.Features[0].PositioningFeature, "absent language must select each actual default language system independently");
	configuration.HasLanguage = true;
	configuration.Direction = sgre::text::VectorFontDirection::LeftToRight;
	const auto horizontal = sgre::text::PrepareVectorFontFeatureMap(font, configuration);
	Require(horizontal.Features.size() == 2 && horizontal.Features[0].Tag == "kern" && horizontal.Features[0].LookupFlags == 7 && horizontal.Features[1].Tag == "liga" && horizontal.Features[1].SubstitutionFeature == 0, "horizontal profile must combine genuine selected GSUB liga with an actual missing-language kern fallback");
}

static void TestActualJapaneseLanguageSystem() {
	const auto bytes = MakeFontWithLayouts(false, "JAN ");
	const sgre::resource::SfntFont font(bytes);
	auto configuration = sgre::text::ResolveVectorFontConfiguration(0, "JA");
	configuration.Script = sgre::text::VectorFontScript::Han;
	configuration.Direction = sgre::text::VectorFontDirection::TopToBottom;
	Require(configuration.Language == "ja" && sgre::text::ResolveVectorFontLayoutLanguageTag(configuration) == "JAN ", "canonical Japanese must reach the native JAN space-padded layout tag");
	const auto japanese = sgre::text::PrepareVectorFontFeatureMap(font, configuration);
	Require(japanese.Features.size() == 1 && !japanese.Features[0].SubstitutionFeature && japanese.Features[0].PositioningFeature == 1, "Japanese must select its actual per-table JAN system rather than default or English metadata");
	const sgre::resource::OpenTypeLayoutFont substitutions(font);
	const sgre::resource::OpenTypeLayoutFont positioning(font, sgre::resource::OpenTypeLayoutKind::Positioning);
	const auto readers = sgre::text::PrepareVectorFontFeatureMap(substitutions, positioning, configuration, {});
	Require(readers.Features[0].SubstitutionFeature == japanese.Features[0].SubstitutionFeature && readers.Features[0].PositioningFeature == japanese.Features[0].PositioningFeature, "the actual admitted-reader path must make the same Japanese selection");
	configuration.Language = "en";
	const auto english = sgre::text::PrepareVectorFontFeatureMap(font, configuration);
	Require(english.Features[0].SubstitutionFeature == 1 && !english.Features[0].PositioningFeature, "English without an ENG system retains the actual default fallback");
	configuration.HasLanguage = false;
	Require(sgre::text::ResolveVectorFontLayoutLanguageTag(configuration) == "dflt", "absent language retains the exact lowercase native tag independently of retained spelling");
}

static void TestOwnedResultsAndUserOverrides() {
	sgre::text::VectorFontFeatureMap result;
	{
		const auto bytes = MakeFontWithLayouts();
		sgre::resource::SfntFont font(bytes);
		sgre::text::VectorFontConfiguration configuration;
		std::array<sgre::text::VectorFontFeatureRequest, 1> requests;
		requests[0].Tag = "kern";
		requests[0].Value = 0;
		result = sgre::text::PrepareVectorFontFeatureMap(font, configuration, requests);
	}

	Require(result.Features.size() == 1 && result.Features[0].Tag == "liga" && result.Features[0].SubstitutionFeature == 0, "user zero must disable original kern while returned tags and indices survive font/reader/request destruction");
}

static void TestUnsupportedProfiles() {
	const auto bytes = vector_font_test::MakeFont();
	sgre::resource::SfntFont font(bytes);
	sgre::text::VectorFontConfiguration configuration;
	configuration.Script = sgre::text::VectorFontScript::Arabic;
	RequireFailure([&] {
		(void)sgre::text::PrepareVectorFontFeatureMap(font, configuration);
	},
	    "neutral script");
	configuration.Script = sgre::text::VectorFontScript::Latin;
	configuration.Language = "fr";
	RequireFailure([&] {
		(void)sgre::text::PrepareVectorFontFeatureMap(font, configuration);
	},
	    "canonical en/ja profile");
	configuration.HasLanguage = false;
	Require(sgre::text::PrepareVectorFontFeatureMap(font, configuration).Features.size() == 1, "absent language ignores a retained spelling and selects the actual default system");
	const auto variation_bytes = MakeFontWithLayouts(true);
	sgre::resource::SfntFont variation_font(variation_bytes);
	RequireFailure([&] {
		(void)sgre::text::PrepareVectorFontFeatureMap(variation_font, configuration);
	},
	    "feature variations");
}

static void WriteWord(std::uint32_t value) {
	std::array<std::uint8_t, 4> bytes{};
	for (std::size_t index = 0; index < bytes.size(); ++index) {
		bytes[index] = static_cast<std::uint8_t>(value >> (index * 8));
	}

	std::cout.write(reinterpret_cast<const char*>(bytes.data()), 4);
}

static void WriteMap(const sgre::text::VectorFontFeatureMap& map) {
	WriteWord(map.GlobalMask);
	WriteWord(map.RequiredSubstitutionStage);
	WriteWord(map.RequiredPositioningStage);
	WriteWord(static_cast<std::uint32_t>(map.Features.size()));
	for (const auto& feature : map.Features) {
		std::uint32_t tag = 0;
		for (const auto character : feature.Tag) {
			tag = tag << 8 | static_cast<std::uint8_t>(character);
		}

		WriteWord(tag);
		WriteWord(feature.SubstitutionFeature ? *feature.SubstitutionFeature : 0xffff);
		WriteWord(feature.PositioningFeature ? *feature.PositioningFeature : 0xffff);
		WriteWord(feature.SubstitutionStage);
		WriteWord(feature.PositioningStage);
		WriteWord(feature.Shift);
		WriteWord(feature.Mask);
		WriteWord(feature.OneMask);
		WriteWord(feature.LookupFlags);
	}
}

static void CompareOriginalFontMaps() {
	std::vector<std::uint8_t> bytes;
	std::array<char, 4096> buffer{};
	while (std::cin) {
		std::cin.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
		const auto count = static_cast<std::size_t>(std::cin.gcount());
		if (count > 64 * 1024 * 1024 - bytes.size()) {
			throw std::runtime_error("font-map corpus input exceeds its limit");
		}

		for (std::size_t index = 0; index < count; ++index) {
			bytes.push_back(static_cast<std::uint8_t>(buffer[index]));
		}
	}

	sgre::resource::SfntFont font(bytes);
	for (std::uint32_t profile = 0; profile < 4; ++profile) {
		sgre::text::VectorFontConfiguration configuration;
		configuration.Script = profile == 0 ? sgre::text::VectorFontScript::Latin : profile == 1 ? sgre::text::VectorFontScript::Cyrillic
		                                                                                         : sgre::text::VectorFontScript::Han;
		configuration.Direction = profile == 3 ? sgre::text::VectorFontDirection::TopToBottom : sgre::text::VectorFontDirection::LeftToRight;
		for (const auto has_language : {true, false}) {
			configuration.HasLanguage = has_language;
			WriteMap(sgre::text::PrepareVectorFontFeatureMap(font, configuration));
			std::array<sgre::text::VectorFontFeatureRequest, 3> requests;
			requests[0].Tag = "kern";
			requests[0].Value = 0;
			requests[1].Tag = "vert";
			requests[1].Value = 2;
			requests[1].StartCluster = 3;
			requests[1].EndCluster = 10;
			requests[2].Tag = "liga";
			requests[2].Value = 2;
			WriteMap(sgre::text::PrepareVectorFontFeatureMap(font, configuration, requests));
		}
	}
}

int main(int argc, char** argv) {
	try {
		if (argc == 2 && std::string_view(argv[1]) == "--font") {
			CompareOriginalFontMaps();

			return std::cout ? 0 : 1;
		}
		Require(argc == 1, "unknown font-map fixture argument");
		TestMissingTablesAndNeutralProfiles();
		TestGenuineLayoutAndLanguageSelections();
		TestActualJapaneseLanguageSystem();
		TestOwnedResultsAndUserOverrides();
		TestUnsupportedProfiles();
		std::cout << "Four independent actual-font feature-map preparation groups passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
