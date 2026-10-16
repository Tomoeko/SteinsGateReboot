#include "sgre/resource/truetype_outline.hpp"
#include "sgre/text/truetype_glyph_font.hpp"

#include <algorithm>
#include <array>
#include <bit>
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

static void Require(bool condition, std::string_view context) {
	if (!condition) {
		throw std::runtime_error(std::string(context));
	}
}

static void RequireFailure(const std::function<void()>& operation, std::string_view context) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, "TrueType failure lost its precise producer context");

		return;
	}

	throw std::runtime_error("unsupported or malformed TrueType fixture was accepted");
}

static void Write16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
	bytes.at(offset) = static_cast<std::uint8_t>(value >> 8);
	bytes.at(offset + 1) = static_cast<std::uint8_t>(value);
}

static void Write32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
	Write16(bytes, offset, static_cast<std::uint16_t>(value >> 16));
	Write16(bytes, offset + 2, static_cast<std::uint16_t>(value));
}

static void Append16(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
	bytes.push_back(static_cast<std::uint8_t>(value >> 8));
	bytes.push_back(static_cast<std::uint8_t>(value));
}

static void AppendSigned16(std::vector<std::uint8_t>& bytes, std::int16_t value) {
	Append16(bytes, std::bit_cast<std::uint16_t>(value));
}

static std::uint32_t SumWords(std::span<const std::uint8_t> bytes) {
	std::uint32_t result = 0;
	for (std::size_t offset = 0; offset < bytes.size(); offset += 4) {
		std::uint32_t word = 0;
		for (std::size_t index = 0; index < 4; ++index) {
			word = (word << 8) | (offset + index < bytes.size() ? bytes[offset + index] : 0u);
		}

		result += word;
	}

	return result;
}

struct GlyphDefinition {
	std::vector<std::uint8_t> Bytes;
	sgre::resource::SfntGlyphMetrics Horizontal{200, 10};
	sgre::resource::SfntGlyphMetrics Vertical{300, 20};
};

struct FontFixture {
	std::vector<std::uint8_t> Bytes;
	std::map<std::string, std::size_t> Offsets;
};

