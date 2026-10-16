#include "sgre/text/vector_font.hpp"

#include "sgre/text/cff_glyph_atlas.hpp"
#include "sgre/text/cff_glyph_stroke_path.hpp"
#include "sgre/text/truetype_glyph_path.hpp"
#include "sgre/text/truetype_hinted_glyph_font.hpp"
#include "sgre/text/vector_font_metric_services.hpp"
#include "truetype_vector_font_fixture.hpp"
#include "vector_font_fixture.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static void RequireFailure(const std::function<void()>& operation, std::string_view expected) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(expected) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("hinted integration accepted an unrecovered operation");
}

static std::vector<std::uint8_t> Font(std::span<const truetype_vector_test::GlyphDefinition> glyphs) {
	const auto base = truetype_vector_test::MakeFont(glyphs);
	const sgre::resource::SfntFont reader(base.Bytes);
	std::map<std::string, std::vector<std::uint8_t>> tables;
	for (const auto& entry : reader.Tables()) {
		const auto source = *reader.FindTable(entry.Tag);
		tables[entry.Tag] = {source.begin(), source.end()};
	}

	truetype_vector_test::Write16(tables.at("maxp"), 16, 2);
	truetype_vector_test::Write16(tables.at("maxp"), 18, 1);
	truetype_vector_test::Write16(tables.at("maxp"), 20, 8);
	truetype_vector_test::Write16(tables.at("maxp"), 24, 16);
	truetype_vector_test::Write16(tables.at("maxp"), 26, 64);
	truetype_vector_test::Write32(tables.at("head"), 8, 0);
	tables["fpgm"] = {};
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

		const auto entry = 12 + index++ * 16;
		std::copy(tag.begin(), tag.end(), bytes.begin() + static_cast<std::ptrdiff_t>(entry));
		truetype_vector_test::Write32(bytes, entry + 4, truetype_vector_test::SumWords(table));
		truetype_vector_test::Write32(bytes, entry + 8, static_cast<std::uint32_t>(bytes.size()));
		truetype_vector_test::Write32(bytes, entry + 12, static_cast<std::uint32_t>(table.size()));
		if (tag == "head") {
			head = bytes.size();
		}
		bytes.insert(bytes.end(), table.begin(), table.end());
	}

	truetype_vector_test::Write32(bytes, head + 8, 0xb1b0afba - truetype_vector_test::SumWords(bytes));

	return bytes;
}

static truetype_vector_test::GlyphDefinition Rectangle(std::span<const std::uint8_t> instructions = {}) {
	auto glyph = truetype_vector_test::Rectangle();
	truetype_vector_test::Write16(glyph.Bytes, 12, static_cast<std::uint16_t>(instructions.size()));
	glyph.Bytes.insert(glyph.Bytes.begin() + 14, instructions.begin(), instructions.end());

	return glyph;
}

static truetype_vector_test::GlyphDefinition Curve() {
	truetype_vector_test::GlyphDefinition glyph;
	glyph.Horizontal = {1000, 0};
	glyph.Vertical = {2000, 0};
	glyph.Bytes = truetype_vector_test::Header(1, 0, -1000, 1000, 1000);
	truetype_vector_test::Append16(glyph.Bytes, 4);
	truetype_vector_test::Append16(glyph.Bytes, 0);
	// One actual conic arc and three lines: on(0,0), off(500,1000),
	// on(1000,0), on(1000,-1000), on(0,-1000). Coordinates use signed16 deltas.
	const std::array<std::uint8_t, 5> flags{1, 0, 1, 1, 1};
	glyph.Bytes.insert(glyph.Bytes.end(), flags.begin(), flags.end());
	for (const auto delta : {std::int16_t{0}, std::int16_t{500}, std::int16_t{500}, std::int16_t{0}, std::int16_t{-1000}, std::int16_t{0}, std::int16_t{1000}, std::int16_t{-1000}, std::int16_t{-1000}, std::int16_t{0}}) {
		truetype_vector_test::AppendSigned16(glyph.Bytes, delta);
	}

	return glyph;
}

