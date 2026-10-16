#include "sgre/text/shaping_feature_map.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
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

static sgre::text::VectorFontCollectedFeature MakeFeature(std::string tag, std::uint32_t order, std::uint32_t value, sgre::text::VectorFontFeatureFlags flags, std::uint32_t substitution_stage = 0, std::uint32_t positioning_stage = 0) {
	sgre::text::VectorFontCollectedFeature result;
	result.Tag = std::move(tag);
	result.Order = order;
	result.Value = value;
	result.Flags = flags;
	result.GlobalValue = (static_cast<std::uint32_t>(flags) & 1) != 0 ? value : 0;
	result.SubstitutionStage = substitution_stage;
	result.PositioningStage = positioning_stage;

	return result;
}

static void TestRealDefaultCollectionAndMissingFallback() {
	sgre::text::VectorFontConfiguration configuration;
	const auto features = sgre::text::CollectVectorFontFeatures(configuration);
	sgre::text::VectorFontFeatureTable absent;
	const auto empty_tables = sgre::text::CompileVectorFontFeatureMasks(features, absent, absent);
	Require(empty_tables.GlobalMask == 2 && empty_tables.Features.size() == 1 && empty_tables.Features[0].Tag == "kern", "only original kern fallback survives absent layout tables");
	const auto& kern = empty_tables.Features[0];
	Require(!kern.SubstitutionFeature && !kern.PositioningFeature && kern.LookupFlags == 7 && kern.Shift == 1 && kern.Mask == 2 && kern.OneMask == 2 && kern.SubstitutionStage == 1, "missing global kern retains its exact fallback and shared-bit mask metadata");
	sgre::text::VectorFontFeatureTable substitutions;
	substitutions.FeatureTags = {"rvrn", "liga", "kern"};
	substitutions.SelectedFeatures = {1, 0};
	substitutions.RequiredFeature = 1;
	const auto mapped = sgre::text::CompileVectorFontFeatureMasks(features, substitutions, absent);
	Require(mapped.Features.size() == 3 && mapped.Features[0].Tag == "kern" && mapped.Features[1].Tag == "liga" && mapped.Features[2].Tag == "rvrn" && mapped.RequiredSubstitutionStage == 1, "compiler sorts tags, keeps required feature stage and retains the missing kern fallback independently");
}

static void TestDuplicatePoliciesAndLanguageOrder() {
	std::array<sgre::text::VectorFontCollectedFeature, 3> features{
	    MakeFeature("kern", 1, 1, sgre::text::VectorFontFeatureFlags::GlobalWithFallback, 5, 7),
	    MakeFeature("kern", 2, 15, sgre::text::VectorFontFeatureFlags::PerCluster, 3, 2),
	    MakeFeature("kern", 3, 2, sgre::text::VectorFontFeatureFlags::GlobalWithTableSearch, 4, 4)};
	sgre::text::VectorFontFeatureTable substitutions;
	substitutions.FeatureTags = {"kern", "kern"};
	substitutions.SelectedFeatures = {1, 0};
	sgre::text::VectorFontFeatureTable absent;
	const auto result = sgre::text::CompileVectorFontFeatureMasks(features, substitutions, absent);
	Require(result.Features.size() == 1 && result.GlobalMask == 10, "later global value replaces the prior range maximum and updates its two-bit global value");
	const auto& feature = result.Features[0];
	Require(feature.SubstitutionFeature == 1 && feature.SubstitutionStage == 3 && feature.PositioningStage == 2 && feature.Mask == 12 && feature.OneMask == 4 && feature.LookupFlags == 6, "selected language order and earliest stages must survive duplicate consolidation");
	substitutions.SelectedFeatures.clear();
	const auto missing = sgre::text::CompileVectorFontFeatureMasks(features, substitutions, absent);
	Require(!missing.Features[0].SubstitutionFeature && missing.Features[0].LookupFlags == 7, "later duplicate table-search bit is not propagated; original first-record fallback remains");
	features[0] = MakeFeature("vert", 1, 1, sgre::text::VectorFontFeatureFlags::GlobalWithTableSearch, 2, 5);
	features[1] = MakeFeature("vert", 2, 15, sgre::text::VectorFontFeatureFlags::PerCluster, 1, 4);
	features[2] = MakeFeature("vert", 3, 7, sgre::text::VectorFontFeatureFlags::PerCluster, 3, 1);
	substitutions.FeatureTags = {"vert", "vert"};
	const auto searched = sgre::text::CompileVectorFontFeatureMasks(features, substitutions, absent);
	Require(searched.Features.size() == 1 && searched.Features[0].SubstitutionFeature == 0 && searched.Features[0].Mask == 60 && searched.GlobalMask == 6 && searched.Features[0].SubstitutionStage == 1 && searched.Features[0].PositioningStage == 1, "first-record table search survives range overrides and preserves the earlier global enabled value");
}

