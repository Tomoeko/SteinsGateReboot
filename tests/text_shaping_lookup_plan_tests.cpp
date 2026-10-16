#include "sgre/text/shaping_lookup_plan.hpp"

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

	throw std::runtime_error("VectorFont lookup-plan fixture expected failure");
}

static void AppendWord(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
	bytes.push_back(static_cast<std::uint8_t>(value >> 8));
	bytes.push_back(static_cast<std::uint8_t>(value));
}

static void WriteTag(std::vector<std::uint8_t>& bytes, std::size_t offset, std::string_view tag) {
	std::copy(tag.begin(), tag.end(), bytes.data() + offset);
}

static std::vector<std::uint8_t> MakeLayout(std::uint16_t required_feature, bool japanese_required = false) {
	std::vector<std::string_view> tags{"ccmp", "liga", "rvrn", "vert"};
	std::vector<std::vector<std::uint16_t>> feature_lookups{{2, 1, 2}, {0, 2, 0}, {2}, {1}};
	if (japanese_required) {
		tags.push_back("zzzz");
		feature_lookups.push_back({0});
	}
	std::vector<std::uint8_t> scripts(8);
	vector_font_test::WriteWord(scripts, 0, 1);
	WriteTag(scripts, 2, "DFLT");
	vector_font_test::WriteWord(scripts, 6, 8);
	AppendWord(scripts, japanese_required ? 10 : 4);
	AppendWord(scripts, japanese_required ? 1 : 0);
	if (japanese_required) {
		scripts.insert(scripts.end(), {'J', 'A', 'N', ' '});
		AppendWord(scripts, 26);
	}
	AppendWord(scripts, 0);
	AppendWord(scripts, required_feature);
	AppendWord(scripts, static_cast<std::uint16_t>(tags.size()));
	for (std::uint16_t index = 0; index < tags.size(); ++index) {
		AppendWord(scripts, index);
	}
	if (japanese_required) {
		AppendWord(scripts, 0);
		AppendWord(scripts, 4);
		AppendWord(scripts, static_cast<std::uint16_t>(tags.size()));
		for (std::uint16_t index = 0; index < tags.size(); ++index) {
			AppendWord(scripts, index);
		}
	}

	std::vector<std::uint8_t> features(2 + tags.size() * 6);
	vector_font_test::WriteWord(features, 0, static_cast<std::uint16_t>(tags.size()));
	for (std::size_t index = 0; index < tags.size(); ++index) {
		WriteTag(features, 2 + index * 6, tags[index]);
		vector_font_test::WriteWord(features, 6 + index * 6, static_cast<std::uint16_t>(features.size()));
		AppendWord(features, 0);
		AppendWord(features, static_cast<std::uint16_t>(feature_lookups[index].size()));
		for (const auto lookup_index : feature_lookups[index]) {
			AppendWord(features, lookup_index);
		}
	}

	std::vector<std::uint8_t> lookups(8);
	vector_font_test::WriteWord(lookups, 0, 3);
	for (std::size_t index = 0; index < 3; ++index) {
		vector_font_test::WriteWord(lookups, 2 + index * 2, static_cast<std::uint16_t>(lookups.size()));
		for (const auto word : std::array<std::uint16_t, 5>{2, 0, 1, 8, 1}) {
			AppendWord(lookups, word);
		}
	}

	std::vector<std::uint8_t> result(10);
	vector_font_test::WriteDoubleWord(result, 0, 0x10000);
	vector_font_test::WriteWord(result, 4, static_cast<std::uint16_t>(result.size()));
	vector_font_test::Append(result, scripts);
	vector_font_test::WriteWord(result, 6, static_cast<std::uint16_t>(result.size()));
	vector_font_test::Append(result, features);
	vector_font_test::WriteWord(result, 8, static_cast<std::uint16_t>(result.size()));
	vector_font_test::Append(result, lookups);

	return result;
}