static void SamePath(const sgre::text::CffGlyphPath& actual, const sgre::text::CffGlyphPath& expected) {
	Require(actual.Commands.size() == expected.Commands.size(), "prepared path command count differs");
	const std::array actual_bounds{actual.Bounds.Left, actual.Bounds.Top, actual.Bounds.Right, actual.Bounds.Bottom};
	const std::array expected_bounds{expected.Bounds.Left, expected.Bounds.Top, expected.Bounds.Right, expected.Bounds.Bottom};
	for (std::size_t index = 0; index < actual_bounds.size(); ++index) {
		Require(std::bit_cast<std::uint64_t>(actual_bounds[index]) == std::bit_cast<std::uint64_t>(expected_bounds[index]), "owned path bounds differ from actual hinted decomposition");
	}
	for (std::size_t index = 0; index < actual.Commands.size(); ++index) {
		Require(actual.Commands[index].Kind == expected.Commands[index].Kind, "owned path command kind changed");
		for (std::size_t point = 0; point < 3; ++point) {
			Require(std::bit_cast<std::uint64_t>(actual.Commands[index].Points[point].X) == std::bit_cast<std::uint64_t>(expected.Commands[index].Points[point].X) && std::bit_cast<std::uint64_t>(actual.Commands[index].Points[point].Y) == std::bit_cast<std::uint64_t>(expected.Commands[index].Points[point].Y), "owned path coordinate bits changed");
		}
	}
}

static std::array<std::int64_t, 13> MetricWords(const sgre::text::CffHintedGlyphMetrics& metrics) {
	return {metrics.Width26Dot6, metrics.Height26Dot6, metrics.HorizontalBearingX26Dot6, metrics.HorizontalBearingY26Dot6, metrics.HorizontalAdvance26Dot6, metrics.VerticalBearingX26Dot6, metrics.VerticalBearingY26Dot6, metrics.VerticalAdvance26Dot6, metrics.LinearHorizontalAdvance16Dot16, metrics.LinearVerticalAdvance16Dot16, metrics.AdvanceX26Dot6, metrics.AdvanceY26Dot6, metrics.OutlineFlags};
}

struct AtlasImage {
	std::vector<std::uint8_t> Pixels;
	sgre::text::GlyphAtlasPacking Packing;
	std::array<std::uint32_t, 6> Placement{};
	bool Locked = false;
	bool Upload = false;
};

static AtlasImage Rasterize(const sgre::text::PreparedCffGlyphRaster& raster) {
	AtlasImage image;
	image.Pixels.resize(64 * 64 * 4);
	image.Packing.WidthPixels = 64;
	image.Packing.HeightPixels = 64;
	sgre::text::GlyphAtlasCpuSurface surface;
	surface.Pixels = image.Pixels;
	surface.RowPitchBytes = 256;
	const auto result = sgre::text::RasterizePreparedCffGlyphInAtlas(raster, image.Packing, surface);
	Require(result.has_value(), "genuine prepared glyph failed its bounded CPU atlas allocation");
	image.Placement = {result->Allocation.LeftPixels, result->Allocation.TopPixels, std::bit_cast<std::uint32_t>(result->LeftPixel), std::bit_cast<std::uint32_t>(result->TopTranslationPixel), result->Allocation.WidthPixels, result->Allocation.HeightPixels};
	image.Locked = surface.Locked;
	image.Upload = surface.UploadRequired;

	return image;
}

static void SameRaster(const sgre::text::PreparedCffGlyphRaster& actual, const sgre::text::PreparedCffGlyphRaster& expected) {
	Require(actual.Cells().WidthPixels == expected.Cells().WidthPixels && actual.Cells().FirstRowPixel == expected.Cells().FirstRowPixel && actual.Cells().RowCount == expected.Cells().RowCount && actual.Cells().Deltas == expected.Cells().Deltas && actual.Cells().ActiveGroups == expected.Cells().ActiveGroups, "shared raster changed exact coverage cells");
	const auto left = Rasterize(actual);
	const auto right = Rasterize(expected);
	Require(left.Pixels == right.Pixels && left.Placement == right.Placement && left.Packing.PenXPixels == right.Packing.PenXPixels && left.Packing.PenYPixels == right.Packing.PenYPixels && left.Packing.RowHeightPixels == right.Packing.RowHeightPixels && !left.Locked && !right.Locked && left.Upload && right.Upload, "shared raster changed atlas bytes, placement or owner state");
}

