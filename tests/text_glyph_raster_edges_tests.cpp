#include "sgre/text/glyph_raster_edges.hpp"

#include <array>
#include <functional>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static void RequireFailure(const std::function<void()>& operation, std::string_view context) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, "glyph raster edge failure lost context");

		return;
	}

	throw std::runtime_error("invalid glyph raster polyline was accepted");
}

static sgre::text::GlyphRasterClip Clip(std::uint8_t band_height = 64) {
	sgre::text::GlyphRasterClip clip;
	clip.WidthPixels = 256;
	clip.HeightPixels = 256;
	clip.BandHeightPixels = band_height;

	return clip;
}

static void RequirePoints(const sgre::text::GlyphRasterEdge& edge, std::span<const std::array<std::int32_t, 2>> expected) {
	Require(edge.Points.size() == expected.size(), "native edge point count differs");
	for (std::size_t index = 0; index < expected.size(); ++index) {
		Require(edge.Points[index].X24Dot8 == expected[index][0] && edge.Points[index].Y24Dot8 == expected[index][1], "native edge point differs");
	}
}

static void TestRetainedNativeRectangleAndWinding() {
	const std::array<sgre::text::CffGlyphPathPoint, 4> points{{{2, 3}, {9, 3}, {9, 10}, {2, 10}}};
	const auto edges = sgre::text::BuildGlyphRasterPolylineEdges(points, Clip());
	Require(edges.size() == 2 && !edges[0].ReverseWinding && edges[1].ReverseWinding && edges[0].BandIndex == 0 && edges[1].BandIndex == 0, "native rectangle chain/winding differs");
	const std::array<std::array<std::int32_t, 2>, 3> first{{{2304, 768}, {2304, 2560}, {512, 2560}}};
	const std::array<std::array<std::int32_t, 2>, 2> second{{{512, 768}, {512, 2560}}};
	RequirePoints(edges[0], first);
	RequirePoints(edges[1], second);
	Require(points[1].X == 9 && points[1].Y == 3, "edge preparation mutated borrowed points");

	const std::array<sgre::text::CffGlyphPathPoint, 4> reverse{{{2, 3}, {2, 10}, {9, 10}, {9, 3}}};
	const auto reversed = sgre::text::BuildGlyphRasterPolylineEdges(reverse, Clip());
	const std::array<std::array<std::int32_t, 2>, 3> reverse_first{{{512, 768}, {512, 2560}, {2304, 2560}}};
	const std::array<std::array<std::int32_t, 2>, 2> reverse_second{{{2304, 768}, {2304, 2560}}};
	RequirePoints(reversed[0], reverse_first);
	RequirePoints(reversed[1], reverse_second);
}

static void TestNativeDuplicatesAndZeroHeight() {
	const std::array<sgre::text::CffGlyphPathPoint, 5> duplicates{{{4, 4}, {4, 4}, {8, 8}, {8, 8}, {4, 4}}};
	const auto edges = sgre::text::BuildGlyphRasterPolylineEdges(duplicates, Clip());
	Require(edges.size() == 2, "duplicate native edge count differs");
	const std::array<std::array<std::int32_t, 2>, 3> first{{{1024, 1024}, {2048, 2048}, {2048, 2048}}};
	const std::array<std::array<std::int32_t, 2>, 2> second{{{1024, 1024}, {2048, 2048}}};
	RequirePoints(edges[0], first);
	RequirePoints(edges[1], second);
	const std::array<sgre::text::CffGlyphPathPoint, 3> horizontal{{{4, 8}, {8, 8}, {12, 8}}};
	Require(sgre::text::BuildGlyphRasterPolylineEdges(horizontal, Clip()).empty() && sgre::text::BuildGlyphRasterPolylineEdges({}, Clip()).empty(), "empty/horizontal native polyline produced edges");
}

