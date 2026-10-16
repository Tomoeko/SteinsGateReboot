#include "sgre/text/glyph_atlas_owner.hpp"

#include "glyph_atlas_owner_fixture.hpp"
#include "vector_font_fixture.hpp"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <iostream>
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
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("glyph atlas owner fixture expected failure");
}

static void TestLazyFixedTextureActualPixelsAndExplicitUpload() {
	text_atlas_test::Backend backend;
	sgre::text::TextGlyphAtlasOwner owner(backend);
	const auto bytes = vector_font_test::MakeFont();
	const sgre::resource::SfntFont sfnt(bytes);
	const sgre::text::CffGlyphFont font(sfnt);
	Require(!owner.Resource() && !owner.State().Texture && backend.Observed.Creates == 0 && !owner.Bind(0, sgre::scene::MutableTextureStage::Pixel), "native text atlas construction must remain lazy");
	Require(!owner.PrepareDefaultCffGlyph(font.PrepareGlyph(1, 24)) && backend.Observed.Creates == 0, "empty actual CFF glyph must not create the native atlas texture");
	const auto first = owner.PrepareDefaultCffGlyph(font.PrepareGlyph(0, 24));
	Require(first && first->Allocation.LeftPixels == 1 && first->Allocation.TopPixels == 1 && first->Allocation.WidthPixels == 24 && first->Allocation.HeightPixels == 24 && first->Allocation.RowPitchBytes == 4096 && first->LeftPixel == 0 && first->TopTranslationPixel == 24, "actual default CFF glyph region differs from its native fixed text atlas");
	const auto state = owner.State();
	Require(state.Texture && state.Texture->Storage == sgre::scene::MutableTextureStorage::CpuShadow && state.Texture->ShadowBytes == 4194304 && state.Texture->CachedRowPitch == 4096 && state.Texture->Dirty && !state.Texture->Locked && backend.Observed.Creates == 1 && backend.Observed.Views == 1 && backend.Observed.Uploads == 0 && backend.Observed.Binds == 0, "glyph preparation must leave actual CpuShadow bytes unlocked/dirty without platform mapping, upload or binding");
	Require(owner.Bind(0, sgre::scene::MutableTextureStage::Pixel) && backend.Observed.Uploads == 1 && backend.Observed.Samplers == 1 && backend.Observed.LastPitch == 4096 && !owner.State().Texture->Dirty, "native atlas upload and sampler creation must occur only at explicit binding");
	for (std::size_t offset = 0; offset < backend.Observed.LastUpload.size(); ++offset) {
		const auto row = offset / 4096;
		const auto column = (offset % 4096) / 4;
		const auto expected = row >= 1 && row <= 24 && column >= 1 && column <= 24 ? (column <= 4 ? std::byte{254} : std::byte{255}) : std::byte{};
		Require(backend.Observed.LastUpload[offset] == expected, "real glyph atlas coverage, full border or safe initialized background differs");
	}

	Require(owner.Bind(0, sgre::scene::MutableTextureStage::Pixel) && backend.Observed.Uploads == 1, "unchanged native atlas must not upload again");
	owner.SetFiltering(true);
	Require(owner.State().Texture->DesiredSampler.FilterWord == 21 && owner.Bind(0, sgre::scene::MutableTextureStage::Pixel) && backend.Observed.Samplers == 1 && backend.Observed.LastSampler.FilterWord == 0, "native filtering must preserve the already created sampler without inventing invalidation");
	owner.Close();
	Require(owner.State().Closed && !owner.Resource() && backend.Observed.ReleasedTextures == 1 && backend.Observed.ReleasedViews == 1 && backend.Observed.ReleasedSamplers == 1, "text atlas context must exclusively retire all texture/view/sampler resources");
}

static void TestFullAtlasDoesNotMapOrGrowPages() {
	text_atlas_test::Backend backend;
	sgre::text::TextGlyphAtlasOwner owner(backend);
	const auto bytes = vector_font_test::MakeFont();
	const sgre::resource::SfntFont sfnt(bytes);
	const sgre::text::CffGlyphFont font(sfnt);
	const auto glyph = font.PrepareGlyph(0, 510);
	Require(owner.PrepareDefaultCffGlyph(glyph).has_value() && owner.State().Packing.PenXPixels == 512 && owner.State().Packing.PenYPixels == 0, "first large glyph must fit the fixed native row");
	Require(owner.Bind(0, sgre::scene::MutableTextureStage::Pixel) && !owner.State().Texture->Dirty, "full-atlas fixture failed to clear initial upload state");
	Require(!owner.PrepareDefaultCffGlyph(glyph) && owner.State().Packing.PenXPixels == 0 && owner.State().Packing.PenYPixels == 512 && owner.State().Packing.RowHeightPixels == 0 && !owner.State().Texture->Dirty && !owner.State().Texture->Locked && backend.Observed.Creates == 1, "horizontal wrap must survive strict height equality failure without mapping, dirtying or growing an atlas page");
	owner.Close();
}

static void TestPreparationFailureAndMutationGuards() {
	text_atlas_test::Backend backend;
	sgre::text::TextGlyphAtlasOwner owner(backend);
	const auto bytes = vector_font_test::MakeFont();
	const sgre::resource::SfntFont sfnt(bytes);
	const sgre::text::CffGlyphFont font(sfnt);
	const auto glyph = font.PrepareGlyph(0, 24);
	backend.Observed.FailCreate = true;
	RequireFailure([&] {
		static_cast<void>(owner.PrepareDefaultCffGlyph(glyph));
	},
	    "platform creation failed");
	Require(!owner.Resource() && owner.State().Packing.PenXPixels == 0, "failed atlas texture creation retained a partial texture or advanced packing");
	backend.Observed.FailCreate = false;
	backend.Observed.Creating = [&] {
		owner.Close();
	};
	RequireFailure([&] {
		static_cast<void>(owner.PrepareDefaultCffGlyph(glyph));
	},
	    "another mutation");
	Require(!owner.Resource() && !owner.State().Closed, "reentrant platform callback retired or retained a partial text context");
	backend.Observed.Creating = {};
	Require(owner.PrepareDefaultCffGlyph(glyph).has_value(), "failed preparation did not leave a usable original owner");
	owner.Close();
	RequireFailure([&] {
		static_cast<void>(owner.PrepareDefaultCffGlyph(glyph));
	},
	    "live context");
	owner.Close();
}

int main() {
	try {
		TestLazyFixedTextureActualPixelsAndExplicitUpload();
		TestFullAtlasDoesNotMapOrGrowPages();
		TestPreparationFailureAndMutationGuards();
		std::cout << "Fixed native glyph atlas actual CFF bytes, lazy ownership and upload phases passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
