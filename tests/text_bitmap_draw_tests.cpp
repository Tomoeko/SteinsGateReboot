#include "sgre/text/bitmap_draw.hpp"

#include <array>
#include <bit>
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
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, "glyph command failure lost its context");

		return;
	}

	throw std::runtime_error("invalid glyph command was accepted");
}

static sgre::resource::BitmapFont MakeFont() {
	sgre::resource::BitmapFont font;
	font.Sources.resize(2);
	font.Sources[0].Width = 8;
	font.Sources[0].Height = 8;
	font.Sources[1].Width = 16;
	font.Sources[1].Height = 16;
	sgre::resource::BitmapGlyph glyph;
	glyph.Codepoint = u'A';
	glyph.SourceIndex = 1;
	glyph.AtlasX = 2;
	glyph.AtlasY = 4;
	glyph.InkWidth = 3;
	glyph.InkHeight = 5;
	glyph.VerticalBearing = 2;
	font.Glyphs.emplace("A", glyph);

	return font;
}

struct NativeGlyphCommandFixture {
	std::uint32_t BatchGlyph = 0;
	std::uint32_t Color = 0;
	std::array<std::uint32_t, 9> Input{};
	std::array<std::uint32_t, 20> Vertices{};
	std::array<std::uint16_t, 6> Indices{};
};

static void TestOriginalGlyphVertexAndIndexBlocks() {
	// Original queue color, emitted vertex, and index instruction blocks produced these words.
	const std::array<NativeGlyphCommandFixture, 3> fixtures = {{
	    {0, 0x630ea6ba, {0x42fa0000, 0x43958000, 0x43750000, 0x43210000, 0xc1e80000, 0x44ae04a1, 0x43573ef2, 0xbffd51e5, 0x40059587}, {0x44ae04a1, 0x4389e35a, 0xba630ea6, 0x3dfa0000, 0x3e958000, 0x44ae04a1, 0x4418f4b6, 0xba630ea6, 0x3dfa0000, 0x3ee60000, 0x4462d192, 0x4389e35a, 0xba630ea6, 0x3eb90000, 0x3e958000, 0x4462d192, 0x4418f4b6, 0xba630ea6, 0x3eb90000, 0x3ee60000}, {0, 1, 2, 1, 3, 2}},
	    {1, 0x661bdc75, {0x43acbf3d, 0x43377a48, 0x43a37679, 0x43299f57, 0xc16e1548, 0xc40e9808, 0x4455e27a, 0x4073c656, 0x406f201c}, {0xc40e9808, 0x4463c8b6, 0x75661bdc, 0x3eacbf3d, 0x3e377a48, 0xc40e9808, 0x44c11ce0, 0x75661bdc, 0x3eacbf3d, 0x3eb08cd0, 0x4428b82a, 0x4463c8b6, 0x75661bdc, 0x3f281adb, 0x3e377a48, 0x4428b82a, 0x44c11ce0, 0x75661bdc, 0x3f281adb, 0x3eb08cd0}, {4, 5, 6, 5, 7, 6}},
	    {3, 0xae5bd1fe, {0x433696f3, 0x4379761f, 0x42619f22, 0x42efe505, 0x40d375dd, 0x44661831, 0xc48bbeb1, 0x3e50d6a9, 0xc057abe1}, {0x44661831, 0xc488f619, 0xfeae5bd1, 0x3e3696f3, 0x3e79761f, 0x44661831, 0xc4bb7cb3, 0xfeae5bd1, 0x3e3696f3, 0x3eb8b451, 0x4468f86b, 0xc488f619, 0xfeae5bd1, 0x3e6efebc, 0x3e79761f, 0x4468f86b, 0xc4bb7cb3, 0xfeae5bd1, 0x3e6efebc, 0x3eb8b451}, {12, 13, 14, 13, 15, 14}},
	}};
	for (const auto& fixture : fixtures) {
		sgre::resource::BitmapFont font;
		font.Sources.resize(1);
		font.Sources[0].Width = 1024;
		font.Sources[0].Height = 1024;
		sgre::resource::BitmapGlyph glyph;
		glyph.AtlasX = std::bit_cast<float>(fixture.Input[0]);
		glyph.AtlasY = std::bit_cast<float>(fixture.Input[1]);
		glyph.InkWidth = std::bit_cast<float>(fixture.Input[2]);
		glyph.InkHeight = std::bit_cast<float>(fixture.Input[3]);
		glyph.VerticalBearing = std::bit_cast<float>(fixture.Input[4]);
		font.Glyphs.emplace("A", glyph);
		sgre::text::TextCursor position;
		position.X = std::bit_cast<float>(fixture.Input[5]);
		position.Y = std::bit_cast<float>(fixture.Input[6]);
		const auto command = sgre::text::PrepareBitmapGlyphCommand(font, u'A', position, std::bit_cast<float>(fixture.Input[7]), std::bit_cast<float>(fixture.Input[8]), fixture.Color);
		Require(command.has_value(), "present native bitmap glyph was omitted");
		for (std::size_t vertex_index = 0; vertex_index < command->Vertices.size(); ++vertex_index) {
			const auto& vertex = command->Vertices[vertex_index];
			const auto native_color = (vertex.Color >> 8) | ((vertex.Color & 0xff) << 24);
			const std::array<std::uint32_t, 5> actual{std::bit_cast<std::uint32_t>(vertex.Position.X), std::bit_cast<std::uint32_t>(vertex.Position.Y), native_color, std::bit_cast<std::uint32_t>(vertex.Uv.X), std::bit_cast<std::uint32_t>(vertex.Uv.Y)};
			for (std::size_t word = 0; word < actual.size(); ++word) {
				Require(actual[word] == fixture.Vertices[vertex_index * 5 + word], "bitmap glyph vertex/color differs from original instructions");
			}
		}

		for (std::size_t index = 0; index < command->Indices.size(); ++index) {
			Require(command->Indices[index] + fixture.BatchGlyph * 4 == fixture.Indices[index], "bitmap glyph list differs from original batch indices");
		}
	}
}