static void TestSeparateAutomaticCloseAndBands() {
	const std::array<sgre::text::CffGlyphPathPoint, 4> points{{{4, 100}, {8, 10}, {12, 20}, {16, 30}}};
	for (const auto band : std::array<std::uint8_t, 4>{8, 16, 32, 64}) {
		const auto edges = sgre::text::BuildGlyphRasterPolylineEdges(points, Clip(band));
		Require(edges.size() == 3 && edges[0].ReverseWinding && !edges[1].ReverseWinding && !edges[2].ReverseWinding, "automatic closing edge was merged into pending native chain");
		Require(edges[0].BandIndex == 10 / band && edges[1].BandIndex == 10 / band && edges[2].BandIndex == 30 / band, "native edge band classification differs");
		const std::array<std::array<std::int32_t, 2>, 2> closing{{{4096, 7680}, {1024, 25600}}};
		RequirePoints(edges[2], closing);
	}

	const std::array<sgre::text::CffGlyphPathPoint, 4> boundary{{{0, 0}, {256, 0}, {256, 256}, {0, 256}}};
	const auto edges = sgre::text::BuildGlyphRasterPolylineEdges(boundary, Clip());
	Require(edges.size() == 2 && edges[0].Points.front().X24Dot8 == 65536 && edges[0].Points.back().Y24Dot8 == 65536, "native inclusive boundary changed");
}

static void TestRetainedNativeFractionalQuantization() {
	const std::array<sgre::text::CffGlyphPathPoint, 3> tiny{{{0.001, 0.001}, {0.002, 0.002}, {0.003, 0.003}}};
	Require(sgre::text::BuildGlyphRasterPolylineEdges(tiny, Clip()).empty(), "native subgrid zero-height path emitted edges");
	const std::array<sgre::text::CffGlyphPathPoint, 3> fractional{{{4, 4}, {8, 4.005}, {12, 4.001}}};
	const auto edges = sgre::text::BuildGlyphRasterPolylineEdges(fractional, Clip());
	Require(edges.size() == 2 && !edges[0].ReverseWinding && edges[1].ReverseWinding, "native quantized subgrid direction differs");
	const std::array<std::array<std::int32_t, 2>, 2> first{{{1024, 1024}, {2048, 1025}}};
	const std::array<std::array<std::int32_t, 2>, 2> second{{{3072, 1024}, {2048, 1025}}};
	RequirePoints(edges[0], first);
	RequirePoints(edges[1], second);
	const std::array<sgre::text::CffGlyphPathPoint, 3> flat_after_quantization{{{4, 4}, {8, 8.001}, {12, 8}}};
	const auto flat = sgre::text::BuildGlyphRasterPolylineEdges(flat_after_quantization, Clip());
	Require(flat.size() == 2 && flat[0].Points.size() == 3, "subgrid floating reversal changed native quantized chain");
}

static void TestUnsupportedAndInvalidInputs() {
	RequireFailure([&] {
		static_cast<void>(sgre::text::BuildGlyphRasterPolylineEdges({}, {}));
	},
	    "dimensions");
	RequireFailure([&] {
		static_cast<void>(sgre::text::BuildGlyphRasterPolylineEdges({}, Clip(7)));
	},
	    "band height");
	for (const auto point : std::array<sgre::text::CffGlyphPathPoint, 4>{{{-1, 0}, {257, 0}, {0, std::numeric_limits<double>::infinity()}, {0, std::numeric_limits<double>::quiet_NaN()}}}) {
		RequireFailure([&] {
			static_cast<void>(sgre::text::BuildGlyphRasterPolylineEdges(std::span(&point, 1), Clip()));
		},
		    "finite");
	}

	std::vector<sgre::text::CffGlyphPathPoint> large(32768);
	RequireFailure([&] {
		static_cast<void>(sgre::text::BuildGlyphRasterPolylineEdges(large, Clip()));
	},
	    "point count");
}

