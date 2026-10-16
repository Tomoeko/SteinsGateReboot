#include "sgre/text/glyph_raster_cells.hpp"

#include <array>
#include <bit>
#include <functional>
#include <iostream>
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
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, "glyph area failure lost context");

		return;
	}

	throw std::runtime_error("invalid glyph area input was accepted");
}

static sgre::text::GlyphRasterClip Clip() {
	sgre::text::GlyphRasterClip clip;
	clip.WidthPixels = 16;
	clip.HeightPixels = 16;

	return clip;
}

static std::int32_t SignedCell(const sgre::text::GlyphRasterAreaCells& cells, std::size_t row, std::size_t column) {
	return std::bit_cast<std::int32_t>(cells.Deltas.at(row * (std::size_t(cells.WidthPixels) + 1) + column));
}

static void TestRetainedNativeRectangleArea() {
	const std::array<sgre::text::CffGlyphPathPoint, 4> rectangle{{{2, 3}, {9, 3}, {9, 10}, {2, 10}}};
	const auto edges = sgre::text::BuildGlyphRasterPolylineEdges(rectangle, Clip());
	const auto cells = sgre::text::AccumulateGlyphRasterCells(edges, Clip());
	Require(cells.WidthPixels == 16 && cells.FirstRowPixel == 3 && cells.RowCount == 7 && cells.Deltas.size() == 119, "native rectangle area storage differs");
	for (std::size_t row = 0; row < cells.RowCount; ++row) {
		for (std::size_t column = 0; column <= cells.WidthPixels; ++column) {
			const auto expected = column == 2 ? -131072 : column == 9 ? 131072
			                                                          : 0;
			Require(SignedCell(cells, row, column) == expected, "native rectangle signed area delta differs");
		}
	}
	Require(edges.front().Points.front().X24Dot8 == 2304, "area accumulation mutated borrowed edge storage");
}

static void TestFractionalVerticalAndWrappedAddition() {
	sgre::text::GlyphRasterEdge edge;
	edge.Points = {{640, 3 * 256}, {640, 10 * 256}};
	const auto cells = sgre::text::AccumulateGlyphRasterCells(std::span(&edge, 1), Clip());
	for (std::size_t row = 0; row < cells.RowCount; ++row) {
		Require(SignedCell(cells, row, 2) == 65536 && SignedCell(cells, row, 3) == 65536, "fractional vertical area split differs");
	}

	edge.ReverseWinding = true;
	const auto reversed = sgre::text::AccumulateGlyphRasterCells(std::span(&edge, 1), Clip());
	Require(SignedCell(reversed, 0, 2) == -65536 && SignedCell(reversed, 0, 3) == -65536, "reverse winding did not negate area deltas");
	edge.ReverseWinding = false;
	edge.Points = {{0, 0}, {0, 256}};
	std::vector<sgre::text::GlyphRasterEdge> repeats(32768, edge);
	const auto wrapped = sgre::text::AccumulateGlyphRasterCells(repeats, Clip());
	Require(wrapped.RowCount == 1 && wrapped.Deltas[0] == 0, "native32bit area accumulation did not wrap without signed overflow");
}

static void TestRetainedRoundedRowBoundary() {
	sgre::text::GlyphRasterEdge edge;
	edge.Points = {{2984, 1227}, {2781, 4078}};
	const auto cells = sgre::text::AccumulateGlyphRasterCells(std::span(&edge, 1), Clip());
	Require(cells.FirstRowPixel == 4 && cells.RowCount == 12, "native sloped area row bounds differ");
	Require(SignedCell(cells, 10, 10) == 4608 && SignedCell(cells, 10, 11) == 126464, "rounded row/column coincidence was visited twice");
}

static void TestRetainedZeroAreaGroupAtRowBoundary() {
	sgre::text::GlyphRasterEdge edge;
	edge.ReverseWinding = true;
	edge.Points = {{1557, 2007}, {632, 2078}, {1282, 3915}};
	const auto cells = sgre::text::AccumulateGlyphRasterCells(std::span(&edge, 1), Clip());
	Require(cells.FirstRowPixel == 7 && cells.ActiveGroups[0] == 1 && cells.ActiveGroups[1] == 1 && SignedCell(cells, 0, 3) == 0, "native zero-area crossing was assigned to the following row or omitted from its group mask");
}

static void TestInvalidGeometryAndBounds() {
	Require(sgre::text::AccumulateGlyphRasterCells({}, Clip()).Deltas.empty(), "empty geometry allocated area cells");
	RequireFailure([&] {
		static_cast<void>(sgre::text::AccumulateGlyphRasterCells({}, {}));
	},
	    "dimensions");
	auto invalid_clip = Clip();
	invalid_clip.BandHeightPixels = 0;
	RequireFailure([&] {
		static_cast<void>(sgre::text::AccumulateGlyphRasterCells({}, invalid_clip));
	},
	    "band height");
	sgre::text::GlyphRasterEdge edge;
	RequireFailure([&] {
		static_cast<void>(sgre::text::AccumulateGlyphRasterCells(std::span(&edge, 1), Clip()));
	},
	    "point count");
	for (const auto invalid : std::array<sgre::text::GlyphRasterPoint, 4>{{{-1, 0}, {4097, 0}, {0, -1}, {0, 4097}}}) {
		edge.Points = {invalid, {0, 256}};
		RequireFailure([&] {
			static_cast<void>(sgre::text::AccumulateGlyphRasterCells(std::span(&edge, 1), Clip()));
		},
		    "coordinate/direction");
	}
	edge.Points = {{0, 256}, {0, 0}};
	RequireFailure([&] {
		static_cast<void>(sgre::text::AccumulateGlyphRasterCells(std::span(&edge, 1), Clip()));
	},
	    "coordinate/direction");
	edge.Points = {{0, 0}, {0, 256}};
	edge.BandIndex = 1;
	RequireFailure([&] {
		static_cast<void>(sgre::text::AccumulateGlyphRasterCells(std::span(&edge, 1), Clip()));
	},
	    "band provenance");
	edge.BandIndex = 0;
	edge.Points = {{0, 0}, {0, 65534 * 256}};
	auto huge_clip = Clip();
	huge_clip.WidthPixels = 65534;
	huge_clip.HeightPixels = 65534;
	RequireFailure([&] {
		static_cast<void>(sgre::text::AccumulateGlyphRasterCells(std::span(&edge, 1), huge_clip));
	},
	    "allocation limit");
	edge.Points = {{0, 0}, {65534 * 256, 256}};
	huge_clip.HeightPixels = 1;
	std::vector<sgre::text::GlyphRasterEdge> expensive(128, edge);
	RequireFailure([&] {
		static_cast<void>(sgre::text::AccumulateGlyphRasterCells(expensive, huge_clip));
	},
	    "work limit");
}

int main() {
	try {
		TestRetainedNativeRectangleArea();
		TestFractionalVerticalAndWrappedAddition();
		TestRetainedRoundedRowBoundary();
		TestRetainedZeroAreaGroupAtRowBoundary();
		TestInvalidGeometryAndBounds();
		std::cout << "Text glyph raster area cell tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << "Text glyph raster area failure: " << error.what() << '\n';

		return 1;
	}
}
