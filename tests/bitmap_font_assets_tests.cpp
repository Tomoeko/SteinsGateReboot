#include "sgre/scene/bitmap_font_assets.hpp"

#include "sgre/scene/bitmap_indicator.hpp"

#include <cstdint>
#include <iostream>
#include <memory>
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

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view expected) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(expected) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("bitmap font owner fixture expected failure: " + std::string(expected));
}

template <typename Value>
static sgre::resource::PsbValue Atom(Value value) {
	sgre::resource::PsbValue result;
	result.Data = std::move(value);

	return result;
}

static std::shared_ptr<sgre::resource::PsbDocument> Document() {
	using sgre::resource::PsbArray;
	using sgre::resource::PsbObject;
	PsbArray sources;
	for (std::uint32_t index = 0; index < 2; ++index) {
		sgre::resource::PsbChunkReference chunk;
		chunk.Index = index;
		auto source = Atom(PsbObject{{"type", Atom(std::string("A8"))}, {"width", Atom(std::int64_t{1})}, {"height", Atom(std::int64_t{1})}, {"truncated_width", Atom(std::int64_t{1})}, {"truncated_height", Atom(std::int64_t{1})}, {"pixel", Atom(chunk)}});
		source.SerializedOffset = 64 + index * 32;
		sources.push_back(std::move(source));
	}

	auto glyph = Atom(PsbObject{{"id", Atom(std::int64_t{0})}, {"x", Atom(std::int64_t{0})}, {"y", Atom(std::int64_t{0})}, {"width", Atom(std::int64_t{5})}, {"height", Atom(std::int64_t{6})}, {"w", Atom(std::int64_t{1})}, {"h", Atom(std::int64_t{1})}, {"a", Atom(std::int64_t{2})}, {"b", Atom(std::int64_t{9})}, {"d", Atom(std::int64_t{10})}});
	auto document = std::make_shared<sgre::resource::PsbDocument>();
	document->Header.Version = 4;
	document->Header.RootOffset = 44;
	document->Header.DataOffset = 128;
	document->SerializedSize = 130;
	document->Chunks = {{128, 1}, {129, 1}};
	document->Root = Atom(PsbObject{{"id", Atom(std::string("font"))}, {"spec", Atom(std::string("win"))}, {"version", Atom(1.0800000429153442)}, {"label", Atom(std::string("Synthetic font owner"))}, {"minWidth", Atom(std::int64_t{3})}, {"minHeight", Atom(std::int64_t{6})}, {"maxWidth", Atom(std::int64_t{8})}, {"maxHeight", Atom(std::int64_t{6})}, {"source", Atom(std::move(sources))}, {"code", Atom(PsbObject{{"A", std::move(glyph)}})}});
	document->Root.SerializedOffset = document->Header.RootOffset;

	return document;
}

static std::shared_ptr<std::vector<std::uint8_t>> Backing() {
	auto backing = std::make_shared<std::vector<std::uint8_t>>(130);
	backing->at(128) = 128;
	backing->at(129) = 64;

	return backing;
}

static void TestActualLoadedAssetSharingAndProviderState() {
	std::uint32_t uploads = 0;
	std::vector<std::uint32_t> releases;
	sgre::scene::TextureCache cache([&](auto format, const auto& image) {
		Require(format == sgre::resource::PixelFormat::Alpha8 && image.Width == 1 && image.Height == 1 && image.Pixels == std::vector<std::uint8_t>{0, 0, 0, uploads == 0 ? std::uint8_t{128} : std::uint8_t{64}}, "font source decoding must preserve actual native sampled A8 channels");

		return sgre::render::SpriteTextureHandle{++uploads, 1, &uploads};
	},
	    [&](auto texture) {
		    releases.push_back(texture.Index);
	    });
	sgre::scene::BitmapFontAssetCatalog catalog(cache);
	const auto document = Document();
	const auto asset = catalog.Prepare(document, Backing());
	Require(catalog.Prepare(document, {}) == asset && asset->Model->Glyphs.size() == 1 && asset->Model->Sources.size() == 2 && catalog.DocumentCount() == 1, "one immutable loaded document must share its actual prepared font model");
	sgre::scene::BitmapFontTextureOwner first(cache, asset);
	sgre::scene::BitmapFontTextureOwner second(cache, asset);
	const auto first_source = first.Source(0);
	const auto second_source = second.Source(0);
	const auto texture = first_source.Texture();
	Require(uploads == 2 && second_source.Texture() == texture && cache.Get(texture).References == 2 && !first_source.SameIdentity(second_source), "separate native font providers must own separate wrappers while sharing the same uploaded document/value texture");
	first.SetFiltering(1, 1);
	Require(first.Minification(0) == 1 && first.Magnification(1) == 1 && second.Minification(0) == 0 && cache.Get(texture).Minification == 1, "font filtering must update its ordered local wrappers and their shared physical texture without rewriting another wrapper's local state");
	sgre::scene::BitmapIndicator indicator(asset->Model);
	indicator.Print("A");
	Require(indicator.Placement().Size() == sgre::scene::LayerPoint{5, 6}, "shared immutable font metadata must preserve actual Indicator nominal measurement");
	first.Close();
	Require(first.SourceCount() == 0 && !first_source.IsLive() && second_source.IsLive() && cache.Get(texture).References == 1 && releases.empty(), "intermediate provider retirement must expire only its own wrappers and preserve shared GPU references");
	second.Close();
	Require(!second_source.IsLive() && cache.Count() == 0 && releases == std::vector<std::uint32_t>{1, 2}, "final font provider must retire textures in original source order");
	RequireFailure([&] {
		static_cast<void>(second.Asset());
	},
	    "retired");
}