static void TestNativeCubicSplitsAndSubdivision() {
	const std::array<sgre::text::CffGlyphPathPoint, 4> hump{{{2, 3}, {2, 10}, {9, 10}, {9, 3}}};
	const auto edges = sgre::text::BuildGlyphRasterCubicEdges(hump, Clip());
	Require(edges.size() == 2 && !edges[0].ReverseWinding && edges[1].ReverseWinding, "cubic extrema split winding differs");
	const std::array<std::array<std::int32_t, 2>, 5> first{{{512, 768}, {589, 1356}, {792, 1776}, {1079, 2028}, {1408, 2112}}};
	const std::array<std::array<std::int32_t, 2>, 5> second{{{2304, 768}, {2227, 1356}, {2024, 1776}, {1737, 2028}, {1408, 2112}}};
	RequirePoints(edges[0], first);
	RequirePoints(edges[1], second);

	const std::array<sgre::text::CffGlyphPathPoint, 4> monotone{{{2, 3}, {2, 5}, {9, 8}, {9, 10}}};
	const auto split = sgre::text::BuildGlyphRasterCubicEdges(monotone, Clip());
	Require(split.size() == 3 && !split[0].ReverseWinding && !split[1].ReverseWinding && split[2].ReverseWinding, "cubic inflection-vertex split or separate closing edge differs");
	const std::array<std::array<std::int32_t, 2>, 4> split_first{{{512, 768}, {589, 971}, {792, 1192}, {1408, 1664}}};
	const std::array<std::array<std::int32_t, 2>, 4> split_second{{{1408, 1664}, {2024, 2136}, {2227, 2357}, {2304, 2560}}};
	RequirePoints(split[0], split_first);
	RequirePoints(split[1], split_second);
	Require(hump[1].X == 2 && hump[1].Y == 10, "cubic edge preparation mutated borrowed control points");
}

static void TestNativeCubicDegenerateAndSubgridCases() {
	const std::array<sgre::text::CffGlyphPathPoint, 4> straight{{{2, 2}, {4, 4}, {6, 6}, {8, 8}}};
	const auto edges = sgre::text::BuildGlyphRasterCubicEdges(straight, Clip());
	Require(edges.size() == 2 && !edges[0].ReverseWinding && edges[1].ReverseWinding, "collinear cubic edge winding differs");
	const std::array<std::array<std::int32_t, 2>, 2> endpoints{{{512, 512}, {2048, 2048}}};
	RequirePoints(edges[0], endpoints);
	RequirePoints(edges[1], endpoints);

	const std::array<sgre::text::CffGlyphPathPoint, 4> closed_line{{{2, 2}, {20, 20}, {20, 20}, {2, 2}}};
	const auto loop = sgre::text::BuildGlyphRasterCubicEdges(closed_line, Clip());
	const std::array<std::array<std::int32_t, 2>, 2> loop_points{{{512, 512}, {3968, 3968}}};
	Require(loop.size() == 2, "equal-endpoint cubic critical splits were lost");
	RequirePoints(loop[0], loop_points);
	RequirePoints(loop[1], loop_points);

	const std::array<sgre::text::CffGlyphPathPoint, 4> subgrid{{{0.001, 0.001}, {0.002, 0.002}, {0.003, 0.003}, {0.004, 0.004}}};
	const auto fractional = sgre::text::BuildGlyphRasterCubicEdges(subgrid, Clip());
	const std::array<std::array<std::int32_t, 2>, 2> fractional_points{{{0, 0}, {1, 1}}};
	Require(fractional.size() == 2, "fractional cubic closing edge differs");
	RequirePoints(fractional[0], fractional_points);

	const std::array<sgre::text::CffGlyphPathPoint, 4> horizontal{{{2, 8}, {4, 8}, {8, 8}, {16, 8}}};
	Require(sgre::text::BuildGlyphRasterCubicEdges(horizontal, Clip()).empty(), "horizontal cubic produced raster edges");
	const std::array<sgre::text::CffGlyphPathPoint, 4> point{{{4, 4}, {4, 4}, {4, 4}, {4, 4}}};
	Require(sgre::text::BuildGlyphRasterCubicEdges(point, Clip()).empty(), "point cubic produced raster edges");
}

