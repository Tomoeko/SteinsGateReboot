#include "sgre/text/cff_glyph_atlas.hpp"

#include "glyph_atlas_owner_fixture.hpp"
#include "sgre/text/cff_glyph_stroke_path.hpp"
#include "sgre/text/glyph_atlas_owner.hpp"

#include <array>
#include <cstddef>
#include <functional>
#include <iostream>
#include <limits>
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

static void RequireFailure(const std::function<void()>& operation, std::string_view context) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("CFF stroke wrapper fixture expected a checked rejection");
}

static sgre::text::CffPreparedGlyph RectangleGlyph() {
	sgre::text::CffPreparedGlyph glyph;
	glyph.Outline.Points = {{-144, 416}, {368, 416}, {368, -96}, {-144, -96}};
	glyph.Outline.ContourEnds = {3};

	return glyph;
}

static std::vector<sgre::text::CffGlyphPathCommand> SyntheticContourCommands() {
	// Independent consumer fixture only: these commands are not a substitute stroke expander.
	const std::array<sgre::text::CffGlyphPathPoint, 5> points{{{2, 2}, {11, 2}, {11, 11}, {2, 11}, {2, 2}}};
	std::vector<sgre::text::CffGlyphPathCommand> commands;
	for (std::size_t index = 0; index < points.size(); ++index) {
		sgre::text::CffGlyphPathCommand command;
		command.Kind = index == 0 ? sgre::text::CffGlyphPathCommandKind::MoveTo : sgre::text::CffGlyphPathCommandKind::LineTo;
		command.Points[0] = points[index];
		commands.push_back(command);
	}

	return commands;
}

static void TestOriginalBoundsPaddingAndTranslatedOwnedPath() {
	auto glyph = RectangleGlyph();
	const auto input = sgre::text::CffGlyphStrokeInput::Prepare(glyph, 2);
	Require(input.has_value(), "nonempty glyph did not retain its original stroke input");
	const auto& plan = input->BitmapPlan();
	Require(plan.LeftPixel == -5 && plan.TopPixel == -9 && plan.WidthPixels == 13 && plan.HeightPixels == 13 && plan.PathTranslationPixels.X == 5 && plan.PathTranslationPixels.Y == 9, "native original floor/ceil bounds must precede two-edge padding and baseline translation");
	Require(input->WidthPixels() == 2, "actual native stroke width must equal the edge word converted to double");
	glyph.Outline.Points.clear();
	glyph.Outline.ContourEnds.clear();
	const auto commands = input->Commands();
	Require(commands.size() == 5 && commands[0].Kind == sgre::text::CffGlyphPathCommandKind::MoveTo && commands[0].Points[0].X == 2.75 && commands[0].Points[0].Y == 2.5 && commands[1].Points[0].X == 10.75 && commands[2].Points[0].Y == 10.5, "stroke input must own the translated actual source rather than borrow released glyph point storage");
	Require(commands.back().Kind == sgre::text::CffGlyphPathCommandKind::LineTo && commands.back().Points[0].X == commands.front().Points[0].X && commands.back().Points[0].Y == commands.front().Points[0].Y, "wrapper must preserve actual CFF explicit endpoint closure without adding a native Close command");
	Require(!sgre::text::CffGlyphStrokeInput::Prepare({}, 2), "empty original path must remain empty before positive edge padding");
	RequireFailure([&] {
		static_cast<void>(sgre::text::CffGlyphStrokeInput::Prepare(RectangleGlyph(), 0));
	},
	    "positive actual edge");
	RequireFailure([&] {
		static_cast<void>(sgre::text::CffGlyphStrokeInput::Prepare(RectangleGlyph(), -1));
	},
	    "positive actual edge");
	RequireFailure([&] {
		static_cast<void>(sgre::text::CffGlyphStrokeInput::Prepare(RectangleGlyph(), std::numeric_limits<std::int32_t>::max()));
	},
	    "signed pixel range");
}

static void TestOwnedContourRasterBeforeAtlasMapAndExplicitUpload() {
	const auto input = sgre::text::CffGlyphStrokeInput::Prepare(RectangleGlyph(), 2);
	auto contours = SyntheticContourCommands();
	const auto raster = sgre::text::PreparedCffGlyphRaster::PrepareStroke(*input, std::move(contours));
	contours.clear();
	text_atlas_test::Backend backend;
	sgre::text::TextGlyphAtlasOwner atlas(backend);
	Require(!atlas.Resource(), "CPU contour preparation must precede creating or mapping an atlas");
	const auto glyph = atlas.PrepareCffRaster(raster);
	Require(glyph && glyph->LeftPixel == -5 && glyph->TopTranslationPixel == 9 && glyph->Allocation.WidthPixels == 13 && glyph->Allocation.HeightPixels == 13 && glyph->Allocation.LeftPixels == 1 && glyph->Allocation.TopPixels == 1, "prepared contour consumer lost the native padded placement record");
	Require(atlas.State().Packing.PenXPixels == 15 && atlas.State().Texture->Dirty && !atlas.State().Texture->Locked && backend.Observed.Uploads == 0 && backend.Observed.Binds == 0, "contour preparation must leave shared CpuShadow unlocked and dirty without upload");
	Require(atlas.Bind(0, sgre::scene::MutableTextureStage::Pixel) && backend.Observed.Uploads == 1, "prepared contour raster requires the actual existing explicit atlas upload");
	const auto& pixels = backend.Observed.LastUpload;
	const auto interior = std::size_t(6) * 4096 + std::size_t(7) * 4;
	const auto padding = std::size_t(1) * 4096 + 4;
	Require(pixels[interior + 3] == std::byte{255} && pixels[padding + 3] == std::byte{}, "owned contour coverage must occupy its measured raster and preserve padding");
	atlas.Close();
}

