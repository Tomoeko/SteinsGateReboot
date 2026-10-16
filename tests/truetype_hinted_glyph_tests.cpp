#include "sgre/text/truetype_hinted_glyph_font.hpp"

#include "truetype_vector_font_fixture.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
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

static void RequireFailure(const std::function<void()>& operation, std::string_view text) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(text) != std::string_view::npos, "hinted glyph failure lost its producer context");

		return;
	}

	throw std::runtime_error("invalid hinted glyph owner was admitted");
}

static truetype_vector_test::GlyphDefinition Rectangle(std::span<const std::uint8_t> instructions = {}) {
	auto glyph = truetype_vector_test::Rectangle();
	truetype_vector_test::Write16(glyph.Bytes, 12, static_cast<std::uint16_t>(instructions.size()));
	glyph.Bytes.insert(glyph.Bytes.begin() + 14, instructions.begin(), instructions.end());

	return glyph;
}

static std::vector<std::uint8_t> Font(std::span<const truetype_vector_test::GlyphDefinition> glyphs, std::span<const std::uint8_t> font_program = {}) {
	const auto base = truetype_vector_test::MakeFont(glyphs);
	const sgre::resource::SfntFont original(base.Bytes);
	std::map<std::string, std::vector<std::uint8_t>> tables;
	for (const auto& row : original.Tables()) {
		const auto table = *original.FindTable(row.Tag);
		tables[row.Tag] = {table.begin(), table.end()};
	}

	truetype_vector_test::Write16(tables.at("maxp"), 16, 2);
	truetype_vector_test::Write16(tables.at("maxp"), 18, 1);
	truetype_vector_test::Write16(tables.at("maxp"), 20, 8);
	truetype_vector_test::Write16(tables.at("maxp"), 24, 16);
	truetype_vector_test::Write16(tables.at("maxp"), 26, 64);
	truetype_vector_test::Write32(tables.at("head"), 8, 0);
	tables["fpgm"] = {font_program.begin(), font_program.end()};
	tables["prep"] = {};
	tables["cvt "] = {};
	std::vector<std::uint8_t> bytes(12 + tables.size() * 16);
	truetype_vector_test::Write32(bytes, 0, 0x10000);
	truetype_vector_test::Write16(bytes, 4, static_cast<std::uint16_t>(tables.size()));
	std::size_t index = 0;
	std::size_t head = 0;
	for (const auto& [tag, table] : tables) {
		while (bytes.size() % 4 != 0) {
			bytes.push_back(0);
		}

		const auto record = 12 + index++ * 16;
		std::copy(tag.begin(), tag.end(), bytes.begin() + static_cast<std::ptrdiff_t>(record));
		truetype_vector_test::Write32(bytes, record + 4, truetype_vector_test::SumWords(table));
		truetype_vector_test::Write32(bytes, record + 8, static_cast<std::uint32_t>(bytes.size()));
		truetype_vector_test::Write32(bytes, record + 12, static_cast<std::uint32_t>(table.size()));
		if (tag == "head") {
			head = bytes.size();
		}

		bytes.insert(bytes.end(), table.begin(), table.end());
	}

	truetype_vector_test::Write32(bytes, head + 8, 0xb1b0afba - truetype_vector_test::SumWords(bytes));

	return bytes;
}

static truetype_vector_test::GlyphDefinition Composite(bool instructions, bool use_child_metrics = false, std::uint16_t extra_flags = 0) {
	truetype_vector_test::GlyphDefinition glyph;
	glyph.Horizontal.Advance = 500;
	glyph.Bytes = truetype_vector_test::Header(-1, 0, 0, 200, 50);
	for (const auto value : {std::uint16_t(3 | 0x20), std::uint16_t{0}, std::uint16_t{0}, std::uint16_t{0}}) {
		truetype_vector_test::Append16(glyph.Bytes, value);
	}
	const auto flags = static_cast<std::uint16_t>(3 | (instructions ? 0x100 : 0) | (use_child_metrics ? 0x200 : 0) | extra_flags);
	for (const auto value : {flags, std::uint16_t{1}, std::uint16_t{100}, std::uint16_t{0}}) {
		truetype_vector_test::Append16(glyph.Bytes, value);
	}
	if ((extra_flags & 8) != 0) {
		truetype_vector_test::Append16(glyph.Bytes, 0x4000);
	}
	if (instructions) {
		truetype_vector_test::Append16(glyph.Bytes, 5);
		const std::array<std::uint8_t, 5> program{0x00, 0xb1, 0, 32, 0x48};
		glyph.Bytes.insert(glyph.Bytes.end(), program.begin(), program.end());
	}

	return glyph;
}