static void TestCubicUnsupportedInputs() {
	std::array<sgre::text::CffGlyphPathPoint, 4> points{};
	RequireFailure([&] {
		static_cast<void>(sgre::text::BuildGlyphRasterCubicEdges(points, Clip(7)));
	},
	    "band height");
	for (const auto invalid : std::array<sgre::text::CffGlyphPathPoint, 4>{{{-1, 0}, {257, 0}, {0, std::numeric_limits<double>::infinity()}, {0, std::numeric_limits<double>::quiet_NaN()}}}) {
		points[0] = invalid;
		RequireFailure([&] {
			static_cast<void>(sgre::text::BuildGlyphRasterCubicEdges(points, Clip()));
		},
		    "finite");
	}
}

static sgre::text::CffGlyphPathCommand PointCommand(sgre::text::CffGlyphPathCommandKind kind, double x, double y) {
	sgre::text::CffGlyphPathCommand command;
	command.Kind = kind;
	command.Points[0] = {x, y};

	return command;
}

static sgre::text::CffGlyphPathCommand CubicCommand(std::array<sgre::text::CffGlyphPathPoint, 3> points) {
	sgre::text::CffGlyphPathCommand command;
	command.Kind = sgre::text::CffGlyphPathCommandKind::CubicTo;
	command.Points = points;

	return command;
}

static void TestNativePathCommandAndContourCoupling() {
	using Kind = sgre::text::CffGlyphPathCommandKind;
	const std::array<sgre::text::CffGlyphPathCommand, 4> contours{PointCommand(Kind::MoveTo, 2, 3), PointCommand(Kind::LineTo, 4, 5), PointCommand(Kind::MoveTo, 10, 10), PointCommand(Kind::LineTo, 20, 20)};
	const auto separate = sgre::text::BuildGlyphRasterPathEdges(contours, Clip());
	Require(separate.size() == 4 && !separate[0].ReverseWinding && separate[1].ReverseWinding && !separate[2].ReverseWinding && separate[3].ReverseWinding, "move did not flush and close the previous native contour");
	const std::array<std::array<std::int32_t, 2>, 2> first{{{512, 768}, {1024, 1280}}};
	const std::array<std::array<std::int32_t, 2>, 2> second{{{2560, 2560}, {5120, 5120}}};
	RequirePoints(separate[0], first);
	RequirePoints(separate[1], first);
	RequirePoints(separate[2], second);
	RequirePoints(separate[3], second);

	const std::array<sgre::text::CffGlyphPathCommand, 3> curves{PointCommand(Kind::MoveTo, 2, 3), CubicCommand({{{2, 10}, {9, 10}, {9, 3}}}), CubicCommand({{{9, 1}, {2, 1}, {2, 3}}})};
	const auto adjacent = sgre::text::BuildGlyphRasterPathEdges(curves, Clip());
	Require(adjacent.size() == 4 && !adjacent[0].ReverseWinding && adjacent[1].ReverseWinding && adjacent[2].ReverseWinding && !adjacent[3].ReverseWinding, "adjacent native cubic pieces merged or closed prematurely");
	const std::array<std::array<std::int32_t, 2>, 4> third{{{1408, 384}, {2024, 480}, {2227, 600}, {2304, 768}}};
	const std::array<std::array<std::int32_t, 2>, 4> fourth{{{1408, 384}, {792, 480}, {589, 600}, {512, 768}}};
	RequirePoints(adjacent[2], third);
	RequirePoints(adjacent[3], fourth);

	const std::array<sgre::text::CffGlyphPathCommand, 4> mixed{PointCommand(Kind::MoveTo, 2, 3), PointCommand(Kind::LineTo, 4, 5), CubicCommand({{{4, 10}, {9, 10}, {9, 3}}}), PointCommand(Kind::LineTo, 8, 1)};
	const auto interleaved = sgre::text::BuildGlyphRasterPathEdges(mixed, Clip(8));
	Require(interleaved.size() == 6 && interleaved.front().Points.size() == 2 && interleaved.back().BandIndex == 1, "line/cubic transition or stable native band order differs");
	RequirePoints(interleaved.front(), first);
	const std::array<std::array<std::int32_t, 2>, 2> closing{{{2048, 256}, {512, 768}}};
	RequirePoints(interleaved[4], closing);
	Require(sgre::text::BuildGlyphRasterPathEdges({}, Clip()).empty(), "empty native path emitted edges");
}

