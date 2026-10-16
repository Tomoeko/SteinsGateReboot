#include "sgre/text/glyph_raster_solid_copy.hpp"

#include <array>
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
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, "glyph copy failure lost context");

		return;
	}

	throw std::runtime_error("invalid glyph solid-copy input was accepted");
}

static sgre::text::GlyphRasterClip Clip() {
	sgre::text::GlyphRasterClip clip;
	clip.WidthPixels = 16;
	clip.HeightPixels = 16;

	return clip;
}

static sgre::text::GlyphRasterAreaCells RectangleCells() {
	const std::array<sgre::text::CffGlyphPathPoint, 4> points{{{2, 3}, {9, 3}, {9, 10}, {2, 10}}};
	const auto edges = sgre::text::BuildGlyphRasterPolylineEdges(points, Clip());

	return sgre::text::AccumulateGlyphRasterCells(edges, Clip());
}

static void TestNativeDefaultWhitePixelsAndRetainedInputs() {
	const auto cells = RectangleCells();
	const auto original_deltas = cells.Deltas;
	const auto original_groups = cells.ActiveGroups;
	std::vector<std::uint32_t> surface(16 * 16);
	sgre::text::CompositeGlyphRasterSolidCopy(cells, surface, 16);
	for (std::size_t row = 0; row < 16; ++row) {
		for (std::size_t column = 0; column < 16; ++column) {
			std::uint32_t expected = 0;
			if (row >= 3 && row < 10) {
				if (column == 2 || column == 3 || column == 8) {
					expected = 0xfefefefe;
				} else if (column >= 4 && column < 8) {
					expected = 0xffffffff;
				}
			}

			Require(surface[row * 16 + column] == expected, "native white raster pixel differs");
		}
	}
	Require(cells.Deltas == original_deltas && cells.ActiveGroups == original_groups, "solid copy consumed immutable prepared area storage");
}

static void TestFillRuleAndKernelOpacityPaths() {
	const auto cells = RectangleCells();
	std::vector<std::uint32_t> surface(16 * 16, 0xffffffff);
	sgre::text::CompositeGlyphRasterSolidCopy(cells, surface, 16, 0xffffffff, 0);
	Require(surface[3 * 16] == 0xfefefefe && surface[3 * 16 + 4] == 0xffffffff && surface[3 * 16 + 12] == 0xffffffff, "native selected-kernel zero-opacity edge/gap behavior differs");

	const std::array<sgre::text::CffGlyphPathPoint, 4> points{{{2, 3}, {9, 3}, {9, 10}, {2, 10}}};
	const auto single = sgre::text::BuildGlyphRasterPolylineEdges(points, Clip());
	auto doubled = single;
	doubled.insert(doubled.end(), single.begin(), single.end());
	const auto twice = sgre::text::AccumulateGlyphRasterCells(doubled, Clip());
	std::vector<std::uint32_t> nonzero(16 * 16);
	std::vector<std::uint32_t> evenodd(16 * 16);
	sgre::text::CompositeGlyphRasterSolidCopy(twice, nonzero, 16);
	sgre::text::CompositeGlyphRasterSolidCopy(twice, evenodd, 16, 0xffffffff, 255, sgre::text::GlyphRasterFillRule::EvenOdd);
	Require(nonzero[4 * 16 + 5] == 0xffffffff && evenodd == std::vector<std::uint32_t>(16 * 16), "native nonzero/evenodd coverage mask differs");

	const auto empty = sgre::text::AccumulateGlyphRasterCells({}, Clip());
	const auto retained = surface;
	sgre::text::CompositeGlyphRasterSolidCopy(empty, surface, 16);
	Require(surface == retained, "empty area changed destination pixels");
}

static void TestNativeMultipleBandContinuity() {
	const std::array<sgre::text::CffGlyphPathPoint, 4> rectangle{{{2, 30}, {9, 30}, {9, 180}, {2, 180}}};
	for (const auto band : std::array<std::uint8_t, 4>{8, 16, 32, 64}) {
		auto clip = Clip();
		clip.HeightPixels = 256;
		clip.BandHeightPixels = band;
		const auto edges = sgre::text::BuildGlyphRasterPolylineEdges(rectangle, clip);
		const auto cells = sgre::text::AccumulateGlyphRasterCells(edges, clip);
		Require(cells.FirstRowPixel == 30 && cells.RowCount == 150, "native multiple-band row extent differs");
		std::vector<std::uint32_t> surface(16 * 256);
		sgre::text::CompositeGlyphRasterSolidCopy(cells, surface, 256);
		std::size_t copied = 0;
		std::size_t approximated = 0;
		for (std::size_t row = 0; row < 256; ++row) {
			for (std::size_t column = 0; column < 16; ++column) {
				std::uint32_t expected = 0;
				if (row >= 30 && row < 180) {
					if (column == 2 || column == 3 || column == 8) {
						expected = 0xfefefefe;
						++approximated;
					} else if (column >= 4 && column < 8) {
						expected = 0xffffffff;
						++copied;
					}
				}

				Require(surface[row * 16 + column] == expected, "native multiple-band pixel continuation differs");
			}
		}
		Require(copied == 600 && approximated == 450, "retained native multiple-band pixel counts differ");
	}
}

