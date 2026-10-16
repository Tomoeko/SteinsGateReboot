#include "sgre/text/cff_glyph_metrics.hpp"

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
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, "CFF glyph metric failure lost its context");

		return;
	}

	throw std::runtime_error("invalid CFF glyph metrics were accepted");
}

static sgre::text::VectorFontScale Scale(std::int32_t value) {
	sgre::text::VectorFontScale scale;
	scale.Scale16Dot16 = value;

	return scale;
}

static std::array<std::int32_t, 10> MetricWords(const sgre::text::CffGlyphMetrics& metrics) {
	return {metrics.Width26Dot6, metrics.Height26Dot6, metrics.HorizontalBearingX26Dot6, metrics.HorizontalBearingY26Dot6, metrics.HorizontalAdvance26Dot6, metrics.VerticalBearingX26Dot6, metrics.VerticalSideBearingFontUnits, metrics.VerticalAdvance26Dot6, metrics.VerticalOriginX26Dot6, metrics.VerticalOriginY26Dot6};
}

static void TestRetainedNativeMetricPhase() {
	sgre::resource::CffDesignOutline outline;
	outline.Points = {{0, 0}, {600, 737}};
	outline.ContourEnds = {1};
	sgre::text::CffGlyphFontMetrics metrics;
	metrics.HorizontalAdvance = 622;
	metrics.VerticalAdvance = 1000;
	metrics.VerticalSideBearing = 143;
	const auto scale = sgre::text::PrepareVectorFontScale(12, 1000);
	const auto actual = sgre::text::ScaleCffGlyphOutline26Dot6(outline, scale, scale, metrics);
	Require(MetricWords(actual.Metrics) == std::array<std::int32_t, 10>{461, 566, 0, 566, 478, -239, 143, 768, 239, 709}, "retained original glyph metrics or vertical origin differ");
	Require(actual.Points[1].X26Dot6 == 461 && actual.Points[1].Y26Dot6 == 566 && actual.ContourEnds == outline.ContourEnds, "glyph scaling changed point or contour storage");
	Require(outline.Points[1].X == 600 && outline.Points[1].Y == 737, "glyph scaling changed borrowed design points");

	metrics.HorizontalAdvance = 225;
	metrics.VerticalSideBearing = 880;
	const auto empty = sgre::text::ScaleCffGlyphOutline26Dot6({}, scale, scale, metrics);
	Require(MetricWords(empty.Metrics) == std::array<std::int32_t, 10>{0, 0, 0, 0, 173, -86, 880, 768, 86, 880}, "native empty glyph bounds or unscaled vertical bearing differ");
}

static void TestSignedRoundingAndControlBounds() {
	sgre::resource::CffDesignOutline outline;
	outline.Points = {{-3, -1}, {3, 1}};
	outline.ContourEnds = {1};
	sgre::text::CffGlyphFontMetrics metrics;
	metrics.HorizontalAdvance = 3;
	metrics.VerticalAdvance = 1;
	metrics.VerticalSideBearing = -4;
	const auto half = Scale(32768);
	const auto actual = sgre::text::ScaleCffGlyphOutline26Dot6(outline, half, half, metrics);
	Require(actual.Points[0].X26Dot6 == -2 && actual.Points[0].Y26Dot6 == -1 && actual.Points[1].X26Dot6 == 2 && actual.Points[1].Y26Dot6 == 1, "native signed coordinate halves did not round away from zero");
	Require(MetricWords(actual.Metrics) == std::array<std::int32_t, 10>{4, 2, -2, 1, 2, -3, -4, 1, 1, -3}, "signed control metrics differ");

	outline.Points = {{0, 0}, {-100, 200, sgre::resource::CffPointKind::CubicControl}, {100, 200, sgre::resource::CffPointKind::CubicControl}, {0, 0}};
	outline.ContourEnds = {3};
	const auto control_bounds = sgre::text::ScaleCffGlyphOutline26Dot6(outline, half, Scale(65536), metrics);
	Require(control_bounds.Metrics.Width26Dot6 == 100 && control_bounds.Metrics.Height26Dot6 == 200 && control_bounds.Metrics.HorizontalBearingX26Dot6 == -50 && control_bounds.Metrics.HorizontalBearingY26Dot6 == 200, "native control bounds were replaced with cubic extrema");
	Require(control_bounds.Points[1].Kind == sgre::resource::CffPointKind::CubicControl, "scaled glyph lost its cubic point tag");
}

static void TestInvalidScalesAndOutlines() {
	const auto unit = Scale(65536);
	RequireFailure([&] {
		static_cast<void>(sgre::text::ScaleCffGlyphOutline26Dot6({}, {}, unit, {}));
	},
	    "positive");
	RequireFailure([&] {
		static_cast<void>(sgre::text::ScaleCffGlyphOutline26Dot6({}, unit, Scale(-1), {}));
	},
	    "positive");
	for (const auto& ends : std::vector<std::vector<std::uint16_t>>{{}, {2}, {0}, {1, 0}}) {
		sgre::resource::CffDesignOutline outline;
		outline.Points = {{0, 0}, {1, 1}};
		outline.ContourEnds = ends;
		RequireFailure([&] {
			static_cast<void>(sgre::text::ScaleCffGlyphOutline26Dot6(outline, unit, unit, {}));
		},
		    "CFF glyph metrics");
	}
	sgre::resource::CffDesignOutline outline;
	outline.Points = {{0, 0, static_cast<sgre::resource::CffPointKind>(0)}};
	outline.ContourEnds = {0};
	RequireFailure([&] {
		static_cast<void>(sgre::text::ScaleCffGlyphOutline26Dot6(outline, unit, unit, {}));
	},
	    "point tag");
	outline.Points.resize(32768);
	RequireFailure([&] {
		static_cast<void>(sgre::text::ScaleCffGlyphOutline26Dot6(outline, unit, unit, {}));
	},
	    "counts");
}

static void TestCoordinateAndBoundsOverflow() {
	const auto unit = Scale(65536);
	sgre::resource::CffDesignOutline outline;
	outline.Points = {{std::numeric_limits<std::int32_t>::min(), 0}, {std::numeric_limits<std::int32_t>::max(), 0}};
	outline.ContourEnds = {1};
	RequireFailure([&] {
		static_cast<void>(sgre::text::ScaleCffGlyphOutline26Dot6(outline, unit, unit, {}));
	},
	    "coordinate range");
	outline.Points = {{std::numeric_limits<std::int32_t>::max(), 0}};
	outline.ContourEnds = {0};
	RequireFailure([&] {
		static_cast<void>(sgre::text::ScaleCffGlyphOutline26Dot6(outline, Scale(std::numeric_limits<std::int32_t>::max()), unit, {}));
	},
	    "coordinate range");
	outline.Points = {{0, std::numeric_limits<std::int32_t>::max()}};
	sgre::text::CffGlyphFontMetrics metrics;
	metrics.VerticalSideBearing = 1;
	RequireFailure([&] {
		static_cast<void>(sgre::text::ScaleCffGlyphOutline26Dot6(outline, unit, unit, metrics));
	},
	    "coordinate range");
}

int main() {
	try {
		TestRetainedNativeMetricPhase();
		TestSignedRoundingAndControlBounds();
		TestInvalidScalesAndOutlines();
		TestCoordinateAndBoundsOverflow();
		std::cout << "Text CFF glyph metrics tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << "Text CFF glyph metrics test failure: " << error.what() << '\n';

		return 1;
	}
}