static void TestOutsideControlsWithInteriorCurveExtrema() {
	const std::array<sgre::text::CffGlyphPathPoint, 4> curve{{{64, 64}, {-64, 64}, {320, 192}, {192, 192}}};
	const auto edges = sgre::text::BuildGlyphRasterCubicEdges(curve, Clip(8));
	Require(edges.size() == 5 && edges[0].BandIndex == 8 && edges[1].BandIndex == 8 && edges[2].ReverseWinding && edges[3].BandIndex == 16 && edges[4].BandIndex == 23, "outside controls changed interior cubic split/band selection");
	const std::array<std::array<std::int32_t, 2>, 8> first{{{16384, 16384}, {13299, 16512}, {12127, 16669}, {11191, 16885}, {10479, 17157}, {9983, 17483}, {9692, 17860}, {9597, 18286}}};
	RequirePoints(edges[0], first);
	const std::array<sgre::text::CffGlyphPathPoint, 4> outside{{{64, 64}, {-400, 64}, {-400, 192}, {192, 192}}};
	RequireFailure([&] {
		static_cast<void>(sgre::text::BuildGlyphRasterCubicEdges(outside, Clip()));
	},
	    "clipping");
}

static void TestInvalidPathCommands() {
	const auto line = PointCommand(sgre::text::CffGlyphPathCommandKind::LineTo, 2, 3);
	RequireFailure([&] {
		static_cast<void>(sgre::text::BuildGlyphRasterPathEdges(std::span(&line, 1), Clip()));
	},
	    "move before drawing");
	const auto invalid = PointCommand(static_cast<sgre::text::CffGlyphPathCommandKind>(255), 2, 3);
	RequireFailure([&] {
		static_cast<void>(sgre::text::BuildGlyphRasterPathEdges(std::span(&invalid, 1), Clip()));
	},
	    "command kind");
	std::vector<sgre::text::CffGlyphPathCommand> large(32768);
	RequireFailure([&] {
		static_cast<void>(sgre::text::BuildGlyphRasterPathEdges(large, Clip()));
	},
	    "command count");
	std::vector<sgre::text::CffGlyphPathCommand> excessive{PointCommand(sgre::text::CffGlyphPathCommandKind::MoveTo, 0, 0)};
	for (std::size_t index = 0; index < 5000; ++index) {
		excessive.push_back(CubicCommand({{{65534, 0}, {0, 65534}, {65534, 65534}}}));
		excessive.push_back(CubicCommand({{{0, 65534}, {65534, 0}, {0, 0}}}));
	}

	auto wide_clip = Clip();
	wide_clip.WidthPixels = 65534;
	wide_clip.HeightPixels = 65534;
	RequireFailure([&] {
		static_cast<void>(sgre::text::BuildGlyphRasterPathEdges(excessive, wide_clip));
	},
	    "allocation/work limits");
}

