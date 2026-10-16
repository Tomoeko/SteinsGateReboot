#include "sgre/text/bitmap_element_draw.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

static void Require(bool value, std::string_view message) {
	if (!value) {
		throw std::runtime_error(std::string(message));
	}
}

static sgre::resource::BitmapFont Font() {
	sgre::resource::BitmapFont font;
	font.MaxWidth = 8;
	font.MaxHeight = 12;
	font.Sources.resize(2);
	for (auto& source : font.Sources) {
		source.Width = 16;
		source.Height = 16;
	}
	sgre::resource::BitmapGlyph first;
	first.SourceIndex = 0;
	first.NominalWidth = 5;
	first.NominalHeight = 6;
	first.InkWidth = 2;
	first.InkHeight = 4;
	first.VerticalBearing = 1;
	font.Glyphs.emplace("A", first);
	auto second = first;
	second.SourceIndex = 1;
	second.NominalWidth = 2;
	second.NominalHeight = 9;
	second.InkWidth = 3;
	second.InkHeight = 1;
	second.VerticalBearing = 2;
	font.Glyphs.emplace("B", second);

	return font;
}

static void TestActualElementOrderLineAlignmentAndOpacity() {
	const auto font = Font();
	std::array<sgre::text::IndicatorTextElement, 4> elements;
	elements[0].CodeUnits = {'A', 0x3099};
	elements[0].Color = 0x11223380;
	elements[1].CodeUnits = {'M'};
	elements[2].CodeUnits = {'\n'};
	elements[3].CodeUnits = {'B'};
	elements[3].Color = 0xaabbccff;
	elements[3].Scale = 0.5f;
	std::array<sgre::text::TextCursor, 4> positions;
	std::array<float, 5> lines;
	sgre::text::BitmapElementDrawScratch scratch;
	scratch.Positions = positions;
	scratch.LineWidths = lines;
	std::vector<sgre::text::BitmapGlyphCommand> glyphs;
	sgre::text::EmitBitmapElementGlyphs(font, elements, {10, 20}, {2, 3}, 128, 1, 100, scratch, [&](const auto& glyph) {
		glyphs.push_back(glyph);
	});
	Require(glyphs.size() == 2 && glyphs[0].Font == &font && glyphs[0].TextureSourceIndex == 0 && glyphs[1].TextureSourceIndex == 1, "bitmap font draw must preserve real first-unit lookup/source order and skip missing glyphs");
	Require(positions[0].X == 10 && positions[1].X == 17 && positions[3].X == 10 && positions[3].Y == 29 && lines[0] == 7 && lines[1] == 1, "native two-pass positions/line widths must use nominal glyph metrics and spacing order");
	Require(glyphs[0].Vertices[0].Position.X == 56.5f && glyphs[0].Vertices[0].Position.Y == 19 && glyphs[0].Vertices[3].Position.X == 58.5f && glyphs[0].Vertices[3].Position.Y == 23, "centered first line ink geometry must retain native bearing and fractional alignment");
	Require(glyphs[1].Vertices[0].Position.X == 59.5f && glyphs[1].Vertices[0].Position.Y == 28 && glyphs[1].Vertices[3].Position.X == 61 && glyphs[1].Vertices[3].Position.Y == 28.5f, "second-line scaled glyph must use its own source/nominal height and actual baseline");
	Require(glyphs[0].Vertices[0].Color == 0x11223340 && glyphs[1].Vertices[0].Color == 0xaabbcc80 && glyphs[0].Indices == std::array<std::uint32_t, 6>{0, 1, 2, 1, 3, 2}, "bitmap opacity changes only native low alpha before exact triangle-list emission");
	glyphs.clear();
	sgre::text::EmitBitmapElementGlyphs(font, elements, {10, 20}, {2, 3}, 255, 2, 100, scratch, [&](const auto& glyph) {
		glyphs.push_back(glyph);
	});
	Require(glyphs[0].Vertices[0].Position.X == 103 && glyphs[1].Vertices[0].Position.X == 109 && glyphs[0].Vertices[0].Color == 0x11223380, "right alignment and unmodified opacity255 must preserve original words");
	std::size_t visits = 0;
	sgre::text::EmitBitmapElementGlyphs(font, {}, {}, {}, 255, 0, 0, scratch, [&](const auto&) {
		++visits;
	});
	Require(visits == 0, "empty displayed list must not invent glyph commands");
	bool rejected = false;
	scratch.Positions = {};
	try {
		sgre::text::EmitBitmapElementGlyphs(font, elements, {}, {}, 255, 0, 0, scratch, [&](const auto&) {
			throw std::runtime_error("invalid storage reached the actual glyph receiver");
		});
	} catch (const std::runtime_error& error) {
		rejected = std::string_view(error.what()).find("storage") != std::string_view::npos;
	}
	Require(rejected, "insufficient prepared storage must reject before emission");
}

int main() {
	try {
		TestActualElementOrderLineAlignmentAndOpacity();
		std::cout << "bitmap element native-derived draw order/alignment/opacity and exact glyph command checks passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << "bitmap element draw test error: " << error.what() << '\n';

		return 1;
	}
}