static void TestHintedCurveThroughOwnedPathAndFill() {
	const std::array glyphs{Curve()};
	const auto bytes = std::make_shared<const std::vector<std::uint8_t>>(Font(glyphs));
	const sgre::resource::SfntFont sfnt(*bytes);
	sgre::text::TrueTypeHintedGlyphFont leaf(sfnt);
	const auto hinted = leaf.PrepareGlyph(0, 16);
	const auto expected_path = sgre::text::PrepareTrueTypeGlyphPath(hinted.Points, hinted.ContourEnds);
	sgre::text::VectorFont font(bytes);
	font.SetSize(16);
	const auto glyph = font.PrepareGlyph(0);
	Require(glyph.PreparedPath && glyph.Outline.Points.empty() && glyph.Outline.ContourEnds.empty(), "TrueType integration fabricated CFF point tags or omitted its owned quadratic path");
	Require(std::ranges::any_of(glyph.PreparedPath->Commands, [](const auto& command) {
		return command.Kind == sgre::text::CffGlyphPathCommandKind::QuadraticTo;
	}),
	    "actual conic glyph lost its quadratic consumer");
	SamePath(*glyph.PreparedPath, expected_path);
	Require(glyph.Metrics.HorizontalAdvance26Dot6 == hinted.Metrics.HorizontalAdvance26Dot6 && glyph.Metrics.AdvanceX26Dot6 == hinted.Metrics.HorizontalAdvance26Dot6 && glyph.Metrics.AdvanceY26Dot6 == 0 && glyph.Metrics.OutlineFlags == hinted.OutlineFlags, "flags8 TrueType metrics were not published through the common prepared record");
	const auto raster = sgre::text::PreparedCffGlyphRaster::Prepare(glyph);
	Require(raster.has_value(), "real conic fill was lost at the shared prepared raster branch");
	const auto image = Rasterize(*raster);
	Require(std::ranges::any_of(image.Pixels, [](auto value) {
		return value != 0;
	}),
	    "real conic fill produced no CPU coverage");
	const auto stroke = sgre::text::CffGlyphStrokeInput::Prepare(glyph, 1);
	Require(stroke.has_value(), "conic stroke wrapper lost its owned source path");
	RequireFailure([&] {
		static_cast<void>(sgre::text::PrepareCffGlyphStroke(stroke->Commands(), stroke->WidthPixels()));
	},
	    "measured move/line/cubic decomposition");
}

static void TestSourceFirstRetirementAndLineStroke() {
	sgre::text::CffPreparedGlyph owned;
	std::weak_ptr<const std::vector<std::uint8_t>> weak;
	{
		auto bytes = std::make_shared<const std::vector<std::uint8_t>>(Font(std::array{Rectangle()}));
		weak = bytes;
		sgre::text::VectorFont font(bytes);
		font.SetSize(48);
		bytes.reset();
		Require(!weak.expired(), "actual VectorFont failed to retain immutable source bytes");
		owned = font.PrepareGlyph(0);
	}
	Require(weak.expired() && owned.PreparedPath && !owned.PreparedPath->Commands.empty(), "returned prepared glyph retained source or lost its independent path");
	const auto snapshot = *owned.PreparedPath;
	const auto fill = sgre::text::PreparedCffGlyphRaster::Prepare(owned);
	const auto stroke = sgre::text::CffGlyphStrokeInput::Prepare(owned, 1);
	Require(fill && stroke, "owned source-first result failed genuine fill/stroke preparation");
	const auto expanded = sgre::text::PrepareCffGlyphStroke(stroke->Commands(), stroke->WidthPixels());
	Require(!expanded.empty(), "actual line-outline stroke emitted no expanded geometry");
	const auto stroked = sgre::text::PreparedCffGlyphRaster::PrepareStroke(*stroke, expanded);
	const auto fill_image = Rasterize(*fill);
	const auto stroke_image = Rasterize(stroked);
	Require(std::ranges::any_of(fill_image.Pixels, [](auto value) {
		return value != 0;
	}) && std::ranges::any_of(stroke_image.Pixels, [](auto value) {
		return value != 0;
	}),
	    "source-first glyph owners produced empty CPU coverage");
	SamePath(*owned.PreparedPath, snapshot);
}