static void TestNativeQuadraticSubdivisionAndWinding() {
	const std::array<sgre::text::CffGlyphPathPoint, 3> points{{{2, 3}, {2, 10}, {9, 3}}};
	const auto edges = sgre::text::BuildGlyphRasterQuadraticEdges(points, Clip(8));
	Require(edges.size() == 2 && !edges[0].ReverseWinding && edges[1].ReverseWinding, "native quadratic extrema must retain two independent monotone chains");
	const std::array<std::array<std::int32_t, 2>, 5> first{{{512, 768}, {540, 1160}, {624, 1440}, {764, 1608}, {960, 1664}}};
	const std::array<std::array<std::int32_t, 2>, 4> second{{{2304, 768}, {1520, 1440}, {1212, 1608}, {960, 1664}}};
	RequirePoints(edges[0], first);
	RequirePoints(edges[1], second);
	const std::array<sgre::text::CffGlyphPathPoint, 3> reversed{{{9, 3}, {2, 10}, {2, 3}}};
	const auto backwards = sgre::text::BuildGlyphRasterQuadraticEdges(reversed, Clip(8));
	Require(backwards.size() == 2 && !backwards[0].ReverseWinding && backwards[1].ReverseWinding, "native reversed quadratic keeps traversal order and independent winding");
	RequirePoints(backwards[0], second);
	RequirePoints(backwards[1], first);
}

static void TestQuadraticPathClosureAndValidation() {
	using Kind = sgre::text::CffGlyphPathCommandKind;
	sgre::text::CffGlyphPathCommand quadratic;
	quadratic.Kind = Kind::QuadraticTo;
	quadratic.Points[0] = {2, 10};
	quadratic.Points[1] = {9, 3};
	sgre::text::CffGlyphPathCommand close;
	close.Kind = Kind::Close;
	const std::array<sgre::text::CffGlyphPathCommand, 5> path{PointCommand(Kind::MoveTo, 2, 3), quadratic, close, PointCommand(Kind::MoveTo, 20, 20), PointCommand(Kind::LineTo, 22, 25)};
	const auto edges = sgre::text::BuildGlyphRasterPathEdges(path, Clip());
	Require(edges.size() == 4 && !edges[0].ReverseWinding && edges[1].ReverseWinding && !edges[2].ReverseWinding && edges[3].ReverseWinding, "explicit native close must finish one quadratic contour before the next move");
	const std::array<std::array<std::int32_t, 2>, 2> line{{{5120, 5120}, {5632, 6400}}};
	RequirePoints(edges[2], line);
	RequirePoints(edges[3], line);
	std::array<sgre::text::CffGlyphPathPoint, 3> invalid{{{2, 3}, {std::numeric_limits<double>::quiet_NaN(), 10}, {9, 3}}};
	RequireFailure([&] {
		static_cast<void>(sgre::text::BuildGlyphRasterQuadraticEdges(invalid, Clip()));
	},
	    "finite");
	invalid[1] = {-20, 10};
	invalid[0] = {0, 3};
	invalid[2] = {0, 20};
	RequireFailure([&] {
		static_cast<void>(sgre::text::BuildGlyphRasterQuadraticEdges(invalid, Clip()));
	},
	    "clipping");
	RequireFailure([&] {
		static_cast<void>(sgre::text::BuildGlyphRasterPathEdges(std::span(&close, 1), Clip()));
	},
	    "move before drawing");
}

int main() {
	try {
		TestRetainedNativeRectangleAndWinding();
		TestNativeDuplicatesAndZeroHeight();
		TestSeparateAutomaticCloseAndBands();
		TestRetainedNativeFractionalQuantization();
		TestUnsupportedAndInvalidInputs();
		TestNativeCubicSplitsAndSubdivision();
		TestNativeCubicDegenerateAndSubgridCases();
		TestCubicUnsupportedInputs();
		TestNativePathCommandAndContourCoupling();
		TestOutsideControlsWithInteriorCurveExtrema();
		TestInvalidPathCommands();
		TestNativeQuadraticSubdivisionAndWinding();
		TestQuadraticPathClosureAndValidation();
		std::cout << "Text glyph raster edge tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << "Text glyph raster edge failure: " << error.what() << '\n';

		return 1;
	}
}