static FontFixture MakeFont(std::span<const GlyphDefinition> glyphs, bool vertical = true, bool long_loca = false, std::string_view extra_table = {}) {
	Require(!glyphs.empty() && glyphs.size() <= 128, "fixture glyph count is outside its small bound");
	std::map<std::string, std::vector<std::uint8_t>> tables;
	// One valid Unicode format4 map containing only the sentinel; tests select actual glyph IDs.
	tables["cmap"].resize(36);
	Write16(tables["cmap"], 2, 1);
	Write16(tables["cmap"], 6, 3);
	Write32(tables["cmap"], 8, 12);
	Write16(tables["cmap"], 12, 4);
	Write16(tables["cmap"], 14, 24);
	Write16(tables["cmap"], 18, 2);
	Write16(tables["cmap"], 20, 2);
	Write16(tables["cmap"], 26, 0xffff);
	Write16(tables["cmap"], 30, 0xffff);
	Write16(tables["cmap"], 32, 1);
	tables["head"].resize(54);
	Write32(tables["head"], 0, 0x00010000);
	Write32(tables["head"], 12, 0x5f0f3cf5);
	Write16(tables["head"], 18, 1000);
	Write16(tables["head"], 50, long_loca ? 1 : 0);
	tables["hhea"].resize(36);
	Write32(tables["hhea"], 0, 0x00010000);
	Write16(tables["hhea"], 34, static_cast<std::uint16_t>(glyphs.size()));
	tables["maxp"].resize(32);
	Write32(tables["maxp"], 0, 0x00010000);
	Write16(tables["maxp"], 4, static_cast<std::uint16_t>(glyphs.size()));
	tables["glyf"] = {};
	tables["loca"].resize((glyphs.size() + 1) * (long_loca ? 4u : 2u));
	if (vertical) {
		tables["vhea"].resize(36);
		Write32(tables["vhea"], 0, 0x00010000);
		Write16(tables["vhea"], 34, static_cast<std::uint16_t>(glyphs.size()));
	}

	for (std::size_t index = 0; index < glyphs.size(); ++index) {
		const auto offset = tables["glyf"].size();
		Require(offset <= 65534, "fixture glyf storage exceeds its small bound");
		if (long_loca) {
			Write32(tables["loca"], index * 4, static_cast<std::uint32_t>(offset));
		} else {
			Write16(tables["loca"], index * 2, static_cast<std::uint16_t>(offset / 2));
		}

		tables["glyf"].insert(tables["glyf"].end(), glyphs[index].Bytes.begin(), glyphs[index].Bytes.end());
		if (tables["glyf"].size() % 2 != 0) {
			tables["glyf"].push_back(0);
		}

		Append16(tables["hmtx"], glyphs[index].Horizontal.Advance);
		AppendSigned16(tables["hmtx"], glyphs[index].Horizontal.SideBearing);
		if (vertical) {
			Append16(tables["vmtx"], glyphs[index].Vertical.Advance);
			AppendSigned16(tables["vmtx"], glyphs[index].Vertical.SideBearing);
		}
	}

	if (long_loca) {
		Write32(tables["loca"], glyphs.size() * 4, static_cast<std::uint32_t>(tables["glyf"].size()));
	} else {
		Write16(tables["loca"], glyphs.size() * 2, static_cast<std::uint16_t>(tables["glyf"].size() / 2));
	}
	if (!extra_table.empty()) {
		tables[std::string(extra_table)] = {0};
	}

	FontFixture fixture;
	fixture.Bytes.resize(12 + tables.size() * 16);
	Write32(fixture.Bytes, 0, 0x00010000);
	Write16(fixture.Bytes, 4, static_cast<std::uint16_t>(tables.size()));
	std::size_t index = 0;
	for (const auto& [tag, table] : tables) {
		while (fixture.Bytes.size() % 4 != 0) {
			fixture.Bytes.push_back(0);
		}

		const auto record = 12 + index++ * 16;
		fixture.Offsets[tag] = fixture.Bytes.size();
		std::copy(tag.begin(), tag.end(), fixture.Bytes.begin() + static_cast<std::ptrdiff_t>(record));
		Write32(fixture.Bytes, record + 4, SumWords(table));
		Write32(fixture.Bytes, record + 8, static_cast<std::uint32_t>(fixture.Bytes.size()));
		Write32(fixture.Bytes, record + 12, static_cast<std::uint32_t>(table.size()));
		fixture.Bytes.insert(fixture.Bytes.end(), table.begin(), table.end());
	}

	Write32(fixture.Bytes, fixture.Offsets.at("head") + 8, 0xb1b0afba - SumWords(fixture.Bytes));

	return fixture;
}

static std::vector<std::uint8_t> Header(std::int16_t contours, std::int16_t left, std::int16_t bottom, std::int16_t right, std::int16_t top) {
	std::vector<std::uint8_t> bytes;
	for (const auto value : {contours, left, bottom, right, top}) {
		AppendSigned16(bytes, value);
	}

	return bytes;
}

static GlyphDefinition Rectangle() {
	GlyphDefinition result;
	result.Bytes = Header(1, 0, 0, 100, 50);
	// Four on-curve points: (0,0),(100,0),(100,50),(0,50).
	const std::array<std::uint8_t, 11> payload{0, 3, 0, 0, 0x31, 0x33, 0x35, 0x23, 100, 100, 50};
	result.Bytes.insert(result.Bytes.end(), payload.begin(), payload.end());

	return result;
}

static GlyphDefinition MixedFlags() {
	GlyphDefinition result;
	result.Bytes = Header(1, -295, -265, 20, 40);
	Append16(result.Bytes, 3);
	Append16(result.Bytes, 3);
	const std::array<std::uint8_t, 17> payload{0xb0, 1, 2, 0x3f, 1, 6, 1, 10, 10, 15, 0xfe, 0xd4, 20, 20, 5, 0xfe, 0xd4};
	result.Bytes.insert(result.Bytes.end(), payload.begin(), payload.end());

	return result;
}

static GlyphDefinition Composite(std::uint16_t flags, std::uint16_t child, std::int16_t x, std::int16_t y) {
	GlyphDefinition result;
	result.Bytes = Header(-1, 20, -10, 120, 40);
	Append16(result.Bytes, flags);
	Append16(result.Bytes, child);
	if ((flags & 1) != 0) {
		AppendSigned16(result.Bytes, x);
		AppendSigned16(result.Bytes, y);
	} else {
		result.Bytes.push_back(static_cast<std::uint8_t>(x));
		result.Bytes.push_back(static_cast<std::uint8_t>(y));
	}

	return result;
}