static void TestStridedByteStorageAndSentinels() {
	const auto cells = RectangleCells();
	std::vector<std::uint32_t> expected(16 * 16, 0x51342619);
	sgre::text::CompositeGlyphRasterSolidCopy(cells, expected, 16, 0x9f614a2c, 173);
	constexpr std::size_t pitch = 71;
	constexpr std::size_t prefix = 3;
	std::vector<std::uint8_t> storage(prefix + pitch * 16 + 5, 0xa5);
	for (std::size_t row = 0; row < 16; ++row) {
		for (std::size_t column = 0; column < 16; ++column) {
			for (std::uint32_t channel = 0; channel < 4; ++channel) {
				storage[prefix + row * pitch + column * 4 + channel] = static_cast<std::uint8_t>(std::uint32_t(0x51342619) >> (channel * 8));
			}
		}
	}
	const auto prior = storage;
	sgre::text::CompositeGlyphRasterSolidCopyBytes(cells, std::span(storage).subspan(prefix), 16, pitch, 0x9f614a2c, 173);
	for (std::size_t offset = 0; offset < storage.size(); ++offset) {
		if (offset < prefix || offset >= prefix + pitch * 16 || (offset - prefix) % pitch >= 64) {
			Require(storage[offset] == prior[offset], "strided copy touched a prefix, row padding, or suffix sentinel");
			continue;
		}
		const auto row = (offset - prefix) / pitch;
		const auto within_row = (offset - prefix) % pitch;
		const auto channel = within_row % 4;
		const auto word = expected[row * 16 + within_row / 4];
		Require(storage[offset] == static_cast<std::uint8_t>(word >> (channel * 8)), "strided little-endian byte channel differs from word-buffer output");
	}
	const auto retained = storage;
	RequireFailure([&] {
		sgre::text::CompositeGlyphRasterSolidCopyBytes(cells, storage, 16, 63);
	},
	    "byte surface");
	RequireFailure([&] {
		sgre::text::CompositeGlyphRasterSolidCopyBytes(cells, std::span(storage).first(pitch * 15 + 63), 16, pitch);
	},
	    "byte surface");
	RequireFailure([&] {
		sgre::text::CompositeGlyphRasterSolidCopyBytes(cells, storage, 16, 0xffffffff);
	},
	    "byte surface");
	Require(storage == retained, "invalid strided surface was partially modified");
}

static void TestInvalidSurfaceStyleAndAreaStorage() {
	const auto cells = RectangleCells();
	std::vector<std::uint32_t> surface(16 * 16);
	RequireFailure([&] {
		sgre::text::CompositeGlyphRasterSolidCopy(cells, {}, 16);
	},
	    "surface dimensions");
	RequireFailure([&] {
		sgre::text::CompositeGlyphRasterSolidCopy(cells, surface, 16, 0x00112233);
	},
	    "premultiplied");
	RequireFailure([&] {
		sgre::text::CompositeGlyphRasterSolidCopy(cells, surface, 16, 0xffffffff, 255, static_cast<sgre::text::GlyphRasterFillRule>(255));
	},
	    "fill rule");
	auto broken = cells;
	broken.Deltas.pop_back();
	RequireFailure([&] {
		sgre::text::CompositeGlyphRasterSolidCopy(broken, surface, 16);
	},
	    "area storage");
	broken = cells;
	broken.FirstRowPixel = 15;
	RequireFailure([&] {
		sgre::text::CompositeGlyphRasterSolidCopy(broken, surface, 16);
	},
	    "area storage");
	broken = cells;
	broken.ActiveGroups[0] = 2;
	RequireFailure([&] {
		sgre::text::CompositeGlyphRasterSolidCopy(broken, surface, 16);
	},
	    "group flag");
}

int main() {
	try {
		TestNativeDefaultWhitePixelsAndRetainedInputs();
		TestFillRuleAndKernelOpacityPaths();
		TestNativeMultipleBandContinuity();
		TestStridedByteStorageAndSentinels();
		TestInvalidSurfaceStyleAndAreaStorage();
		std::cout << "Text glyph raster solid-copy tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << "Text glyph solid-copy failure: " << error.what() << '\n';

		return 1;
	}
}