static void TestPersistentHintStateAndActualSizeInvalidation() {
	const std::array<std::uint8_t, 16> program{0x00, 0xb0, 0, 0x43, 0xb0, 1, 0x60, 0x20, 0xb0, 0, 0x23, 0x42, 0xb0, 0, 0x23, 0x48};
	const auto bytes = std::make_shared<const std::vector<std::uint8_t>>(Font(std::array{Rectangle(program)}));
	sgre::text::VectorFont first(bytes);
	sgre::text::VectorFont independent(bytes);
	first.SetSize(16);
	independent.SetSize(16);
	first.BindDefaultMetricServices();
	const auto a = first.PrepareGlyph(0);
	first.SetSize(16.9f);
	const auto b = first.PrepareGlyph(0);
	const auto separate = independent.PrepareGlyph(0);
	Require(a.PreparedPath && b.PreparedPath && separate.PreparedPath && a.PreparedPath->Commands.front().Points[0].Y == -1.0 / 64 && b.PreparedPath->Commands.front().Points[0].Y == -2.0 / 64 && separate.PreparedPath->Commands.front().Points[0].Y == -1.0 / 64, "same-owner storage, unchanged size word or independent same-bytes hint owner changed");
	first.SetSize(17);
	Require(first.MetricServices().HorizontalAdvance(0) > 0, "intermediate flags2-only size lacked its genuine metric producer");
	first.SetSize(16);
	const auto reset = first.PrepareGlyph(0);
	Require(reset.PreparedPath && reset.PreparedPath->Commands.front().Points[0].Y == -1.0 / 64, "flags2-only A-B-A size change failed to invalidate the actual hinted owner");
	first.SetSize(2001);
	first.SetSize(16);
	const auto after_invalid_size = first.PrepareGlyph(0);
	Require(after_invalid_size.PreparedPath && after_invalid_size.PreparedPath->Commands.front().Points[0].Y == -1.0 / 64, "outer invalid-size setter either raised a new error or failed to invalidate preparation");
	Require(a.PreparedPath->Commands.front().Points[0].Y == -1.0 / 64 && b.PreparedPath->Commands.front().Points[0].Y == -2.0 / 64, "later hint execution mutated previously returned path values");
}

static void TestEmptyGlyphAndUnappliedSizeGuards() {
	const auto bytes = std::make_shared<const std::vector<std::uint8_t>>(Font(std::array{truetype_vector_test::GlyphDefinition{}}));
	sgre::text::VectorFont font(bytes);
	RequireFailure([&] {
		static_cast<void>(font.PrepareGlyph(0));
	},
	    "applied character size");
	font.SetSize(16);
	const auto empty = font.PrepareGlyph(0);
	Require(empty.PreparedPath && empty.PreparedPath->Commands.empty() && empty.Outline.Points.empty() && empty.Metrics.Width26Dot6 == 0 && empty.Metrics.Height26Dot6 == 0, "actual empty TrueType glyph acquired fabricated visible geometry");
	Require(!sgre::text::PreparedCffGlyphRaster::Prepare(empty) && !sgre::text::CffGlyphStrokeInput::Prepare(empty, 1), "empty owned path created a fill/stroke allocation");
	RequireFailure([&] {
		static_cast<void>(font.PrepareGlyph(1));
	},
	    "glyph index");
}

static void TestCffBranchAndFallbackRemainExact() {
	const auto bytes = std::make_shared<const std::vector<std::uint8_t>>(vector_font_test::MakeFont());
	const sgre::resource::SfntFont sfnt(*bytes);
	const sgre::text::CffGlyphFont direct(sfnt);
	sgre::text::VectorFont font(bytes);
	font.SetSize(24);
	const auto actual = font.PrepareGlyph(0);
	const auto expected = direct.PrepareGlyph(0, 24);
	Require(!actual.PreparedPath && !expected.PreparedPath && MetricWords(actual.Metrics) == MetricWords(expected.Metrics), "new TrueType branch changed CFF metrics or its absent-path fallback");
	const auto actual_path = sgre::text::PrepareCffGlyphPath(actual.Outline.Points, actual.Outline.ContourEnds);
	const auto expected_path = sgre::text::PrepareCffGlyphPath(expected.Outline.Points, expected.Outline.ContourEnds);
	SamePath(actual_path, expected_path);
	const auto left = sgre::text::PreparedCffGlyphRaster::Prepare(actual);
	const auto right = sgre::text::PreparedCffGlyphRaster::Prepare(expected);
	Require(left && right, "actual CFF parity glyph failed raster preparation");
	SameRaster(*left, *right);
	const auto left_stroke = sgre::text::CffGlyphStrokeInput::Prepare(actual, 1);
	const auto right_stroke = sgre::text::CffGlyphStrokeInput::Prepare(expected, 1);
	Require(left_stroke && right_stroke, "actual CFF parity glyph failed stroke preparation");
	SameRaster(sgre::text::PreparedCffGlyphRaster::PrepareStroke(*left_stroke, sgre::text::PrepareCffGlyphStroke(left_stroke->Commands(), left_stroke->WidthPixels())), sgre::text::PreparedCffGlyphRaster::PrepareStroke(*right_stroke, sgre::text::PrepareCffGlyphStroke(right_stroke->Commands(), right_stroke->WidthPixels())));
}

int main() {
	try {
		TestHintedCurveThroughOwnedPathAndFill();
		TestSourceFirstRetirementAndLineStroke();
		TestPersistentHintStateAndActualSizeInvalidation();
		TestEmptyGlyphAndUnappliedSizeGuards();
		TestCffBranchAndFallbackRemainExact();
		std::cout << "TrueType hinted integration tests passed (5 groups)\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