static sgre::text::VectorFontScale Scale(std::int32_t value = 65536) {
	sgre::text::VectorFontScale scale;
	scale.Scale16Dot16 = value;

	return scale;
}

static void RequirePoints(const std::vector<sgre::resource::TrueTypePoint>& actual, std::span<const sgre::resource::TrueTypePoint> expected) {
	Require(actual.size() == expected.size() && std::equal(actual.begin(), actual.end(), expected.begin()), "TrueType points, curve tags or component order changed");
}

static void TestEncodedPointsAndBorrowedInstructions() {
	const std::array<GlyphDefinition, 2> definitions{GlyphDefinition{}, MixedFlags()};
	for (const bool long_loca : {false, true}) {
		const auto fixture = MakeFont(definitions, true, long_loca);
		const sgre::resource::SfntFont sfnt(fixture.Bytes);
		sfnt.ValidateChecksums();
		const sgre::resource::TrueTypeFont font(sfnt);
		const auto glyph = font.ReadGlyph(1);
		const std::array<sgre::resource::TrueTypePoint, 4> points{{{10, 20, sgre::resource::TrueTypePointKind::OnCurve}, {20, 40, sgre::resource::TrueTypePointKind::OnCurve}, {5, 35, sgre::resource::TrueTypePointKind::ConicControl}, {-295, -265, sgre::resource::TrueTypePointKind::OnCurve}}};
		Require(glyph.Kind == sgre::resource::TrueTypeGlyphKind::Simple && glyph.HeaderBounds == sgre::resource::TrueTypeBounds{-295, -265, 20, 40}, "TrueType simple identity or signed header bounds changed");
		RequirePoints(glyph.Points, points);
		Require(glyph.ContourEnds == std::vector<std::uint16_t>{3}, "TrueType contour end did not retain its encoded point index");
		const auto glyf = sfnt.FindTable("glyf");
		Require(glyf && glyph.Instructions.size() == 3 && glyph.Instructions.data() == glyf->data() + 14, "TrueType instruction view lost its actual immutable table owner");
		Require(glyph.Instructions[0] == 0xb0 && glyph.Instructions[1] == 1 && glyph.Instructions[2] == 2, "TrueType outline reader executed or changed unhinted instructions");
		Require(font.ReadGlyph(0).Kind == sgre::resource::TrueTypeGlyphKind::Empty, "zero-length loca range did not remain an empty glyph");
	}
}

static void TestSimpleMetricsAndIndependentAxisScale() {
	const std::array<GlyphDefinition, 1> definitions{Rectangle()};
	const auto fixture = MakeFont(definitions);
	const sgre::resource::SfntFont sfnt(fixture.Bytes);
	const sgre::text::TrueTypeGlyphFont font(sfnt);
	const auto glyph = font.PrepareUnhintedGlyph(0, Scale(), Scale());
	const std::array<sgre::resource::TrueTypePoint, 4> points{{{10, 0}, {110, 0}, {110, 50}, {10, 50}}};
	RequirePoints(glyph.Points, points);
	Require(glyph.ControlBounds == sgre::resource::TrueTypeBounds{10, 0, 110, 50}, "TrueType control bounds did not describe the translated owned outline");
	Require(glyph.Phantoms == std::array<std::array<std::int32_t, 2>, 4>{{{-10, 0}, {190, 0}, {0, 70}, {0, -230}}}, "TrueType horizontal/vertical phantom ownership changed");
	const auto& metrics = glyph.Metrics;
	Require(metrics.Width26Dot6 == 100 && metrics.Height26Dot6 == 50 && metrics.HorizontalBearingX26Dot6 == 10 && metrics.HorizontalBearingY26Dot6 == 50 && metrics.HorizontalAdvance26Dot6 == 200, "TrueType simple horizontal metrics differ from the hand-authored rectangle");
	Require(metrics.VerticalBearingX26Dot6 == -90 && metrics.VerticalBearingY26Dot6 == 20 && metrics.VerticalAdvance26Dot6 == 300 && metrics.VerticalOriginX26Dot6 == 100 && metrics.VerticalOriginY26Dot6 == 70, "TrueType simple vertical metrics differ from the hand-authored phantoms");
	Require(metrics.LinearHorizontalAdvance16Dot16 == 204800 && metrics.LinearVerticalAdvance16Dot16 == 307200, "TrueType explicit scale advance units changed");
	const auto scaled = font.PrepareUnhintedGlyph(0, Scale(98304), Scale(32768));
	const std::array<sgre::resource::TrueTypePoint, 4> scaled_points{{{15, 0}, {165, 0}, {165, 25}, {15, 25}}};
	RequirePoints(scaled.Points, scaled_points);
	Require(scaled.Metrics.Width26Dot6 == 150 && scaled.Metrics.Height26Dot6 == 25 && scaled.Metrics.HorizontalAdvance26Dot6 == 300 && scaled.Metrics.VerticalAdvance26Dot6 == 150, "TrueType axis scales were conflated or silently inferred");
}