static void TestRawPointsBeforeRootShift() {
	const std::array glyphs{Rectangle()};
	const auto bytes = Font(glyphs);
	const sgre::resource::SfntFont sfnt(bytes);
	sgre::text::TrueTypeHintedGlyphFont font(sfnt);
	const auto glyph = font.PrepareGlyph(0, 16);
	const std::array<std::array<std::int32_t, 2>, 4> expected{{{0, 0}, {102, 0}, {102, 51}, {0, 51}}};
	Require(glyph.Points.size() == 4 && glyph.ContourEnds == std::vector<std::uint16_t>{3}, "simple hinted outline lost raw points/contour");
	for (std::size_t index = 0; index < 4; ++index) {
		Require(glyph.Points[index].X == expected[index][0] && glyph.Points[index].Y == expected[index][1], "simple loader used completed unhinted postshift as hint input");
	}
	Require(glyph.Phantoms[0][0] == 0 && glyph.Phantoms[1][0] == 192, "simple phantom rounding did not precede root translation");
	Require(glyph.Metrics.Width26Dot6 == 128 && glyph.Metrics.Height26Dot6 == 64 && glyph.Metrics.HorizontalAdvance26Dot6 == 192, "completed glyph metrics did not retain native pixel bounds");
}

static void TestDefinitionDispatchAndOwnedResult() {
	const std::array<std::uint8_t, 9> definitions{0xb0, 3, 0x2c, 0x00, 0xb1, 0, 64, 0x48, 0x2d};
	const std::array<std::uint8_t, 3> call{0xb0, 3, 0x2b};
	const std::array<std::uint8_t, 8> x_definitions{0xb0, 3, 0x2c, 0xb1, 0, 64, 0x48, 0x2d};
	const std::array x_glyphs{Rectangle(call)};
	const auto x_bytes = Font(x_glyphs, x_definitions);
	const sgre::resource::SfntFont x_sfnt(x_bytes);
	sgre::text::TrueTypeHintedGlyphFont x_font(x_sfnt);
	const auto suppressed = x_font.PrepareGlyph(0, 16);
	std::cout << "PROBE version40 X coordinate=" << suppressed.Points[0].X << "," << suppressed.Points[0].Y << " tag=" << unsigned(suppressed.Tags[0]) << '\n';
	Require(suppressed.Points[0].X == 0 && suppressed.Points[0].Y == 0 && (suppressed.Tags[0] & 8) != 0, "native version40 X suppression/touch policy changed");
	sgre::text::TrueTypeHintedGlyph glyph;
	{
		const std::array glyphs{Rectangle(call)};
		const auto bytes = Font(glyphs, definitions);
		const sgre::resource::SfntFont sfnt(bytes);
		sgre::text::TrueTypeHintedGlyphFont font(sfnt);
		glyph = font.PrepareGlyph(0, 16);
	}

	std::cout << "PROBE version40 Y coordinate=" << glyph.Points[0].X << "," << glyph.Points[0].Y << " tag=" << unsigned(glyph.Tags[0]) << '\n';
	Require(glyph.Points[0].X == 0 && glyph.Points[0].Y == 64, "real Y FDEF/CALL did not move the actual source glyph point");
	Require((glyph.Tags[0] & 16) != 0 && (glyph.Tags[0] & 4) != 0, "hinted touch/scan metadata did not survive owned publication");
	Require(glyph.Points[1].X == 102, "definition movement changed an unrelated source point");
}

