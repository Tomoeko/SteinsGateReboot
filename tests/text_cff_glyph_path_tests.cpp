#include "sgre/text/cff_glyph_path.hpp"

#include <bit>
#include <cmath>
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
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, "CFF glyph path failure lost its context");

		return;
	}

	throw std::runtime_error("invalid CFF glyph path was accepted");
}

static void TestRetainedNativePixelPaths() {
	sgre::text::CffScaledGlyph glyph;
	glyph.Points = {{0, 0}, {6400, 0}, {6400, 6400}};
	glyph.ContourEnds = {2};
	const auto triangle = sgre::text::PrepareCffGlyphPath(glyph);
	Require(triangle.Commands.size() == 4 && triangle.Commands[0].Kind == sgre::text::CffGlyphPathCommandKind::MoveTo && triangle.Commands[3].Kind == sgre::text::CffGlyphPathCommandKind::LineTo, "native outline closure did not emit its line to the start");
	Require(triangle.Commands[1].Points[0].X == 100 && triangle.Commands[1].Points[0].Y == 0 && triangle.Commands[2].Points[0].X == 100 && triangle.Commands[2].Points[0].Y == -100, "native point-to-pixel conversion or drawing axis differs");
	Require(triangle.Bounds.Left == 0 && triangle.Bounds.Top == -100 && triangle.Bounds.Right == 100 && triangle.Bounds.Bottom == 0, "native triangle path bounds differ");
	Require(glyph.Points[2].Y26Dot6 == 6400, "path preparation changed borrowed glyph points");

	glyph.Points = {{0, 0}, {-6400, 12800, sgre::resource::CffPointKind::CubicControl}, {6400, 12800, sgre::resource::CffPointKind::CubicControl}};
	const auto loop = sgre::text::PrepareCffGlyphPath(glyph);
	Require(loop.Commands.size() == 2 && loop.Commands[1].Kind == sgre::text::CffGlyphPathCommandKind::CubicTo && loop.Commands[1].Points[2].X == 0 && loop.Commands[1].Points[2].Y == 0, "closing cubic did not use its implicit start endpoint");
	// Retained original derivative roots and de Casteljau evaluation differ by one ULP across X extrema.
	Require(std::bit_cast<std::uint64_t>(loop.Bounds.Left) == 0xc03cde155cb14fdc && std::bit_cast<std::uint64_t>(loop.Bounds.Right) == 0x403cde155cb14fdd && loop.Bounds.Top == -150 && loop.Bounds.Bottom == 0, "retained native cubic extrema words differ");
}

static void TestLinearAndConstantDerivativeAxes() {
	sgre::text::CffScaledGlyph glyph;
	glyph.Points = {{0, 0}, {6400, 6400, sgre::resource::CffPointKind::CubicControl}, {6400, 6400, sgre::resource::CffPointKind::CubicControl}};
	glyph.ContourEnds = {2};
	const auto linear_derivative = sgre::text::PrepareCffGlyphPath(glyph);
	Require(linear_derivative.Bounds.Right == 75 && linear_derivative.Bounds.Top == -75 && linear_derivative.Bounds.Left == 0 && linear_derivative.Bounds.Bottom == 0, "linear derivative branch lost its interior extremum");
	glyph.Points = {{640, 0}, {640, 6400, sgre::resource::CffPointKind::CubicControl}, {640, 6400, sgre::resource::CffPointKind::CubicControl}};
	const auto constant_axis = sgre::text::PrepareCffGlyphPath(glyph);
	Require(constant_axis.Bounds.Left == 10 && constant_axis.Bounds.Right == 10 && constant_axis.Bounds.Top == -75, "constant cubic axis produced a non-finite bound");

	glyph.Points = {{1, -1}, {2, -2}};
	glyph.ContourEnds = {1};
	const auto fraction = sgre::text::PrepareCffGlyphPath(glyph);
	Require(fraction.Commands[0].Points[0].X == 0.015625 && fraction.Commands[0].Points[0].Y == 0.015625 && fraction.Bounds.Right == 0.03125 && fraction.Bounds.Bottom == 0.03125, "26.6 fractional pixels were rounded during path preparation");
}

static void TestMultipleContoursAndEmptyGlyphs() {
	sgre::text::CffScaledGlyph glyph;
	glyph.Points = {{-64, 0}, {0, 64}, {640, 640}, {1280, 1280}};
	glyph.ContourEnds = {1, 3};
	const auto path = sgre::text::PrepareCffGlyphPath(glyph);
	Require(path.Commands.size() == 6 && path.Commands[3].Kind == sgre::text::CffGlyphPathCommandKind::MoveTo && path.Bounds.Left == -1 && path.Bounds.Top == -20 && path.Bounds.Right == 20 && path.Bounds.Bottom == 0, "contour sequence or global path bounds differ");
	const auto empty = sgre::text::PrepareCffGlyphPath({});
	Require(empty.Commands.empty() && empty.Bounds.Left == 0 && empty.Bounds.Top == 0 && empty.Bounds.Right == 0 && empty.Bounds.Bottom == 0, "native empty path generated commands or bounds");
}

