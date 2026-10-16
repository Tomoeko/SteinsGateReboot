#include "sgre/script/text_font_info.hpp"

#include "glyph_atlas_owner_fixture.hpp"
#include "sgre/text/vector_glyph_draw.hpp"
#include "vector_font_fixture.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
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

	throw std::runtime_error("FontInfo real atlas coupling fixture expected failure");
}

static sgre::script::SquirrelValue CompleteSource(sgre::script::SquirrelVm& vm, sgre::script::ScriptVectorFontBindings& fonts) {
	const std::array<sgre::script::SquirrelValue, 2> arguments{vm.RootTable(), std::string("font.otf")};
	const auto source = vm.Invoke(fonts.VectorFontClass(), arguments).Value;
	const std::array<sgre::script::SquirrelValue, 1> receiver{source};
	static_cast<void>(vm.Invoke(vm.GetSlot(source, std::string("getLoading")), receiver));

	return source;
}

static void TestActualInterfaceCachesShareOneOwnedAtlasAndRetainEmptyResults() {
	sgre::script::SquirrelVm vm;
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptPsbBindings psb(vm, objects);
	const auto bytes = std::make_shared<const std::vector<std::uint8_t>>(vector_font_test::MakeFont());
	sgre::script::ScriptVectorFontBindings vectors(vm, objects, [bytes](auto, auto) {
		return [bytes]() -> std::optional<sgre::script::ScriptResourceAsset> {
			sgre::script::ScriptResourceAsset asset;
			asset.Kind = sgre::script::ScriptResourceDataKind::Binary;
			asset.Backing = bytes;

			return asset;
		};
	},
	    [](auto, auto) {
	    });
	sgre::scene::TextureCache bitmap_cache([](auto, const auto&) -> sgre::render::SpriteTextureHandle {
		throw std::runtime_error("vector atlas fixture must not load bitmap sources");
	},
	    [](auto) {
	    });
	sgre::scene::BitmapFontAssetCatalog bitmap_fonts(bitmap_cache);
	const auto source = CompleteSource(vm, vectors);
	text_atlas_test::Backend backend;
	sgre::text::TextGlyphAtlasOwner atlas(backend);
	sgre::text::TextGlyphAtlasOwner foreign_atlas(backend);
	sgre::script::ScriptTextFontInfo first(vm, objects, psb, vectors, bitmap_fonts, bitmap_cache, source, {});
	sgre::script::ScriptTextFontInfo second(vm, objects, psb, vectors, bitmap_fonts, bitmap_cache, source, {});
	first.SetSize(24);
	second.SetSize(24);
	std::array<sgre::text::TextRunGlyph, 4> run{};
	run[0].GlyphIndex = 0;
	run[1].GlyphIndex = 1;
	run[2].GlyphIndex = 0;
	run[3].GlyphIndex = 2;
	run[0].Position.Y = 0.5f;
	first.PrepareVectorRun(atlas, run);
	Require(first.VectorCacheCounts().GlyphEntries == 3 && atlas.State().Packing.PenXPixels == 52 && atlas.State().Packing.PenYPixels == 0 && backend.Observed.Creates == 1 && vm.RetainedReferenceCount(source) == 2, "real FontInfo cache must prepare distinct actual glyphs once and retain the empty glyph without extra atlas/resource ownership");
	first.PrepareVectorRun(atlas, run);
	Require(atlas.State().Packing.PenXPixels == 52, "cache hits must not rerasterize or advance the actual fixed atlas");
	std::array<sgre::text::CachedVectorGlyphDraw, 4> draws{};
	const auto count = first.PrepareVectorDrawRun({}, 0x12345678, run, draws);
	Require(count == 3 && draws[0].Region.AtlasLeftPixels == 1 && draws[1].Region.AtlasLeftPixels == 1 && draws[2].Region.AtlasLeftPixels == 27 && draws[0].Region.WidthPixels == 24 && draws[0].Position.Y == 24.5f && backend.Observed.Uploads == 0 && backend.Observed.Binds == 0, "real vector cache draw lookup must reuse stored regions and skip the empty glyph without upload/submission");
	const auto rectangle = sgre::text::PrepareVectorGlyphDrawRectangle(draws[0], atlas.Resource() != nullptr);
	Require(rectangle && rectangle->DestinationPixels.Top == 0 && rectangle->DestinationPixels.Bottom == 24 && rectangle->SourcePixels.Left == 1 && rectangle->ColorRgba == 0x12345678, "genuine cached glyph must feed the recovered final rectangle projection");
	second.PrepareVectorRun(atlas, std::span(run).first(1));
	Require(second.VectorCacheCounts().GlyphEntries == 1 && atlas.State().Packing.PenXPixels == 78 && backend.Observed.Creates == 1, "native separate FontInfo interfaces must keep distinct caches while sharing the text owner's single atlas");
	RequireFailure([&] {
		first.PrepareVectorRun(foreign_atlas, run);
	},
	    "one live text atlas");
	RequireFailure([&] {
		first.PrepareVectorRun(atlas, run, -2);
	},
	    "negative-edge vector glyph pixels");
	Require(first.VectorCacheCounts().GlyphEntries == 3 && foreign_atlas.Resource() == nullptr, "unsupported stroke or foreign context must not replace actual cached regions or create synthetic assets");
	first.SetSize(510);
	first.PrepareVectorRun(atlas, std::span(run).first(1));
	Require(atlas.State().Packing.PenXPixels == 590 && atlas.State().Packing.PenYPixels == 0, "real CFF large glyph did not preserve current text atlas packing");
	std::array<sgre::text::TextRunGlyph, 1> full_run{};
	full_run[0].GlyphIndex = 2;
	first.PrepareVectorRun(atlas, full_run);
	Require(first.VectorCacheCounts().GlyphEntries == 5 && atlas.State().Packing.PenXPixels == 0 && atlas.State().Packing.PenYPixels == 512 && backend.Observed.Creates == 1, "full native atlas must cache an empty failed glyph result after persistent row wrap without page growth");
	const auto packing = atlas.State().Packing;
	first.PrepareVectorRun(atlas, full_run);
	Require(atlas.State().Packing.PenXPixels == packing.PenXPixels && atlas.State().Packing.PenYPixels == packing.PenYPixels, "failed allocation cache entry was retried");
	Require(first.PrepareVectorDrawRun({}, 0, full_run, std::span(draws).first(1)) == 0, "full-atlas failed result became an invented visible glyph");
	first.Close();
	second.Close();
	bool root_retired_before_texture = false;
	backend.Observed.ReleasingTexture = [&] {
		root_retired_before_texture = vm.RetainedReferenceCount(source) == 0;
	};
	atlas.Close();
	Require(root_retired_before_texture && backend.Observed.ReleasedTextures == 1 && bitmap_cache.Count() == 0, "derived FontInfo source/cache owners must retire before the shared base text atlas texture");
	foreign_atlas.Close();
	vm.Close();
}

