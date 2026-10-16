#include "sgre/text/shaping_features.hpp"

#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

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

	throw std::runtime_error("VectorFont feature fixture expected failure");
}

static void TestHorizontalNativeOrderAndStages() {
	sgre::text::VectorFontConfiguration configuration;
	const auto features = sgre::text::CollectVectorFontFeatures(configuration);
	const std::array<std::string_view, 17> expected{"rvrn", "ltra", "ltrm", "frac", "numr", "dnom", "ccmp", "locl", "mark", "mkmk", "rlig", "calt", "clig", "curs", "kern", "liga", "rclt"};
	Require(features.size() == expected.size(), "horizontal neutral shaper must collect all seventeen original default features");
	for (std::size_t index = 0; index < features.size(); ++index) {
		const auto& feature = features[index];
		Require(feature.Tag == expected[index] && feature.Order == index + 1 && feature.Value == 1, "feature tag, insertion order and enabled value must match the original collector");
		Require(feature.SubstitutionStage == (index == 0 ? 0 : 1) && feature.PositioningStage == 0, "rvrn must precede the GSUB pause while GPOS remains in stage zero");
	}

	Require(features[3].Flags == sgre::text::VectorFontFeatureFlags::PerCluster && features[3].GlobalValue == 0 && features[14].Flags == sgre::text::VectorFontFeatureFlags::GlobalWithFallback && features[14].GlobalValue == 1, "fraction activation and kern fallback policy must remain distinct");
}

static void TestRightToLeftAndVerticalDefaults() {
	sgre::text::VectorFontConfiguration configuration;
	configuration.Script = sgre::text::VectorFontScript::Cyrillic;
	configuration.Direction = sgre::text::VectorFontDirection::RightToLeft;
	const auto right_to_left = sgre::text::CollectVectorFontFeatures(configuration);
	Require(right_to_left[1].Tag == "rtla" && right_to_left[1].Flags == sgre::text::VectorFontFeatureFlags::Global && right_to_left[2].Tag == "rtlm" && right_to_left[2].Flags == sgre::text::VectorFontFeatureFlags::PerCluster && right_to_left[2].GlobalValue == 0, "right-to-left alternate and mirroring features use different original activation policies");
	configuration.Script = sgre::text::VectorFontScript::Han;
	configuration.Direction = sgre::text::VectorFontDirection::TopToBottom;
	const auto vertical = sgre::text::CollectVectorFontFeatures(configuration);
	const std::array<std::string_view, 10> expected{"rvrn", "frac", "numr", "dnom", "ccmp", "locl", "mark", "mkmk", "rlig", "vert"};
	Require(vertical.size() == expected.size(), "vertical collector must replace horizontal defaults with vert");
	for (std::size_t index = 0; index < vertical.size(); ++index) {
		Require(vertical[index].Tag == expected[index], "vertical feature insertion order differs from the original collector");
	}

	Require(vertical.back().Flags == sgre::text::VectorFontFeatureFlags::GlobalWithTableSearch && vertical.back().GlobalValue == 1, "vert must keep the original table-wide feature search bit");
}

static void TestDuplicateUserRequestsAndClusterPolicy() {
	sgre::text::VectorFontConfiguration configuration;
	std::array<sgre::text::VectorFontFeatureRequest, 3> requests;
	requests[0].Tag = "kern";
	requests[0].Value = 0;
	requests[1].Tag = "kern";
	requests[1].Value = 31;
	requests[1].StartCluster = 2;
	requests[2].Tag = "liga";
	requests[2].Value = 7;
	requests[2].StartCluster = 0xffffffff;
	requests[2].EndCluster = 0;
	const auto features = sgre::text::CollectVectorFontFeatures(configuration, requests);
	Require(features.size() == 20 && features[14].Tag == "kern" && features[17].Tag == "kern" && features[18].Tag == "kern", "collection must preserve default/user duplicate records until the later compiler resolves them");
	Require(features[17].Value == 0 && features[17].Flags == sgre::text::VectorFontFeatureFlags::Global && features[17].GlobalValue == 0 && features[17].Order == 18, "global user zero is a retained disabling record");
	Require(features[18].Value == 31 && features[18].Flags == sgre::text::VectorFontFeatureFlags::PerCluster && features[18].GlobalValue == 0 && features[19].Flags == sgre::text::VectorFontFeatureFlags::PerCluster, "any non-global cluster interval uses per-cluster policy; the collector does not reinterpret reversed ranges");
	requests[0].Tag = "xxxx";
	Require(features[17].Tag == "kern", "collected tags must be owned independently of caller requests");
}

static void TestBoundedAndUnsupportedInputs() {
	sgre::text::VectorFontConfiguration configuration;
	Require(sgre::text::CollectVectorFontFeatures(configuration, {}, 17).size() == 17, "exact feature capacity must accept the complete defaults");
	RequireFailure([&] {
		(void)sgre::text::CollectVectorFontFeatures(configuration, {}, 16);
	},
	    "feature limit");
	RequireFailure([&] {
		(void)sgre::text::CollectVectorFontFeatures(configuration, {}, 65537);
	},
	    "feature limit");
	if (std::numeric_limits<std::size_t>::max() > std::numeric_limits<std::uint32_t>::max()) {
		RequireFailure([&] {
			(void)sgre::text::CollectVectorFontFeatures(configuration, {}, std::numeric_limits<std::size_t>::max());
		},
		    "feature limit");
	}

	configuration.Script = sgre::text::VectorFontScript::Arabic;
	RequireFailure([&] {
		(void)sgre::text::CollectVectorFontFeatures(configuration);
	},
	    "neutral shaper");
	configuration.Script = sgre::text::VectorFontScript::Latin;
	configuration.Direction = static_cast<sgre::text::VectorFontDirection>(7);
	RequireFailure([&] {
		(void)sgre::text::CollectVectorFontFeatures(configuration);
	},
	    "direction");
	configuration.Direction = sgre::text::VectorFontDirection::LeftToRight;
	std::array<sgre::text::VectorFontFeatureRequest, 1> requests;
	for (const auto& tag : std::array<std::string, 3>{"abc", std::string("ab\0d", 4), std::string("abc\xff", 4)}) {
		requests[0].Tag = tag;
		RequireFailure([&] {
			(void)sgre::text::CollectVectorFontFeatures(configuration, requests);
		},
		    "four ASCII");
	}
}

int main() {
	try {
		TestHorizontalNativeOrderAndStages();
		TestRightToLeftAndVerticalDefaults();
		TestDuplicateUserRequestsAndClusterPolicy();
		TestBoundedAndUnsupportedInputs();
		std::cout << "Four independent original VectorFont feature-collection groups passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