static void TestMalformedCubicStorage() {
	using PointKind = sgre::resource::CffPointKind;
	for (const auto& points : std::vector<std::vector<sgre::text::CffScaledPoint>>{{{0, 0, PointKind::CubicControl}}, {{0, 0}, {1, 1, PointKind::CubicControl}}, {{0, 0}, {1, 1, PointKind::CubicControl}, {2, 2}}, {{0, 0}, {1, 1, PointKind::CubicControl}, {2, 2, PointKind::CubicControl}, {3, 3, PointKind::CubicControl}}, {{0, 0}, {1, 1, static_cast<PointKind>(0)}}}) {
		sgre::text::CffScaledGlyph glyph;
		glyph.Points = points;
		glyph.ContourEnds = {static_cast<std::uint16_t>(points.size() - 1)};
		RequireFailure([&] {
			static_cast<void>(sgre::text::PrepareCffGlyphPath(glyph));
		},
		    "CFF glyph path");
	}
	for (const auto& ends : std::vector<std::vector<std::uint16_t>>{{}, {2}, {0}, {1, 0}}) {
		sgre::text::CffScaledGlyph glyph;
		glyph.Points = {{0, 0}, {1, 1}};
		glyph.ContourEnds = ends;
		RequireFailure([&] {
			static_cast<void>(sgre::text::PrepareCffGlyphPath(glyph));
		},
		    "CFF glyph path");
	}

	sgre::text::CffScaledGlyph glyph;
	glyph.Points = {{0, std::numeric_limits<std::int32_t>::min()}};
	glyph.ContourEnds = {0};
	RequireFailure([&] {
		static_cast<void>(sgre::text::PrepareCffGlyphPath(glyph));
	},
	    "drawing-axis");
	glyph.Points.resize(32768);
	RequireFailure([&] {
		static_cast<void>(sgre::text::PrepareCffGlyphPath(glyph));
	},
	    "counts");
}

static void TestRetainedNativeBitmapPlans() {
	sgre::text::CffGlyphPathBounds bounds;
	bounds.Top = -100;
	bounds.Right = 100;
	const auto triangle = sgre::text::PlanCffGlyphBitmap(bounds, 5);
	Require(triangle && triangle->LeftPixel == -5 && triangle->TopPixel == -105 && triangle->WidthPixels == 110 && triangle->HeightPixels == 110 && triangle->PathTranslationPixels.X == 5 && triangle->PathTranslationPixels.Y == 105, "retained native bitmap padding or translation differs");
	bounds.Left = std::bit_cast<double>(std::uint64_t(0xc03cde155cb14fdc));
	bounds.Top = -150;
	bounds.Right = std::bit_cast<double>(std::uint64_t(0x403cde155cb14fdd));
	const auto loop = sgre::text::PlanCffGlyphBitmap(bounds, 6);
	Require(loop && loop->LeftPixel == -35 && loop->TopPixel == -156 && loop->WidthPixels == 70 && loop->HeightPixels == 162 && loop->PathTranslationPixels.X == 35 && loop->PathTranslationPixels.Y == 156, "native curve extrema were rounded differently for bitmap storage");
	Require(!sgre::text::PlanCffGlyphBitmap({}, 5), "border padding allocated a bitmap for an empty native path");
	bounds.Left = bounds.Top = bounds.Right = bounds.Bottom = 0.5;
	const auto fraction = sgre::text::PlanCffGlyphBitmap(bounds);
	Require(fraction && fraction->WidthPixels == 1 && fraction->HeightPixels == 1, "native floor/ceil subpixel extent was replaced with geometric width");
}

static void TestMalformedBitmapPlans() {
	sgre::text::CffGlyphPathBounds bounds;
	bounds.Right = bounds.Bottom = 1;
	RequireFailure([&] {
		static_cast<void>(sgre::text::PlanCffGlyphBitmap(bounds, -1));
	},
	    "nonnegative");
	RequireFailure([&] {
		static_cast<void>(sgre::text::PlanCffGlyphBitmap(bounds, std::numeric_limits<std::int32_t>::max()));
	},
	    "signed pixel range");
	for (const auto invalid : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(), -1.0}) {
		bounds.Right = invalid;
		RequireFailure([&] {
			static_cast<void>(sgre::text::PlanCffGlyphBitmap(bounds));
		},
		    "finite ordered");
	}
	bounds.Right = double(std::numeric_limits<std::int32_t>::max()) + 0.25;
	RequireFailure([&] {
		static_cast<void>(sgre::text::PlanCffGlyphBitmap(bounds));
	},
	    "signed pixel range");
	bounds.Right = 1;
	bounds.Left = std::numeric_limits<std::int32_t>::min();
	RequireFailure([&] {
		static_cast<void>(sgre::text::PlanCffGlyphBitmap(bounds));
	},
	    "signed pixel range");
}

int main() {
	try {
		TestRetainedNativePixelPaths();
		TestLinearAndConstantDerivativeAxes();
		TestMultipleContoursAndEmptyGlyphs();
		TestMalformedCubicStorage();
		TestRetainedNativeBitmapPlans();
		TestMalformedBitmapPlans();
		std::cout << "Text CFF glyph path tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << "Text CFF glyph path test failure: " << error.what() << '\n';

		return 1;
	}
}