static void TestCatalogLifetimeIdentityCapacityAndCacheScope() {
	std::uint32_t uploads = 0;
	sgre::scene::TextureCache cache([&](auto, const auto&) {
		return sgre::render::SpriteTextureHandle{++uploads, 1, &uploads};
	},
	    [](auto) {
	    });
	sgre::scene::BitmapFontAssetCatalog catalog(cache, 1);
	auto document = Document();
	const auto backing = Backing();
	auto asset = catalog.Prepare(document, backing);
	const auto identity = asset->DocumentIdentity;
	asset.reset();
	RequireFailure([&] {
		static_cast<void>(catalog.Prepare(document, {}));
	},
	    "complete retained");
	asset = catalog.Prepare(document, backing);
	Require(asset->DocumentIdentity == identity, "repreparing metadata for a still-loaded document must retain its actual cache identity");
	const auto another_document = Document();
	RequireFailure([&] {
		static_cast<void>(catalog.Prepare(another_document, Backing()));
	},
	    "capacity");
	sgre::scene::TextureCache foreign([](auto, const auto&) {
		return sgre::render::SpriteTextureHandle{};
	},
	    [](auto) {
	    });
	RequireFailure([&] {
		sgre::scene::BitmapFontTextureOwner wrong(foreign, asset);
	},
	    "actual cache scope");
	const auto weak_document = std::weak_ptr<const sgre::resource::PsbDocument>(document);
	asset.reset();
	document.reset();
	catalog.Prune();
	Require(weak_document.expired() && catalog.DocumentCount() == 0, "catalog bookkeeping must not retain decoded documents after actual owners expire");
	const auto replacement = catalog.Prepare(another_document, Backing());
	Require(replacement->DocumentIdentity != identity, "a new loaded document must not recycle a disposed cache-scoped identity");
}

static void TestCompletePreparationRollback() {
	std::uint32_t attempts = 0;
	std::vector<std::uint32_t> releases;
	sgre::scene::TextureCache cache([&](auto, const auto&) {
		if (++attempts == 2) {
			throw std::runtime_error("prepared second upload failure");
		}

		return sgre::render::SpriteTextureHandle{attempts, 1, &attempts};
	},
	    [&](auto texture) {
		    releases.push_back(texture.Index);
	    });
	sgre::scene::BitmapFontAssetCatalog catalog(cache);
	const auto asset = catalog.Prepare(Document(), Backing());
	RequireFailure([&] {
		sgre::scene::BitmapFontTextureOwner provider(cache, asset);
	},
	    "second upload failure");
	Require(attempts == 2 && cache.Count() == 0 && releases == std::vector<std::uint32_t>{1}, "failed preparation must retire each acquired source while preserving the initiating error");
}

static void TestFailureCompletingSourceOrderAndBackingLifetime() {
	std::uint32_t uploads = 0;
	std::vector<std::uint32_t> releases;
	std::weak_ptr<const sgre::resource::PsbDocument> weak_document;
	std::weak_ptr<const std::vector<std::uint8_t>> weak_backing;
	sgre::scene::MotionTextureWrapper::Borrowed first_source;
	sgre::scene::TextureCache cache([&](auto, const auto&) {
		return sgre::render::SpriteTextureHandle{++uploads, 1, &uploads};
	},
	    [&](auto texture) {
		    Require(!weak_document.expired() && !weak_backing.expired(), "font model/document/backing must survive all GPU disposal callbacks");
		    releases.push_back(texture.Index);
		    if (texture.Index == 1) {
			    throw std::runtime_error("first source release failure");
		    }

		    Require(!first_source.IsLive() && !first_source.SameIdentity(first_source), "each source wrapper must dispose immediately after its own GPU release, before the next source callback");
	    });
	sgre::scene::BitmapFontAssetCatalog catalog(cache);
	std::unique_ptr<sgre::scene::BitmapFontTextureOwner> provider;
	{
		auto document = Document();
		auto backing = Backing();
		weak_document = document;
		weak_backing = backing;
		provider = std::make_unique<sgre::scene::BitmapFontTextureOwner>(cache, catalog.Prepare(std::move(document), std::move(backing)));
	}

	first_source = provider->Source(0);
	RequireFailure([&] {
		provider->Close();
	},
	    "first source release failure");
	Require(releases == std::vector<std::uint32_t>{1, 2} && cache.Count() == 0 && provider->SourceCount() == 0 && weak_document.expired() && weak_backing.expired(), "authored close failure must retire all sources in native order before document/backing disposal and first-error reporting");
	provider->Close();
	RequireFailure([&] {
		provider->SetFiltering(0, 0);
	},
	    "live owner");
	RequireFailure([&] {
		static_cast<void>(provider->Source(0));
	},
	    "live provider");
}

int main() {
	try {
		TestActualLoadedAssetSharingAndProviderState();
		TestCatalogLifetimeIdentityCapacityAndCacheScope();
		TestCompletePreparationRollback();
		TestFailureCompletingSourceOrderAndBackingLifetime();
		std::cout << "Native bitmap font source owner and immutable asset fixtures passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