static std::vector<std::uint8_t> MakeLongLookupLayout() {
	// One valid feature holds the maximum serialized count of repeated lookup references.
	std::vector<std::uint8_t> result(10);
	vector_font_test::WriteDoubleWord(result, 0, 0x10000);
	vector_font_test::WriteWord(result, 4, 10);
	AppendWord(result, 1);
	const auto tag_offset = result.size();
	result.resize(result.size() + 4);
	WriteTag(result, tag_offset, "DFLT");
	AppendWord(result, 8);
	for (const auto word : std::array<std::uint16_t, 8>{4, 0, 0, 0, 1, 0, 0, 0}) {
		AppendWord(result, word);
	}

	vector_font_test::WriteWord(result, 8, static_cast<std::uint16_t>(result.size()));
	for (const auto word : std::array<std::uint16_t, 7>{1, 4, 2, 0, 1, 8, 1}) {
		AppendWord(result, word);
	}

	vector_font_test::WriteWord(result, 6, static_cast<std::uint16_t>(result.size()));
	AppendWord(result, 1);
	const auto feature_tag = result.size();
	result.resize(result.size() + 4);
	WriteTag(result, feature_tag, "ccmp");
	AppendWord(result, 8);
	AppendWord(result, 0);
	AppendWord(result, 0xffff);
	result.resize(result.size() + 0xffffU * 2U);

	return result;
}