static void TestTranslationMetricsAndNestedAssembly() {
	auto component = Composite(0x203, 1, 20, -10);
	component.Horizontal = {800, 30};
	component.Vertical = {900, 40};
	GlyphDefinition parent;
	parent.Bytes = Header(-1, 15, 5, 115, 55);
	Append16(parent.Bytes, 3);
	Append16(parent.Bytes, 2);
	AppendSigned16(parent.Bytes, -5);
	AppendSigned16(parent.Bytes, 15);
	parent.Horizontal = {500, 15};
	parent.Vertical = {700, 25};
	const std::array<GlyphDefinition, 4> definitions{GlyphDefinition{}, Rectangle(), component, parent};
	const auto fixture = MakeFont(definitions);
	const sgre::resource::SfntFont sfnt(fixture.Bytes);
	const sgre::text::TrueTypeGlyphFont font(sfnt);
	const auto child = font.PrepareUnhintedGlyph(2, Scale(), Scale());
	const std::array<sgre::resource::TrueTypePoint, 4> child_points{{{30, -10}, {130, -10}, {130, 40}, {30, 40}}};
	RequirePoints(child.Points, child_points);
	Require(child.Phantoms == std::array<std::array<std::int32_t, 2>, 4>{{{-10, 0}, {190, 0}, {0, 70}, {0, -230}}} && child.Metrics.HorizontalAdvance26Dot6 == 200, "USE_MY_METRICS did not retain the component's unpositioned metric owner");
	const auto nested = font.PrepareUnhintedGlyph(3, Scale(), Scale());
	const std::array<sgre::resource::TrueTypePoint, 4> nested_points{{{15, 5}, {115, 5}, {115, 55}, {15, 55}}};
	RequirePoints(nested.Points, nested_points);
	Require(nested.ContourEnds == std::vector<std::uint16_t>{3} && nested.Phantoms[0][0] == 0 && nested.Phantoms[1][0] == 500, "nested glyph was prematurely shifted into a child root's metric plane");
}

static void TestMultipleComponentsByteArgumentsAndGridRounding() {
	auto multiple = Composite(0x223, 0, -20, 3);
	Append16(multiple.Bytes, 3);
	Append16(multiple.Bytes, 0);
	AppendSigned16(multiple.Bytes, 50);
	AppendSigned16(multiple.Bytes, -7);
	auto byte_offsets = Composite(2, 0, -128, 127);
	auto rounded = Composite(0x207, 0, 64, -64);
	const std::array<GlyphDefinition, 4> definitions{Rectangle(), multiple, byte_offsets, rounded};
	const auto fixture = MakeFont(definitions);
	const sgre::resource::SfntFont sfnt(fixture.Bytes);
	const sgre::resource::TrueTypeFont reader(sfnt);
	const auto parsed = reader.ReadGlyph(2);
	Require(parsed.Components.size() == 1 && parsed.Components[0].Arguments == std::array<std::int32_t, 2>{-128, 127}, "signed-byte XY component offsets were treated as unsigned point indices");
	const sgre::text::TrueTypeGlyphFont font(sfnt);
	const auto glyph = font.PrepareUnhintedGlyph(1, Scale(), Scale());
	const std::array<sgre::resource::TrueTypePoint, 8> points{{{-10, 3}, {90, 3}, {90, 53}, {-10, 53}, {60, -7}, {160, -7}, {160, 43}, {60, 43}}};
	RequirePoints(glyph.Points, points);
	Require(glyph.ContourEnds == std::vector<std::uint16_t>{3, 7}, "component contour rebasing changed point order");
	const auto grid = font.PrepareUnhintedGlyph(3, Scale(32768), Scale(32768));
	const std::array<sgre::resource::TrueTypePoint, 4> grid_points{{{69, 0}, {119, 0}, {119, 25}, {69, 25}}};
	RequirePoints(grid.Points, grid_points);
}