static void TestNativeBitCapacityAndShiftWrapping() {
	std::vector<sgre::text::VectorFontCollectedFeature> features;
	sgre::text::VectorFontFeatureTable substitutions;
	for (std::uint32_t index = 0; index < 7; ++index) {
		const auto tag = "f00" + std::to_string(index);
		const auto value = index == 4 ? 63 : index >= 5 ? 1
		                                                : 255;
		const auto flags = index == 6 ? sgre::text::VectorFontFeatureFlags::PerCluster : sgre::text::VectorFontFeatureFlags::Global;
		features.push_back(MakeFeature(tag, index + 1, static_cast<std::uint32_t>(value), flags));
		substitutions.FeatureTags.push_back(tag);
		substitutions.SelectedFeatures.push_back(static_cast<std::uint16_t>(index));
	}

	sgre::text::VectorFontFeatureTable absent;
	const auto result = sgre::text::CompileVectorFontFeatureMasks(features, substitutions, absent);
	Require(result.Features.size() == 5 && result.Features[3].Tag == "f004" && result.Features[4].Tag == "f005", "full eight-bit request that cannot fit is skipped; later smaller and common-bit requests still compile");
	Require(result.Features[3].Shift == 26 && result.Features[3].Mask == 0xfc000001 && result.Features[3].OneMask == 0x04000000 && result.GlobalMask == 0xfffffffe, "end-at-bit-32 must reproduce x86 shift-count wrapping and unsigned mask subtraction");
	Require(result.Features[4].Shift == 1 && result.Features[4].Mask == 2, "global value one reuses bit one even after all thirty private bits are occupied");
}

static void TestInputSafetyAndZeroValues() {
	sgre::text::VectorFontFeatureTable table;
	table.FeatureTags = {"liga"};
	table.SelectedFeatures = {0};
	table.RequiredFeature = 0;
	sgre::text::VectorFontFeatureTable absent;
	std::array<sgre::text::VectorFontCollectedFeature, 1> features{MakeFeature("liga", 1, 0, sgre::text::VectorFontFeatureFlags::Global, 9, 7)};
	const auto disabled = sgre::text::CompileVectorFontFeatureMasks(features, table, absent);
	Require(disabled.Features.empty() && disabled.RequiredSubstitutionStage == 0 && disabled.GlobalMask == 2, "zero-valued feature is skipped before required-stage selection or mask allocation");
	table.SelectedFeatures = {1};
	RequireFailure([&] {
		(void)sgre::text::CompileVectorFontFeatureMasks(features, table, absent);
	},
	    "selected feature index");
	table.SelectedFeatures = {0};
	table.RequiredFeature = 0xffff;
	RequireFailure([&] {
		(void)sgre::text::CompileVectorFontFeatureMasks(features, table, absent);
	},
	    "required feature index");
	table.RequiredFeature.reset();
	features[0].Order = 2;
	RequireFailure([&] {
		(void)sgre::text::CompileVectorFontFeatureMasks(features, table, absent);
	},
	    "collected-record invariants");
	features[0].Order = 1;
	features[0].Flags = static_cast<sgre::text::VectorFontFeatureFlags>(4);
	RequireFailure([&] {
		(void)sgre::text::CompileVectorFontFeatureMasks(features, table, absent);
	},
	    "collected-record invariants");
}

int main() {
	try {
		TestRealDefaultCollectionAndMissingFallback();
		TestDuplicatePoliciesAndLanguageOrder();
		TestNativeBitCapacityAndShiftWrapping();
		TestInputSafetyAndZeroValues();
		std::cout << "Four independent original VectorFont feature-map groups passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
