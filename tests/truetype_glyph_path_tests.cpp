#include "sgre/text/truetype_glyph_path.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static void TestNativePixelCoordinatesAndClosure() {
	const std::vector<sgre::resource::TrueTypePoint> points{{0, 0}, {6400, 0}, {6400, 6400}};
	const std::vector<std::uint16_t> ends{2};
	const auto path = sgre::text::PrepareTrueTypeGlyphPath(points, ends);
	Require(path.Commands.size() == 4 && path.Commands.back().Kind == sgre::text::CffGlyphPathCommandKind::LineTo, "native line contour must explicitly close to its start");
	Require(path.Commands[2].Points[0].X == 100 && path.Commands[2].Points[0].Y == -100 && path.Bounds.Left == 0 && path.Bounds.Top == -100 && path.Bounds.Right == 100 && path.Bounds.Bottom == 0, "native signed 26.6 conversion or downward-positive bounds differ");
	Require(points[2].Y == 6400 && ends[0] == 2, "path preparation must preserve borrowed outline storage");
}

static void TestImplicitConicStartsAndEndpoints() {
	using Kind = sgre::resource::TrueTypePointKind;
	using Command = sgre::text::CffGlyphPathCommandKind;
	const std::vector<std::uint16_t> ends{2};
	const auto last_on = sgre::text::PrepareTrueTypeGlyphPath(std::vector<sgre::resource::TrueTypePoint>{{320, 640, Kind::ConicControl}, {640, 0}, {0, 0}}, ends);
	Require(last_on.Commands.size() == 3 && last_on.Commands[0].Points[0].X == 0 && last_on.Commands[1].Kind == Command::QuadraticTo && last_on.Commands[1].Points[1].X == 10 && last_on.Commands.back().Kind == Command::LineTo, "first conic must borrow the last on-curve point as its contour start");
	const auto both_off = sgre::text::PrepareTrueTypeGlyphPath(std::vector<sgre::resource::TrueTypePoint>{{1, -1, Kind::ConicControl}, {128, 256}, {4, -4, Kind::ConicControl}}, ends);
	Require(both_off.Commands.size() == 3 && both_off.Commands[0].Points[0].X == 2.0 / 64 && both_off.Commands[0].Points[0].Y == 2.0 / 64 && both_off.Commands.back().Kind == Command::QuadraticTo && both_off.Commands.back().Points[1].X == 2.0 / 64, "conic endpoints must form their implicit start with signed integer division toward zero and close by a quadratic");
	const auto successive = sgre::text::PrepareTrueTypeGlyphPath(std::vector<sgre::resource::TrueTypePoint>{{0, 0}, {1, -1, Kind::ConicControl}, {4, -4, Kind::ConicControl}, {64, 0}}, std::vector<std::uint16_t>{3});
	Require(successive.Commands.size() == 4 && successive.Commands[1].Points[1].X == 2.0 / 64 && successive.Commands[1].Points[1].Y == 2.0 / 64 && successive.Commands[2].Points[0].X == 4.0 / 64, "consecutive conics must retain each control and share the truncated integer midpoint");
}

static void TestQuadraticExtremaAndConstantAxes() {
	using Kind = sgre::resource::TrueTypePointKind;
	const std::vector<std::uint16_t> ends{1};
	const auto loop = sgre::text::PrepareTrueTypeGlyphPath(std::vector<sgre::resource::TrueTypePoint>{{0, 0}, {6400, 12800, Kind::ConicControl}}, ends);
	Require(loop.Commands.size() == 2 && loop.Bounds.Left == 0 && loop.Bounds.Right == 50 && loop.Bounds.Top == -100 && loop.Bounds.Bottom == 0, "quadratic path bounds require curve extrema rather than the control-point box");
	const auto constant = sgre::text::PrepareTrueTypeGlyphPath(std::vector<sgre::resource::TrueTypePoint>{{640, 0}, {640, 6400, Kind::ConicControl}}, ends);
	Require(constant.Bounds.Left == 10 && constant.Bounds.Right == 10 && constant.Bounds.Top == -50 && constant.Bounds.Bottom == 0, "constant-axis NaN must select the native zero root without poisoning bounds");
	const auto outside = sgre::text::PrepareTrueTypeGlyphPath(std::vector<sgre::resource::TrueTypePoint>{{0, 0}, {64, 64, Kind::ConicControl}, {128, 128}}, std::vector<std::uint16_t>{2});
	Require(outside.Bounds.Left == 0 && outside.Bounds.Right == 2 && outside.Bounds.Top == -2 && outside.Bounds.Bottom == 0, "a control inside endpoint bounds must not enlarge the path");
}