static void TestOwnedResultAndEmptyHeaderMetrics() {
	sgre::text::TrueTypeUnhintedGlyph owned;
	{
		const std::array<GlyphDefinition, 1> definitions{Rectangle()};
		const auto fixture = MakeFont(definitions);
		const sgre::resource::SfntFont sfnt(fixture.Bytes);
		const sgre::text::TrueTypeGlyphFont font(sfnt);
		owned = font.PrepareUnhintedGlyph(0, Scale(), Scale());
	}

	Require(owned.Points.size() == 4 && owned.Points[1].X == 110 && owned.ContourEnds == std::vector<std::uint16_t>{3}, "prepared TrueType result retained borrowed outline storage");
	GlyphDefinition empty;
	empty.Bytes = Header(0, -100, -200, 300, 400);
	const std::array<GlyphDefinition, 1> definitions{empty};
	const auto fixture = MakeFont(definitions);
	const sgre::resource::SfntFont sfnt(fixture.Bytes);
	const sgre::text::TrueTypeGlyphFont font(sfnt);
	const auto glyph = font.PrepareUnhintedGlyph(0, Scale(), Scale());
	Require(glyph.Points.empty() && glyph.ControlBounds == sgre::resource::TrueTypeBounds{}, "empty contour header invented a visible outline");
	Require(glyph.Phantoms == std::array<std::array<std::int32_t, 2>, 4>{{{-10, 0}, {190, 0}, {0, 20}, {0, -280}}}, "empty glyph metrics used non-outline header bounds instead of native zero bounds");
}

static void TestPreciseUnsupportedDomains() {
	auto matrix = Composite(0x0b, 0, 0, 0);
	Append16(matrix.Bytes, 0x2000);
	auto point = Composite(1, 0, -1, -32768);
	const std::array<GlyphDefinition, 3> definitions{Rectangle(), matrix, point};
	const auto fixture = MakeFont(definitions);
	const sgre::resource::SfntFont sfnt(fixture.Bytes);
	const sgre::resource::TrueTypeFont reader(sfnt);
	Require(reader.ReadGlyph(1).Components[0].Matrix == std::array<std::int32_t, 4>{32768, 0, 0, 32768}, "encoded F2Dot14 component scale was not preserved as 16.16 metadata");
	Require(reader.ReadGlyph(2).Components[0].Arguments == std::array<std::int32_t, 2>{65535, 32768}, "component point indices were incorrectly sign extended");
	const sgre::text::TrueTypeGlyphFont font(sfnt);
	RequireFailure([&] {
		static_cast<void>(font.PrepareUnhintedGlyph(1, Scale(), Scale()));
	},
	    "matrix transforms remain unrecovered");
	RequireFailure([&] {
		static_cast<void>(font.PrepareUnhintedGlyph(2, Scale(), Scale()));
	},
	    "point attachment remains unrecovered");
	RequireFailure([&] {
		static_cast<void>(font.PrepareUnhintedGlyph(0, Scale(0), Scale()));
	},
	    "positive actual TT axis scales");
	RequireFailure([&] {
		static_cast<void>(font.PrepareUnhintedGlyph(0, Scale(), Scale(-1)));
	},
	    "positive actual TT axis scales");
	RequireFailure([&] {
		static_cast<void>(font.PrepareUnhintedGlyph(3, Scale(), Scale()));
	},
	    "glyph index exceeds its font owner");
	const auto no_vertical = MakeFont(definitions, false);
	const sgre::resource::SfntFont horizontal_sfnt(no_vertical.Bytes);
	const sgre::text::TrueTypeGlyphFont horizontal_font(horizontal_sfnt);
	RequireFailure([&] {
		static_cast<void>(horizontal_font.PrepareUnhintedGlyph(0, Scale(), Scale()));
	},
	    "requires actual vertical metrics");
	const auto variable = MakeFont(definitions, true, false, "fvar");
	RequireFailure([&] {
		static_cast<void>(sgre::resource::SfntFont(variable.Bytes));
	},
	    "sfnt variable-font metrics are not recovered");
	const auto bitmap = MakeFont(definitions, true, false, "sbix");
	const sgre::resource::SfntFont bitmap_sfnt(bitmap.Bytes);
	RequireFailure([&] {
		static_cast<void>(sgre::text::TrueTypeGlyphFont(bitmap_sfnt));
	},
	    "static outline-only owner");
}

