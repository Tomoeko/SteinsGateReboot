#include "sgre/text/vector_glyph_draw.hpp"

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

static void TestActualBearingsFloorAndSourceRectangles() {
	sgre::text::CachedVectorGlyphDraw draw;
	draw.Position.X = 10.25f;
	draw.Position.Y = 22.75f;
	draw.ColorRgba = 0x89abcdef;
	draw.Region.AtlasLeftPixels = 17;
	draw.Region.AtlasTopPixels = 29;
	draw.Region.LeftPixel = -2;
	draw.Region.TopTranslationPixel = 7;
	draw.Region.WidthPixels = 3;
	draw.Region.HeightPixels = 5;
	const auto rectangle = sgre::text::PrepareVectorGlyphDrawRectangle(draw, true);
	Require(rectangle && rectangle->SourcePixels.Left == 17 && rectangle->SourcePixels.Top == 29 && rectangle->SourcePixels.Right == 20 && rectangle->SourcePixels.Bottom == 34, "cached vector atlas coordinates were changed or normalized before rectangle submission");
	Require(rectangle->DestinationPixels.Left == 8.25f && rectangle->DestinationPixels.Top == 15 && rectangle->DestinationPixels.Right == 11.25f && rectangle->DestinationPixels.Bottom == 20 && rectangle->ColorRgba == 0x89abcdef, "native bearing translation, vertical floor or RGBA forwarding differs");
	draw.Position.Y = -0.25f;
	draw.Region.TopTranslationPixel = 0;
	draw.Region.HeightPixels = 2;
	const auto negative = sgre::text::PrepareVectorGlyphDrawRectangle(draw, true);
	Require(negative->DestinationPixels.Top == -1 && negative->DestinationPixels.Bottom == 1, "native floor must round negative fractional baselines down");
}

static void TestSeparateIntegerConversionBeforeAddition() {
	sgre::text::CachedVectorGlyphDraw draw;
	draw.Region.AtlasLeftPixels = 1;
	draw.Region.WidthPixels = 16777217;
	draw.Region.HeightPixels = 1;
	draw.Position.X = 0.5f;
	const auto rectangle = sgre::text::PrepareVectorGlyphDrawRectangle(draw, true);
	Require(rectangle->SourcePixels.Right == 16777216.0f && rectangle->DestinationPixels.Right == 16777216.0f, "native signed integer-to-float conversion must happen separately before endpoint addition");
}

static void TestAbsentAtlasAndNonfinitePosition() {
	sgre::text::CachedVectorGlyphDraw draw;
	draw.Position.X = std::numeric_limits<float>::quiet_NaN();
	Require(!sgre::text::PrepareVectorGlyphDrawRectangle(draw, false), "absent native atlas must skip the rectangle before coordinate processing");
	try {
		static_cast<void>(sgre::text::PrepareVectorGlyphDrawRectangle(draw, true));
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find("not finite") != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("nonfinite vector glyph draw position was accepted");
}

int main() {
	try {
		TestActualBearingsFloorAndSourceRectangles();
		TestSeparateIntegerConversionBeforeAddition();
		TestAbsentAtlasAndNonfinitePosition();
		std::cout << "Vector glyph rectangle bearings, native floor and float endpoint order passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