static void TestReentrantPreparationAndRetiredAtlasRejectBeforeBorrow(std::int32_t edge_width) {
	sgre::script::SquirrelVm vm;
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptPsbBindings psb(vm, objects);
	const auto bytes = std::make_shared<const std::vector<std::uint8_t>>(vector_font_test::MakeFont());
	sgre::script::ScriptVectorFontBindings vectors(vm, objects, [bytes](auto, auto) {
		return [bytes]() -> std::optional<sgre::script::ScriptResourceAsset> {
			sgre::script::ScriptResourceAsset asset;
			asset.Kind = sgre::script::ScriptResourceDataKind::Binary;
			asset.Backing = bytes;

			return asset;
		};
	},
	    [](auto, auto) {
	    });
	sgre::scene::TextureCache bitmap_cache([](auto, const auto&) -> sgre::render::SpriteTextureHandle {
		throw std::runtime_error("vector atlas guard must not load bitmap sources");
	},
	    [](auto) {
	    });
	sgre::scene::BitmapFontAssetCatalog bitmap_fonts(bitmap_cache);
	const auto source = CompleteSource(vm, vectors);
	text_atlas_test::Backend backend;
	sgre::text::TextGlyphAtlasOwner atlas(backend);
	sgre::script::ScriptTextFontInfo font(vm, objects, psb, vectors, bitmap_fonts, bitmap_cache, source, {});
	font.SetSize(24);
	std::array<sgre::text::TextRunGlyph, 1> run{};
	backend.Observed.Creating = [&] {
		font.Close();
	};
	RequireFailure([&] {
		font.PrepareVectorRun(atlas, run, edge_width);
	},
	    "during glyph preparation");
	Require(font.Kind() == sgre::script::TextFontProviderKind::Vector && font.VectorCacheCounts().GlyphEntries == 0 && vm.RetainedReferenceCount(source) == 1 && !atlas.Resource(), "reentrant preparation must preserve the source root and empty cache without retiring or leaking an interface");
	backend.Observed.Creating = {};
	font.PrepareVectorRun(atlas, run, edge_width);
	Require(font.VectorCacheCounts().GlyphEntries == 1, "rejected reentrant preparation left an unusable interface");
	atlas.Close();
	std::array<sgre::text::CachedVectorGlyphDraw, 1> output{};
	RequireFailure([&] {
		static_cast<void>(font.PrepareVectorDrawRun({}, 0, run, output, edge_width));
	},
	    "retired text atlas");
	font.Close();
	Require(vm.RetainedReferenceCount(source) == 0, "retiring an interface whose borrowed atlas was closed retained a source root");
	vm.Close();
}