static std::vector<std::uint8_t> MakeFontWithSchedules(std::uint16_t required_substitution, std::uint16_t required_positioning, bool long_feature = false, bool japanese_required = false) {
	const auto base = vector_font_test::MakeFont();
	const sgre::resource::SfntFont original(base);
	std::map<std::string, std::vector<std::uint8_t>> tables;
	for (const auto& table : original.Tables()) {
		const auto bytes = *original.FindTable(table.Tag);
		tables[table.Tag] = std::vector<std::uint8_t>(bytes.begin(), bytes.end());
	}

	tables["GSUB"] = long_feature ? MakeLongLookupLayout() : MakeLayout(required_substitution, japanese_required);
	tables["GPOS"] = MakeLayout(required_positioning, japanese_required);
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

static void RequireLookup(const sgre::text::VectorFontScheduledLookup& lookup, std::uint16_t index, std::uint32_t mask) {
	Require(lookup.Index == index && lookup.Mask == mask && lookup.Options == 3, "scheduled lookup index, merged mask or original policy bits differ");
}

static void TestEmptyTablesAndOwnedPauses() {
	sgre::text::VectorFontLookupPlan retained;
	{
		const auto bytes = vector_font_test::MakeFont();
		const sgre::resource::SfntFont font(bytes);
		sgre::text::VectorFontConfiguration configuration;
		retained = sgre::text::PrepareVectorFontLookupPlan(font, configuration);
	}

	Require(retained.Substitutions.Lookups.empty() && retained.Positioning.Lookups.empty(), "missing actual tables must not produce lookup payloads");
	Require(retained.Substitutions.StageEnds == std::vector<std::size_t>{0, 0} && retained.Positioning.StageEnds == std::vector<std::size_t>{0}, "null-callback pauses survive empty tables and input owner destruction");
	Require(retained.FeatureMap.Features.size() == 1 && retained.FeatureMap.Features[0].Tag == "kern", "missing kern fallback remains metadata without a synthetic lookup");
}

static void TestRequiredAndPerStageMerge() {
	const auto bytes = MakeFontWithSchedules(1, 0);
	const sgre::resource::SfntFont font(bytes);
	sgre::text::VectorFontConfiguration configuration;
	std::array<sgre::text::VectorFontFeatureRequest, 1> requests;
	requests[0].Tag = "liga";
	requests[0].Value = 2;
	const auto plan = sgre::text::PrepareVectorFontLookupPlan(font, configuration, requests);
	Require(plan.FeatureMap.RequiredSubstitutionStage == 1 && plan.FeatureMap.RequiredPositioningStage == 0, "required features keep the stage of their collected descriptor");
	Require(plan.Substitutions.StageEnds == std::vector<std::size_t>{1, 4}, "stage zero rvrn and stage one merged entries must remain separate");
	RequireLookup(plan.Substitutions.Lookups[0], 2, 2);
	RequireLookup(plan.Substitutions.Lookups[1], 0, 14);
	RequireLookup(plan.Substitutions.Lookups[2], 1, 2);
	RequireLookup(plan.Substitutions.Lookups[3], 2, 14);
	Require(plan.Positioning.StageEnds == std::vector<std::size_t>{3}, "one GPOS stage merges required and feature references");
	RequireLookup(plan.Positioning.Lookups[0], 0, 12);
	RequireLookup(plan.Positioning.Lookups[1], 1, 2);
	RequireLookup(plan.Positioning.Lookups[2], 2, 14);
}

static void TestZeroOverrideKeepsRequiredLookup() {
	const auto bytes = MakeFontWithSchedules(1, 0xffff);
	const sgre::resource::SfntFont font(bytes);
	sgre::text::VectorFontConfiguration configuration;
	std::array<sgre::text::VectorFontFeatureRequest, 1> requests;
	requests[0].Tag = "liga";
	requests[0].Value = 0;
	const auto plan = sgre::text::PrepareVectorFontLookupPlan(font, configuration, requests);
	Require(plan.FeatureMap.RequiredSubstitutionStage == 0 && plan.Substitutions.StageEnds == std::vector<std::size_t>{2, 4}, "disabled descriptor leaves required feature in its native default stage");
	RequireLookup(plan.Substitutions.Lookups[0], 0, 2);
	RequireLookup(plan.Substitutions.Lookups[1], 2, 2);
	RequireLookup(plan.Substitutions.Lookups[2], 1, 2);
	RequireLookup(plan.Substitutions.Lookups[3], 2, 2);
}

static void TestVerticalAndUnsupportedProfiles() {
	const auto bytes = MakeFontWithSchedules(0xffff, 0xffff);
	const sgre::resource::SfntFont font(bytes);
	sgre::text::VectorFontConfiguration configuration;
	configuration.Script = sgre::text::VectorFontScript::Han;
	configuration.Direction = sgre::text::VectorFontDirection::TopToBottom;
	const auto vertical = sgre::text::PrepareVectorFontLookupPlan(font, configuration);
	Require(vertical.Substitutions.StageEnds == std::vector<std::size_t>{1, 3}, "vertical common features and vert use actual GSUB stages");
	RequireLookup(vertical.Substitutions.Lookups[0], 2, 2);
	RequireLookup(vertical.Substitutions.Lookups[1], 1, 2);
	RequireLookup(vertical.Substitutions.Lookups[2], 2, 2);
	configuration.Script = sgre::text::VectorFontScript::Arabic;
	RequireFailure([&] {
		(void)sgre::text::PrepareVectorFontLookupPlan(font, configuration);
	},
	    "neutral script");
	configuration.Script = sgre::text::VectorFontScript::Latin;
	configuration.Language = "fr";
	RequireFailure([&] {
		(void)sgre::text::PrepareVectorFontLookupPlan(font, configuration);
	},
	    "canonical en/ja profile");
}

static void TestJapaneseRequiredLookupOwner() {
	const auto bytes = MakeFontWithSchedules(2, 2, false, true);
	const sgre::resource::SfntFont font(bytes);
	auto configuration = sgre::text::ResolveVectorFontConfiguration(0, "ja");
	const auto japanese = sgre::text::PrepareVectorFontLookupPlan(font, configuration);
	Require(japanese.Substitutions.StageEnds.front() == 2 && japanese.Substitutions.Lookups.front().Index == 0 && japanese.Positioning.Lookups.front().Index == 0, "actual JAN required feature must contribute its otherwise-unrequested lookup to each table's real stage");
	configuration.Language = "en";
	const auto english = sgre::text::PrepareVectorFontLookupPlan(font, configuration);
	Require(english.Substitutions.StageEnds.front() == 1 && english.Substitutions.Lookups.front().Index == 2, "the preserved English/default required feature remains distinct from Japanese selection");
}

static void TestScheduledCountLimit() {
	const auto bytes = MakeFontWithSchedules(0, 0xffff, true);
	const sgre::resource::SfntFont font(bytes);
	sgre::text::VectorFontConfiguration configuration;
	RequireFailure([&] {
		(void)sgre::text::PrepareVectorFontLookupPlan(font, configuration);
	},
	    "bounded scheduled lookup count");
}

static void WriteWord(std::uint32_t value) {
	std::array<std::uint8_t, 4> bytes{};
	for (std::size_t index = 0; index < bytes.size(); ++index) {
		bytes[index] = static_cast<std::uint8_t>(value >> (index * 8));
	}

	std::cout.write(reinterpret_cast<const char*>(bytes.data()), 4);
}

static void WriteOptional(std::optional<std::uint16_t> value) {
	WriteWord(value ? *value : 0xffff);
}

static void WritePlan(const sgre::text::VectorFontLookupPlan& plan) {
	const auto& map = plan.FeatureMap;
	WriteWord(map.GlobalMask);
	WriteWord(map.RequiredSubstitutionStage);
	WriteWord(map.RequiredPositioningStage);
	WriteWord(static_cast<std::uint32_t>(map.Features.size()));
	for (const auto& feature : map.Features) {
		std::uint32_t tag = 0;
		for (const auto character : feature.Tag) {
			tag = (tag << 8) | static_cast<std::uint8_t>(character);
		}

		WriteWord(tag);
		WriteOptional(feature.SubstitutionFeature);
		WriteOptional(feature.PositioningFeature);
		WriteWord(feature.SubstitutionStage);
		WriteWord(feature.PositioningStage);
		WriteWord(feature.Shift);
		WriteWord(feature.Mask);
		WriteWord(feature.OneMask);
		WriteWord(feature.LookupFlags);
	}

	for (const auto* schedule : {&plan.Substitutions, &plan.Positioning}) {
		WriteWord(static_cast<std::uint32_t>(schedule->Lookups.size()));
		WriteWord(static_cast<std::uint32_t>(schedule->StageEnds.size()));
		for (const auto end : schedule->StageEnds) {
			WriteWord(static_cast<std::uint32_t>(end));
		}
		for (const auto& lookup : schedule->Lookups) {
			WriteWord(std::uint32_t(lookup.Index) | (std::uint32_t(lookup.Options) << 16));
			WriteWord(lookup.Mask);
		}
	}
}

static void WriteFontProfiles() {
	std::vector<std::uint8_t> bytes;
	char value = 0;
	while (std::cin.get(value)) {
		if (bytes.size() >= 32U * 1024U * 1024U) {
			throw std::runtime_error("lookup-plan local font exceeds bounded stdin");
		}

		bytes.push_back(static_cast<std::uint8_t>(value));
	}

	const sgre::resource::SfntFont font(bytes);
	for (std::size_t profile = 0; profile < 4; ++profile) {
		for (const bool has_language : {true, false}) {
			for (const bool user_features : {false, true}) {
				sgre::text::VectorFontConfiguration configuration;
				configuration.Script = profile == 0 ? sgre::text::VectorFontScript::Latin : profile == 1 ? sgre::text::VectorFontScript::Cyrillic
				                                                                                         : sgre::text::VectorFontScript::Han;
				configuration.Direction = profile == 3 ? sgre::text::VectorFontDirection::TopToBottom : sgre::text::VectorFontDirection::LeftToRight;
				configuration.HasLanguage = has_language;
				std::array<sgre::text::VectorFontFeatureRequest, 3> requests;
				requests[0].Tag = "kern";
				requests[0].Value = 0;
				requests[1].Tag = "vert";
				requests[1].Value = 2;
				requests[1].StartCluster = 3;
				requests[1].EndCluster = 10;
				requests[2].Tag = "liga";
				requests[2].Value = 2;
				WritePlan(sgre::text::PrepareVectorFontLookupPlan(font, configuration, user_features ? std::span(requests) : std::span<const sgre::text::VectorFontFeatureRequest>()));
			}
		}
	}
}

int main(int argc, char** argv) {
	try {
		if (argc == 2 && std::string_view(argv[1]) == "--font") {
			WriteFontProfiles();

			return 0;
		}

		TestEmptyTablesAndOwnedPauses();
		TestRequiredAndPerStageMerge();
		TestZeroOverrideKeepsRequiredLookup();
		TestVerticalAndUnsupportedProfiles();
		TestJapaneseRequiredLookupOwner();
		TestScheduledCountLimit();
		std::cout << "VectorFont lookup plan: five groups passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
