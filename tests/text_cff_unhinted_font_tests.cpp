#include "sgre/text/cff_glyph_font.hpp"

#include "vector_font_fixture.hpp"

#include <functional>
#include <iostream>
#include <memory>
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
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("CFF unhinted owner fixture expected failure");
}

static void TestActualRawProgramsAndMetrics() {
	const auto bytes = vector_font_test::MakeFont();
	const sgre::resource::SfntFont sfnt(bytes);
	const sgre::text::CffGlyphFont font(sfnt);
	const auto plain = font.PrepareUnhintedGlyph(0, 24);
	const auto raster = font.PrepareGlyph(0, 24);
	Require(plain.GlyphIndex == 0 && plain.PixelSize == 24 && plain.Outline.Points.size() == 4 && plain.Outline.ContourEnds == std::vector<std::uint16_t>{3}, "real no-hint owner must execute actual programs and own native contour geometry");
	const auto& metrics = plain.Outline.Metrics;
	Require(metrics.Width26Dot6 == 1536 && metrics.Height26Dot6 == 1536 && metrics.HorizontalAdvance26Dot6 == 1536 && metrics.VerticalAdvance26Dot6 == 1536, "real no-hint table metrics must override Private charstring width");
	Require(metrics.VerticalSideBearingFontUnits == 100 && metrics.VerticalOriginX26Dot6 == 768 && metrics.VerticalOriginY26Dot6 == 1636 && raster.Metrics.VerticalBearingY26Dot6 == 64, "flags-2 origin must preserve unscaled table bearing, independent of hinted flag8 raster publication");
	const auto empty = font.PrepareUnhintedGlyph(1, 24);
	Require(empty.Outline.Points.empty() && empty.Outline.ContourEnds.empty() && empty.Outline.Metrics.HorizontalAdvance26Dot6 == 1536 && empty.Outline.Metrics.VerticalOriginX26Dot6 == 768 && empty.Outline.Metrics.VerticalOriginY26Dot6 == 100, "empty glyph keeps actual table advance and no-hint origin, not a fabricated empty callback");
	const auto subroutine = font.PrepareUnhintedGlyph(2, 24);
	Require(subroutine.Outline.Points.size() == 4 && subroutine.Outline.Metrics.VerticalOriginY26Dot6 == 1636 && plain.Outline.Points[1].X26Dot6 == 1536, "local program must execute while earlier owned points remain stable");
}

static void TestOwnedGeometryAndUnsupportedDomains() {
	sgre::text::CffUnhintedGlyph retained;
	auto bytes = std::make_shared<const std::vector<std::uint8_t>>(vector_font_test::MakeFont());
	const std::weak_ptr<const std::vector<std::uint8_t>> resource = bytes;
	{
		const sgre::resource::SfntFont sfnt(*bytes);
		const sgre::text::CffGlyphFont font(sfnt);
		retained = font.PrepareUnhintedGlyph(0, 12);
		for (const auto size : {0, -1, 2001}) {
			RequireFailure([&]() {
				static_cast<void>(font.PrepareUnhintedGlyph(0, size));
			},
			    "range");
		}
		RequireFailure([&]() {
			static_cast<void>(font.PrepareUnhintedGlyph(3, 24));
		},
		    "range");
	}
	bytes.reset();
	Require(resource.expired() && retained.Outline.Points.size() == 4 && retained.Outline.Points[1].X26Dot6 == 768 && retained.Outline.Metrics.VerticalOriginY26Dot6 == 868, "no-hint result must own points/metrics without an added resource pin");
	const auto no_vertical_bytes = vector_font_test::MakeFont(1000, false);
	const sgre::resource::SfntFont no_vertical(no_vertical_bytes);
	const sgre::text::CffGlyphFont no_vertical_font(no_vertical);
	RequireFailure([&]() {
		static_cast<void>(no_vertical_font.PrepareUnhintedGlyph(0, 24));
	},
	    "original vertical metrics");
}

int main() {
	try {
		TestActualRawProgramsAndMetrics();
		TestOwnedGeometryAndUnsupportedDomains();
		std::cout << "two real owned CFF no-hint program/metric/origin groups pass\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