static void TestGeometryAndScopedTextureSource() {
	const auto font = MakeFont();
	sgre::text::TextCursor position;
	position.X = 10;
	position.Y = 20;
	const auto command = sgre::text::PrepareBitmapGlyphCommand(font, u'A', position, 2, 3, 0x12345678);
	Require(command && command->Font == &font && command->TextureSourceIndex == 1, "glyph command lost font-scoped atlas identity");
	const std::array<sgre::resource::MotionPoint, 4> expected_positions{{{10, 14}, {10, 29}, {16, 14}, {16, 29}}};
	const std::array<sgre::resource::MotionPoint, 4> expected_uv{{{0.125f, 0.25f}, {0.125f, 0.5625f}, {0.3125f, 0.25f}, {0.3125f, 0.5625f}}};
	for (std::size_t index = 0; index < command->Vertices.size(); ++index) {
		const auto& vertex = command->Vertices[index];
		Require(vertex.Position.X == expected_positions[index].X && vertex.Position.Y == expected_positions[index].Y && vertex.Uv.X == expected_uv[index].X && vertex.Uv.Y == expected_uv[index].Y && vertex.Color == 0x12345678, "glyph corner/UV/color order differs from native batch layout");
	}

	Require(command->Indices == std::array<std::uint32_t, 6>{0, 1, 2, 1, 3, 2}, "glyph triangle list was replaced by an icon strip");
	const auto mirrored = sgre::text::PrepareBitmapGlyphCommand(font, u'A', position, -2, -3, 0);
	Require(mirrored && mirrored->Vertices[3].Position.X == 4 && mirrored->Vertices[3].Position.Y == 11 && mirrored->Vertices[0].Color == 0, "mirrored font scale or transparent queue color was normalized");
	Require(!sgre::text::PrepareBitmapGlyphCommand(font, u'Z', position, 1, 1, 0xffffffff), "missing native bitmap glyph invented replacement geometry");
}

static void TestSourceIsolationAndSafety() {
	auto font = MakeFont();
	auto other = MakeFont();
	other.Sources[1].Width = 32;
	const sgre::text::TextCursor position;
	const auto first = sgre::text::PrepareBitmapGlyphCommand(font, u'A', position, 1, 1, 0xffffffff);
	const auto second = sgre::text::PrepareBitmapGlyphCommand(other, u'A', position, 1, 1, 0xffffffff);
	Require(first && second && first->Font != second->Font && first->Vertices[0].Uv.X == 0.125f && second->Vertices[0].Uv.X == 0.0625f, "same glyph/source names crossed prepared font ownership");
	font.Glyphs.at("A").SourceIndex = 2;
	RequireFailure([&] {
		static_cast<void>(sgre::text::PrepareBitmapGlyphCommand(font, u'A', position, 1, 1, 0));
	},
	    "font owner");
	font.Glyphs.at("A").SourceIndex = 1;
	RequireFailure([&] {
		static_cast<void>(sgre::text::PrepareBitmapGlyphCommand(font, u'A', position, std::numeric_limits<float>::infinity(), 1, 0));
	},
	    "nonfinite");
}

int main() {
	try {
		TestOriginalGlyphVertexAndIndexBlocks();
		TestGeometryAndScopedTextureSource();
		TestSourceIsolationAndSafety();
		std::cout << "Prepared bitmap glyph commands passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