static void TestMalformedRangesAndCompositeCycles() {
	auto repeated = MixedFlags();
	repeated.Bytes[18] = 4;
	auto truncated = Rectangle();
	truncated.Bytes.pop_back();
	auto invalid_contour = Rectangle();
	Write16(invalid_contour.Bytes, 0, 0xfffe);
	auto bad_component = Composite(3, 99, 0, 0);
	const std::array<GlyphDefinition, 4> definitions{repeated, truncated, invalid_contour, bad_component};
	const auto fixture = MakeFont(definitions);
	const sgre::resource::SfntFont sfnt(fixture.Bytes);
	const sgre::resource::TrueTypeFont reader(sfnt);
	RequireFailure([&] {
		static_cast<void>(reader.ReadGlyph(0));
	},
	    "point flag repeat exceeds");
	RequireFailure([&] {
		static_cast<void>(reader.ReadGlyph(1));
	},
	    "short coordinate");
	RequireFailure([&] {
		static_cast<void>(reader.ReadGlyph(2));
	},
	    "invalid negative contour count");
	RequireFailure([&] {
		static_cast<void>(reader.ReadGlyph(3));
	},
	    "component glyph index exceeds");
	const std::array<GlyphDefinition, 1> cyclic{Composite(3, 0, 0, 0)};
	const auto cyclic_fixture = MakeFont(cyclic);
	const sgre::resource::SfntFont cyclic_sfnt(cyclic_fixture.Bytes);
	const sgre::text::TrueTypeGlyphFont cyclic_font(cyclic_sfnt);
	RequireFailure([&] {
		static_cast<void>(cyclic_font.PrepareUnhintedGlyph(0, Scale(), Scale()));
	},
	    "recursive glyph cycle");
	std::vector<GlyphDefinition> deep;
	for (std::uint16_t index = 0; index < 64; ++index) {
		deep.push_back(Composite(3, static_cast<std::uint16_t>(index + 1), 0, 0));
	}

	deep.push_back(Rectangle());
	const auto deep_fixture = MakeFont(deep);
	const sgre::resource::SfntFont deep_sfnt(deep_fixture.Bytes);
	const sgre::text::TrueTypeGlyphFont deep_font(deep_sfnt);
	RequireFailure([&] {
		static_cast<void>(deep_font.PrepareUnhintedGlyph(0, Scale(), Scale()));
	},
	    "recursion or total glyph work");
	const std::array<GlyphDefinition, 1> valid{Rectangle()};
	auto invalid_loca = MakeFont(valid, true, true);
	Write32(invalid_loca.Bytes, invalid_loca.Offsets.at("loca") + 4, 100000);
	const sgre::resource::SfntFont invalid_sfnt(invalid_loca.Bytes);
	RequireFailure([&] {
		static_cast<void>(sgre::resource::TrueTypeFont(invalid_sfnt));
	},
	    "loca entries are decreasing or exceed");
}

int main() {
	try {
		TestEncodedPointsAndBorrowedInstructions();
		TestSimpleMetricsAndIndependentAxisScale();
		TestTranslationMetricsAndNestedAssembly();
		TestMultipleComponentsByteArgumentsAndGridRounding();
		TestOwnedResultAndEmptyHeaderMetrics();
		TestPreciseUnsupportedDomains();
		TestMalformedRangesAndCompositeCycles();
		std::cout << "7 TrueType outline fixture groups passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