static void TestEmptyExpandedStrokeKeepsTransparentPlacement() {
	sgre::text::CffPreparedGlyph glyph;
	glyph.Outline.Points = {{0, 0, sgre::resource::CffPointKind::OnCurve}, {128, -128, sgre::resource::CffPointKind::CubicControl}, {128, -128, sgre::resource::CffPointKind::CubicControl}};
	glyph.Outline.ContourEnds = {2};
	const auto input = sgre::text::CffGlyphStrokeInput::Prepare(glyph, 4);
	Require(input.has_value(), "coincident cubic endpoints still require the native positive control bounds and padding");
	auto contours = sgre::text::PrepareCffGlyphStroke(input->Commands(), input->WidthPixels());
	Require(contours.empty(), "native flat coincident-end cubic must yield no expanded contours");
	const auto raster = sgre::text::PreparedCffGlyphRaster::PrepareStroke(*input, std::move(contours));
	Require(raster.Cells().WidthPixels == 10 && raster.Cells().RowCount == 0 && raster.Cells().Deltas.empty() && raster.Cells().ActiveGroups.empty(), "empty actual expansion must pass the ordinary zero-edge area preparation");
	text_atlas_test::Backend backend;
	sgre::text::TextGlyphAtlasOwner atlas(backend);
	Require(!atlas.Resource() && backend.Observed.Creates == 0, "empty expansion must finish geometry preparation before actual atlas allocation");
	const auto region = atlas.PrepareCffRaster(raster);
	Require(region && region->LeftPixel == -4 && region->TopTranslationPixel == 4 && region->Allocation.LeftPixels == 1 && region->Allocation.TopPixels == 1 && region->Allocation.WidthPixels == 10 && region->Allocation.HeightPixels == 10, "empty expanded native stroke must retain its positive transparent bitmap placement");
	Require(atlas.State().Packing.PenXPixels == 12 && atlas.State().Packing.PenYPixels == 0 && atlas.State().Packing.RowHeightPixels == 12 && atlas.State().Texture->Dirty && !atlas.State().Texture->Locked && backend.Observed.Uploads == 0, "transparent stroke region must retain ordinary row advancement and unlocked dirty atlas state");
	Require(atlas.Bind(0, sgre::scene::MutableTextureStage::Pixel) && backend.Observed.Uploads == 1, "transparent allocated region must retain the actual deferred atlas upload");
	for (std::size_t row = 0; row < region->Allocation.HeightPixels; ++row) {
		for (std::size_t column = 0; column < std::size_t(region->Allocation.WidthPixels) * 4; ++column) {
			const auto offset = region->Allocation.DataOffsetBytes + row * region->Allocation.RowPitchBytes + column;
			Require(backend.Observed.LastUpload.at(offset) == std::byte{}, "empty native stroke must leave the allocated color and coverage bytes zero");
		}
	}
	atlas.Close();
}

static void TestInvalidAndMovedStorageRejectBeforeAtlasMutation() {
	auto input = sgre::text::CffGlyphStrokeInput::Prepare(RectangleGlyph(), 2);
	auto invalid = SyntheticContourCommands();
	invalid.front().Points[0].X = -1;
	RequireFailure([&] {
		static_cast<void>(sgre::text::PreparedCffGlyphRaster::PrepareStroke(*input, std::move(invalid)));
	},
	    "clip");
	auto raster = sgre::text::PreparedCffGlyphRaster::PrepareStroke(*input, SyntheticContourCommands());
	auto moved_raster = std::move(raster);
	text_atlas_test::Backend backend;
	sgre::text::TextGlyphAtlasOwner atlas(backend);
	RequireFailure([&] {
		static_cast<void>(atlas.PrepareCffRaster(raster));
	},
	    "storage no longer covers");
	Require(!atlas.Resource() && atlas.State().Packing.PenXPixels == 0 && backend.Observed.Creates == 0, "moved raster must be rejected before texture allocation, row advancement or map");
	Require(atlas.PrepareCffRaster(moved_raster).has_value(), "moved immutable raster must remain a valid owned value");
	auto moved_input = std::move(*input);
	RequireFailure([&] {
		static_cast<void>(sgre::text::PreparedCffGlyphRaster::PrepareStroke(*input, SyntheticContourCommands()));
	},
	    "no longer owns");
	Require(sgre::text::PreparedCffGlyphRaster::PrepareStroke(moved_input, SyntheticContourCommands()).Clip().WidthPixels == 13, "legitimate stroke input move lost its plan or translated path");
	atlas.Close();
}

int main() {
	try {
		TestOriginalBoundsPaddingAndTranslatedOwnedPath();
		TestOwnedContourRasterBeforeAtlasMapAndExplicitUpload();
		TestEmptyExpandedStrokeKeepsTransparentPlacement();
		TestInvalidAndMovedStorageRejectBeforeAtlasMutation();
		std::cout << "Native CFF stroke wrapper bounds, owned contours and atlas preparation passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