static void TestPositiveStrokeCachePixelsAndSourceLifetime() {
	sgre::script::SquirrelVm vm;
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptPsbBindings psb(vm, objects);
	const auto bytes = std::make_shared<const std::vector<std::uint8_t>>(vector_font_test::MakeFont());
	sgre::script::ScriptVectorFontBindings vectors(vm, objects, [bytes](auto, auto) {
		return [bytes]() -> std::optional<sgre::script::ScriptResourceAsset> {
			sgre::script::ScriptResourceAsset asset;
			asset.Kind = sgre::script::ScriptResourceDataKind::Binary;
			asset.Backing = bytes;

			return asset;
		};
	},
	    [](auto, auto) {
	    });
	sgre::scene::TextureCache bitmap_cache([](auto, const auto&) -> sgre::render::SpriteTextureHandle {
		throw std::runtime_error("stroke atlas fixture must not load bitmap sources");
	},
	    [](auto) {
	    });
	sgre::scene::BitmapFontAssetCatalog bitmap_fonts(bitmap_cache);
	const auto source = CompleteSource(vm, vectors);
	text_atlas_test::Backend backend;
	sgre::text::TextGlyphAtlasOwner atlas(backend);
	sgre::script::ScriptTextFontInfo font(vm, objects, psb, vectors, bitmap_fonts, bitmap_cache, source, {});
	font.SetSize(24);
	std::array<sgre::text::TextRunGlyph, 1> run{};
	std::array<sgre::text::CachedVectorGlyphDraw, 1> output{};
	run[0].GlyphIndex = 1;
	font.PrepareVectorRun(atlas, run, 2);
	font.PrepareVectorRun(atlas, run, 2);
	Require(font.VectorCacheCounts().GlyphEntries == 1 && !atlas.Resource() && backend.Observed.Creates == 0 && atlas.State().Packing.PenXPixels == 0, "positive edge cannot turn an empty original glyph into a padded visible allocation");
	Require(font.PrepareVectorDrawRun({}, 0xffffffff, run, output, 2) == 0, "empty positive-edge result must remain an actual cached zero region");
	run[0].GlyphIndex = 0;
	font.PrepareVectorRun(atlas, run, 2);
	Require(font.PrepareVectorDrawRun({}, 0x12345678, run, output, 2) == 1, "positive edge miss must prepare its actual drawable cache record");
	const auto stroke = output[0].Region;
	Require(stroke.LeftPixel == -2 && stroke.TopTranslationPixel == 26 && stroke.WidthPixels == 28 && stroke.HeightPixels == 28 && font.VectorCacheCounts().GlyphEntries == 2, "actual rectangle glyph stroke must preserve native padded bearing and size key");
	const auto packing = atlas.State().Packing;
	font.PrepareVectorRun(atlas, run, 2);
	Require(atlas.State().Packing.PenXPixels == packing.PenXPixels && atlas.State().Packing.PenYPixels == packing.PenYPixels && backend.Observed.Creates == 1 && backend.Observed.Uploads == 0 && backend.Observed.Binds == 0, "positive cache hits must not expand or map again and preparation must not upload or draw");
	Require(atlas.Bind(0, sgre::scene::MutableTextureStage::Pixel), "real positive glyph must use the existing explicit atlas upload");
	const auto alpha = [&](const sgre::text::VectorGlyphAtlasRegion& region, std::int32_t x, std::int32_t y) {
		const auto offset = std::size_t(region.AtlasTopPixels + y) * backend.Observed.LastPitch + std::size_t(region.AtlasLeftPixels + x) * 4 + 3;

		return backend.Observed.LastUpload.at(offset);
	};
	// Active boundary groups preserve the native solid-copy endpoint approximation; white is 254.
	Require(alpha(stroke, 2, 14) == std::byte{254} && alpha(stroke, 14, 14) == std::byte{}, "positive edge must rasterize the actual outline ring rather than substitute the filled glyph");
	font.PrepareVectorRun(atlas, run, 1);
	Require(font.PrepareVectorDrawRun({}, 0xffffffff, run, output, 1) == 1 && output[0].Region.WidthPixels == 26 && output[0].Region.AtlasLeftPixels != stroke.AtlasLeftPixels, "a distinct signed edge key must prepare and retain its own padded native record");
	font.PrepareVectorRun(atlas, run, 0);
	Require(font.PrepareVectorDrawRun({}, 0xffffffff, run, output, 0) == 1 && output[0].Region.WidthPixels == 24, "zero edge must retain the original independent fill cache entry");
	const auto fill = output[0].Region;
	Require(atlas.Bind(0, sgre::scene::MutableTextureStage::Pixel) && alpha(fill, 12, 12) == std::byte{255} && alpha(stroke, 14, 14) == std::byte{}, "later fill preparation must preserve the stored ring and use the same actual text texture");
	Require(font.VectorCacheCounts().GlyphEntries == 4 && backend.Observed.Creates == 1 && vm.RetainedReferenceCount(source) == 1, "three edge keys and one empty record must not create per-entry texture or source owners");
	const auto before_failure = atlas.State().Packing;
	RequireFailure([&] {
		font.PrepareVectorRun(atlas, run, std::numeric_limits<std::int32_t>::max());
	},
	    "signed pixel range");
	Require(font.VectorCacheCounts().GlyphEntries == 4 && atlas.State().Packing.PenXPixels == before_failure.PenXPixels && atlas.State().Packing.PenYPixels == before_failure.PenYPixels && !atlas.State().Texture->Locked, "failed stroke bounds must not commit a glyph or mutate existing atlas packing/mapping");
	vectors.ReleaseInstance(source);
	RequireFailure([&] {
		static_cast<void>(font.PrepareVectorDrawRun({}, 0xffffffff, run, output, 2));
	},
	    "explicitly retired");
	font.Close();
	Require(vm.RetainedReferenceCount(source) == 0, "positive cache retirement must release its exact supplied source root");
	atlas.Close();
	Require(backend.Observed.ReleasedTextures == 1, "one shared text atlas texture must retire once after its positive cache owner");
	vm.Close();
}

int main() {
	try {
		TestActualInterfaceCachesShareOneOwnedAtlasAndRetainEmptyResults();
		TestReentrantPreparationAndRetiredAtlasRejectBeforeBorrow(0);
		TestReentrantPreparationAndRetiredAtlasRejectBeforeBorrow(2);
		TestPositiveStrokeCachePixelsAndSourceLifetime();
		std::cout << "FontInfo actual fill/stroke CFF cache, atlas pixels and owner rollback passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