static void TestPersistentStorageAndSizeReset() {
	const std::array<std::uint8_t, 16> program{0x00, 0xb0, 0, 0x43, 0xb0, 1, 0x60, 0x20, 0xb0, 0, 0x23, 0x42, 0xb0, 0, 0x23, 0x48};
	const std::array glyphs{Rectangle(program)};
	const auto bytes = Font(glyphs);
	const sgre::resource::SfntFont sfnt(bytes);
	sgre::text::TrueTypeHintedGlyphFont font(sfnt);
	const auto first = font.PrepareGlyph(0, 16);
	const auto second = font.PrepareGlyph(0, 16);
	const auto resized = font.PrepareGlyph(0, 17);
	std::cout << "PROBE storage Y first=" << first.Points[0].Y << " second=" << second.Points[0].Y << " resized=" << resized.Points[0].Y << '\n';
	Require(first.Points[0].Y == 1 && second.Points[0].Y == 2, "same-font glyph execution lost persistent storage");
	Require(resized.Points[0].Y == 1, "actual size change did not reset storage before preparation");
	Require(first.Points[0].Y == 1, "later persistent execution mutated an earlier owned result");
	font.SetPixelSize(16);
	font.SetPixelSize(17);
	Require(font.PrepareGlyph(0, 17).Points[0].Y == 1, "intervening applied sizes without glyph preparation lost native size invalidation");
}

static void TestChildBeforeCompositeInstructions() {
	const std::array<std::uint8_t, 5> move{0x00, 0xb1, 0, 64, 0x48};
	const std::array glyphs{Rectangle(move), Rectangle(), Composite(true)};
	const auto bytes = Font(glyphs);
	const sgre::resource::SfntFont sfnt(bytes);
	sgre::text::TrueTypeHintedGlyphFont font(sfnt);
	const auto glyph = font.PrepareGlyph(2, 16);
	std::cout << "PROBE composite first=" << glyph.Points[0].X << "," << glyph.Points[0].Y << " next=" << glyph.Points[4].X << "," << glyph.Points[4].Y << '\n';
	Require(glyph.Points.size() == 8 && glyph.ContourEnds == std::vector<std::uint16_t>({3, 7}), "composite child shape ownership changed");
	Require(glyph.Points[0].X == 0 && glyph.Points[0].Y == 32 && glyph.Points[4].X == 102 && glyph.Points[4].Y == 0, "parent instructions did not execute after genuine child hinting and translation");
	const std::array metric_glyphs{Rectangle(), Rectangle(), Composite(false, true)};
	const auto metric_bytes = Font(metric_glyphs);
	const sgre::resource::SfntFont metric_sfnt(metric_bytes);
	sgre::text::TrueTypeHintedGlyphFont metric_font(metric_sfnt);
	Require(metric_font.PrepareGlyph(2, 16).Phantoms[1][0] == 192, "USE_MY_METRICS did not select the actual child phantom owner");
}

static void TestExplicitBoundaries() {
	const std::array glyphs{Rectangle()};
	const auto bytes = Font(glyphs);
	const sgre::resource::SfntFont sfnt(bytes);
	sgre::text::TrueTypeHintedGlyphFont font(sfnt);
	RequireFailure([&] {
		static_cast<void>(font.PrepareGlyph(0, 0));
	},
	    "positive integral native size");
	RequireFailure([&] {
		static_cast<void>(font.PrepareGlyph(1, 16));
	},
	    "glyph index");
	const std::array transformed{Rectangle(), Rectangle(), Composite(false, false, 8)};
	const auto transformed_bytes = Font(transformed);
	const sgre::resource::SfntFont transformed_sfnt(transformed_bytes);
	sgre::text::TrueTypeHintedGlyphFont transformed_font(transformed_sfnt);
	RequireFailure([&] {
		static_cast<void>(transformed_font.PrepareGlyph(2, 16));
	},
	    "matrix transforms");
	const std::array<std::uint8_t, 1> unrecovered{0x34};
	const std::array unsupported_glyphs{Rectangle(unrecovered)};
	const auto unsupported_bytes = Font(unsupported_glyphs);
	const sgre::resource::SfntFont unsupported_sfnt(unsupported_bytes);
	sgre::text::TrueTypeHintedGlyphFont unsupported_font(unsupported_sfnt);
	RequireFailure([&] {
		static_cast<void>(unsupported_font.PrepareGlyph(0, 16));
	},
	    "requires recovered interpreter execution");
}

int main() {
	try {
		TestRawPointsBeforeRootShift();
		TestDefinitionDispatchAndOwnedResult();
		TestPersistentStorageAndSizeReset();
		TestChildBeforeCompositeInstructions();
		TestExplicitBoundaries();
		std::cout << "TrueType hinted glyph tests passed (5 groups)\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
