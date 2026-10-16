#include "sgre/text/cff_hinted_metrics.hpp"

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
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, "CFF hinted metric error lost its context");

		return;
	}

	throw std::runtime_error("invalid CFF hinted metric request was accepted");
}

static std::array<std::int32_t, 12> MetricWords(const sgre::text::CffHintedGlyphMetrics& value) {
	return {value.Width26Dot6, value.Height26Dot6, value.HorizontalBearingX26Dot6, value.HorizontalBearingY26Dot6, value.HorizontalAdvance26Dot6, value.VerticalBearingX26Dot6, value.VerticalBearingY26Dot6, value.VerticalAdvance26Dot6, value.LinearHorizontalAdvance16Dot16, value.LinearVerticalAdvance16Dot16, value.AdvanceX26Dot6, value.AdvanceY26Dot6};
}

static void TestRetainedNativePublication() {
	sgre::text::CffContourOutline outline;
	outline.Points = {{0, 0}, {461, 566}};
	outline.ContourEnds = {1};
	sgre::text::CffGlyphFontMetrics font_metrics;
	font_metrics.HorizontalAdvance = 622;
	font_metrics.VerticalAdvance = 1000;
	font_metrics.VerticalSideBearing = 143;
	const auto scale = sgre::text::PrepareVectorFontScale(12, 1000);
	const auto result = sgre::text::MeasureCffHintedGlyph(outline, scale, scale, font_metrics, 12);
	Require(MetricWords(result) == std::array<std::int32_t, 12>{512, 576, 0, 576, 448, -256, 128, 768, 489164, 786438, 448, 0}, "retained native hinted metric publication differs");
	Require(result.OutlineFlags == 0x104, "small native outline flags differ");
	Require(outline.Points[1].X26Dot6 == 461 && outline.Points[1].Y26Dot6 == 566 && outline.ContourEnds == std::vector<std::uint16_t>{1}, "metric publication changed borrowed geometry");

	font_metrics.HorizontalAdvance = 225;
	font_metrics.VerticalSideBearing = 880;
	const auto empty = sgre::text::MeasureCffHintedGlyph({}, scale, scale, font_metrics, 12);
	Require(MetricWords(empty) == std::array<std::int32_t, 12>{0, 0, 0, 0, 192, -128, 832, 768, 176948, 786438, 192, 0}, "native empty hinted glyph or unscaled vertical bearing differs");
}

static void TestSignedPixelBoundsAndAdvances() {
	sgre::text::CffContourOutline outline;
	outline.Points = {{-1, -65}, {127, 128}};
	outline.ContourEnds = {1};
	sgre::text::CffGlyphFontMetrics font_metrics;
	font_metrics.HorizontalAdvance = 1000;
	font_metrics.VerticalAdvance = 1000;
	font_metrics.VerticalSideBearing = -65;
	const auto scale = sgre::text::PrepareVectorFontScale(24, 1000);
	const auto result = sgre::text::MeasureCffHintedGlyph(outline, scale, scale, font_metrics, 24);
	Require(MetricWords(result) == std::array<std::int32_t, 12>{192, 256, -64, 128, 1536, -832, -128, 1536, 1572859, 1572859, 1536, 0}, "signed pixel bounds or unscaled negative bearing differs");
	Require(result.OutlineFlags == 4, "24-pixel flag threshold differs");

	outline.Points = {{-33, -32}, {32, 33}};
	font_metrics.HorizontalAdvance = 1;
	font_metrics.VerticalAdvance = 1;
	font_metrics.VerticalSideBearing = -1;
	const auto small_scale = sgre::text::PrepareVectorFontScale(1, 128);
	const auto small = sgre::text::MeasureCffHintedGlyph(outline, small_scale, small_scale, font_metrics, 1);
	Require(MetricWords(small) == std::array<std::int32_t, 12>{128, 128, -64, 64, 0, -64, -64, 0, 512, 512, 0, 0}, "native subpixel advances or zero rounding differ");
}

static void TestControlPointBounds() {
	sgre::text::CffContourOutline outline;
	outline.Points = {{0, 0}, {-100, 200, sgre::resource::CffPointKind::CubicControl}, {100, 200, sgre::resource::CffPointKind::CubicControl}, {0, 0}};
	outline.ContourEnds = {3};
	const auto scale = sgre::text::PrepareVectorFontScale(24, 1000);
	const auto result = sgre::text::MeasureCffHintedGlyph(outline, scale, scale, {}, 24);
	Require(result.Width26Dot6 == 256 && result.Height26Dot6 == 256 && result.HorizontalBearingX26Dot6 == -128 && result.HorizontalBearingY26Dot6 == 256, "hinted metrics used curve extrema rather than native control bounds");
}

static void TestInvalidInputs() {
	const auto scale = sgre::text::PrepareVectorFontScale(24, 1000);
	RequireFailure([&] {
		static_cast<void>(sgre::text::MeasureCffHintedGlyph({}, {}, scale, {}, 24));
	},
	    "positive");
	RequireFailure([&] {
		static_cast<void>(sgre::text::MeasureCffHintedGlyph({}, scale, scale, {}, 0));
	},
	    "positive");
	for (const auto& ends : std::vector<std::vector<std::uint16_t>>{{}, {2}, {0}, {1, 0}}) {
		sgre::text::CffContourOutline outline;
		outline.Points = {{0, 0}, {1, 1}};
		outline.ContourEnds = ends;
		RequireFailure([&] {
			static_cast<void>(sgre::text::MeasureCffHintedGlyph(outline, scale, scale, {}, 24));
		},
		    "CFF hinted metrics");
	}

	sgre::text::CffContourOutline outline;
	outline.Points = {{0, 0, static_cast<sgre::resource::CffPointKind>(0)}};
	outline.ContourEnds = {0};
	RequireFailure([&] {
		static_cast<void>(sgre::text::MeasureCffHintedGlyph(outline, scale, scale, {}, 24));
	},
	    "point tag");
	outline.Points = {{std::numeric_limits<std::int32_t>::max(), 0}};
	RequireFailure([&] {
		static_cast<void>(sgre::text::MeasureCffHintedGlyph(outline, scale, scale, {}, 24));
	},
	    "signed range");
	outline.Points = {{std::numeric_limits<std::int32_t>::min(), 0}, {std::numeric_limits<std::int32_t>::max() - 63, 0}};
	outline.ContourEnds = {1};
	RequireFailure([&] {
		static_cast<void>(sgre::text::MeasureCffHintedGlyph(outline, scale, scale, {}, 24));
	},
	    "signed range");
	outline.Points.resize(32768);
	RequireFailure([&] {
		static_cast<void>(sgre::text::MeasureCffHintedGlyph(outline, scale, scale, {}, 24));
	},
	    "count");
}

int main() {
	try {
		TestRetainedNativePublication();
		TestSignedPixelBoundsAndAdvances();
		TestControlPointBounds();
		TestInvalidInputs();
		std::cout << "Text CFF hinted metrics tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << "Text CFF hinted metrics test failure: " << error.what() << '\n';

		return 1;
	}
}
