#include "sgre/text/vector_metrics.hpp"

#include <array>
#include <functional>
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

static void RequireFailure(const std::function<void()>& operation, std::string_view context) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, "vector metric failure lost its context");

		return;
	}

	throw std::runtime_error("invalid vector metrics were accepted");
}

struct NativeVectorMetricsFixture {
	std::int32_t PixelSize = 0;
	std::uint16_t UnitsPerEm = 0;
	std::uint16_t AdvanceUnits = 0;
	std::int32_t Scale = 0;
	std::int32_t Horizontal = 0;
	std::int32_t Vertical = 0;
};

static void TestRetainedOriginalMetrics() {
	// Original FT request metrics, fast scaling, and HB advance rounding produced these words.
	const std::array<NativeVectorMetricsFixture, 6> fixtures = {{{12, 1000, 225, 50332, 173, -173}, {12, 1000, 622, 50332, 478, -478}, {12, 1000, 1000, 50332, 768, -768}, {24, 1000, 622, 100663, 955, -955}, {48, 1000, 622, 201327, 1911, -1911}, {1, 16, 32767, 262144, 131068, -131068}}};
	for (const auto& fixture : fixtures) {
		const auto scale = sgre::text::PrepareVectorFontScale(fixture.PixelSize, fixture.UnitsPerEm);
		Require(scale.Scale16Dot16 == fixture.Scale, "vector font scale differs from original fixed-point sizing");
		Require(sgre::text::ScaleVectorGlyphAdvance26Dot6(scale, fixture.AdvanceUnits, sgre::text::TextDirection::Horizontal) == fixture.Horizontal, "horizontal vector advance differs from original rounding");
		Require(sgre::text::ScaleVectorGlyphAdvance26Dot6(scale, fixture.AdvanceUnits, sgre::text::TextDirection::Vertical) == fixture.Vertical, "vertical vector advance differs from original rounding");
	}
}

static void TestHalfUnitsAndZeroAdvance() {
	const auto scale = sgre::text::PrepareVectorFontScale(1, 128);
	Require(scale.Scale16Dot16 == 32768, "half-unit font scale changed");
	Require(sgre::text::ScaleVectorGlyphAdvance26Dot6(scale, 1, sgre::text::TextDirection::Horizontal) == 1, "horizontal half-unit advance did not round upward");
	Require(sgre::text::ScaleVectorGlyphAdvance26Dot6(scale, 1, sgre::text::TextDirection::Vertical) == 0, "negative vertical half-unit did not round toward positive infinity");
	Require(sgre::text::ScaleVectorGlyphAdvance26Dot6(scale, 3, sgre::text::TextDirection::Horizontal) == 2, "horizontal one-and-half-unit rounding changed");
	Require(sgre::text::ScaleVectorGlyphAdvance26Dot6(scale, 3, sgre::text::TextDirection::Vertical) == -1, "vertical one-and-half-unit rounding changed");
	Require(sgre::text::ScaleVectorGlyphAdvance26Dot6(scale, 0, sgre::text::TextDirection::Vertical) == 0, "zero unhinted advance changed");
}

static void TestInvalidMetrics() {
	RequireFailure([] {
		static_cast<void>(sgre::text::PrepareVectorFontScale(0, 1000));
	},
	    "positive");
	RequireFailure([] {
		static_cast<void>(sgre::text::PrepareVectorFontScale(12, 15));
	},
	    "units per em");
	RequireFailure([] {
		static_cast<void>(sgre::text::PrepareVectorFontScale(std::numeric_limits<std::int32_t>::max(), 1000));
	},
	    "character size");
	RequireFailure([] {
		static_cast<void>(sgre::text::PrepareVectorFontScale(1000000, 16));
	},
	    "scale exceeds");
	RequireFailure([] {
		static_cast<void>(sgre::text::ScaleVectorGlyphAdvance26Dot6({}, 1, sgre::text::TextDirection::Horizontal));
	},
	    "scale or direction");
	const auto scale = sgre::text::PrepareVectorFontScale(8191, 16);
	RequireFailure([&] {
		static_cast<void>(sgre::text::ScaleVectorGlyphAdvance26Dot6(scale, 65535, sgre::text::TextDirection::Horizontal));
	},
	    "rounding range");
}

int main() {
	try {
		TestRetainedOriginalMetrics();
		TestHalfUnitsAndZeroAdvance();
		TestInvalidMetrics();
		std::cout << "Text vector metrics tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << "Text vector metrics test failure: " << error.what() << '\n';

		return 1;
	}
}