static void TestDegenerateAndMultipleContours() {
	using Kind = sgre::resource::TrueTypePointKind;
	const auto empty = sgre::text::PrepareTrueTypeGlyphPath({}, {});
	Require(empty.Commands.empty() && empty.Bounds.Left == 0 && empty.Bounds.Bottom == 0, "empty outline must retain an empty path");
	const auto single = sgre::text::PrepareTrueTypeGlyphPath(std::vector<sgre::resource::TrueTypePoint>{{-5, -7, Kind::ConicControl}}, std::vector<std::uint16_t>{0});
	Require(single.Commands.size() == 2 && single.Commands[1].Kind == sgre::text::CffGlyphPathCommandKind::QuadraticTo && single.Bounds.Left == -5.0 / 64 && single.Bounds.Right == -5.0 / 64 && single.Bounds.Top == 7.0 / 64, "single conic must emit its actual degenerate closing curve");
	const auto multiple = sgre::text::PrepareTrueTypeGlyphPath(std::vector<sgre::resource::TrueTypePoint>{{-64, 0}, {0, 64}, {640, 640}, {1280, 1280}}, std::vector<std::uint16_t>{1, 3});
	Require(multiple.Commands.size() == 6 && multiple.Commands[3].Kind == sgre::text::CffGlyphPathCommandKind::MoveTo && multiple.Bounds.Left == -1 && multiple.Bounds.Top == -20 && multiple.Bounds.Right == 20, "multiple contours must retain separate starts and one accumulated path bound");
}

static void TestInvalidStorageAndTags() {
	const std::vector<sgre::resource::TrueTypePoint> points{{0, 0}, {64, 64}};
	for (const auto& ends : std::vector<std::vector<std::uint16_t>>{{}, {0}, {2}, {1, 0}}) {
		bool rejected = false;
		try {
			(void)sgre::text::PrepareTrueTypeGlyphPath(points, ends);
		} catch (const std::runtime_error& error) {
			rejected = std::string_view(error.what()).starts_with("TrueType glyph path");
		}

		Require(rejected, "invalid contour extent or ordering must reject before traversal");
	}

	for (const auto& input : std::vector<std::vector<sgre::resource::TrueTypePoint>>{{{0, 0, static_cast<sgre::resource::TrueTypePointKind>(2)}}, {{0, std::numeric_limits<std::int32_t>::min()}}, std::vector<sgre::resource::TrueTypePoint>(32768)}) {
		bool rejected = false;
		try {
			(void)sgre::text::PrepareTrueTypeGlyphPath(input, std::vector<std::uint16_t>{static_cast<std::uint16_t>(input.size() - 1)});
		} catch (const std::runtime_error& error) {
			rejected = std::string_view(error.what()).starts_with("TrueType glyph path");
		}

		Require(rejected, "unsupported tags, unsafe drawing-axis negation and native count overflow require explicit guards");
	}
}

int main() {
	try {
		TestNativePixelCoordinatesAndClosure();
		TestImplicitConicStartsAndEndpoints();
		TestQuadraticExtremaAndConstantAxes();
		TestDegenerateAndMultipleContours();
		TestInvalidStorageAndTags();
		std::cout << "TrueType quadratic glyph path: five groups passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
