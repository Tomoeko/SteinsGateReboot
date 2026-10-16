#include "sgre/scene/motion_receiving_document.hpp"

#include "motion_receiving_fixture.hpp"
#include "sgre/scene/motion_resources.hpp"

#include <cstdint>
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

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view expected) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(expected) != std::string_view::npos, error.what());
		return;
	}

	throw std::runtime_error("expected motion receiving rejection: " + std::string(expected));
}

static std::vector<std::uint8_t> FixtureBytes() {
	return {kReceivingMotionFixture.begin(), kReceivingMotionFixture.end()};
}

static void TestActualReceivingIdentityAndCatalogPin() {
	std::uint32_t uploads = 0;
	std::vector<std::uint32_t> releases;
	sgre::scene::TextureCache cache([&](auto format, const auto& image) {
		Require(format == sgre::resource::PixelFormat::Alpha8 && image.Width == 1 && image.Height == 1 && image.Pixels == std::vector<std::uint8_t>{0, 0, 0, uploads == 0 ? std::uint8_t{128} : std::uint8_t{64}}, "receiving preloads must decode the two original serialized source atoms in order");
		return sgre::render::SpriteTextureHandle{++uploads, 1, &uploads};
	},
	    [&](auto texture) {
		    releases.push_back(texture.Index);
	    });
	sgre::scene::MotionAssetCatalog catalog(cache);
	auto receiving = std::make_shared<const sgre::scene::MotionReceivingDocument>(cache, FixtureBytes());
	const auto weak_document = std::weak_ptr(receiving);
	const auto source = receiving->Source(0);
	const auto texture = source.Texture();
	Require(receiving->SourceCount() == 2 && receiving->Motion().Sources[0].Name == "atlas_a" && receiving->Motion().Sources[1].Name == "atlas_b", "motion receiving must retain genuine serialized source order");
	Require(receiving->MetadataBytes().size() == receiving->Document().Header.DataOffset && receiving->MetadataBytes().size() + 2 == kReceivingMotionFixture.size() && receiving->Document().SerializedSize == kReceivingMotionFixture.size(), "purged motion must expose only real metadata and preserve original logical supplied size");
	Require(uploads == 2 && cache.Get(texture).References == 1, "motion receiving must own actual preload references before publication");
	auto asset = catalog.Prepare(receiving);
	const auto identity = asset->Identity;
	Require(asset->Document.get() == &receiving->Document() && asset->Model.get() == &receiving->Motion() && asset->Backing.get() == &receiving->MetadataStorage() && identity.DocumentInstance != 0 && identity.RootOffset == receiving->Document().Header.RootOffset, "catalog adoption must preserve exact receiving model/document/metadata and allocated identity");
	Require(asset->Aliases.size() == 1 && asset->Aliases[0].Key == "alias" && asset->Aliases[0].Source == "atlas_a" && asset->Aliases[0].Icon == "piece", "receiving adoption must retain genuine alias metadata");
	Require(catalog.ContainsPrepared(*asset) && catalog.Prepare(receiving) == asset && catalog.Prepare(asset->Document, {}) == asset && catalog.DocumentCount() == 1, "catalog weak bookkeeping must recognize the genuine aliased receiving product without redecoding");
	const auto weak_asset = std::weak_ptr(asset);
	receiving.reset();
	Require(!weak_document.expired() && !weak_asset.expired() && source.IsLive(), "the prepared alias must pin the actual receiving resource and its preload wrappers");
	sgre::scene::MotionResourceManager first(cache);
	sgre::scene::MotionResourceManager second(cache);
	const auto a = first.Register(identity, asset->Model, *asset->Backing);
	const auto b = second.Register(identity, asset->Model, {});
	Require(first.FindSource(a, "atlas_a")->Texture == texture && second.FindSource(b, "atlas_a")->Texture == texture && uploads == 2 && cache.Get(texture).References == 3, "metadata-only manager registration must acquire true preloaded cache hits with the same document/value identity");
	first.Close();
	second.Close();
	Require(cache.Get(texture).References == 1 && releases.empty(), "manager retirement must preserve genuine resource preloads");
	asset.reset();
	Require(weak_document.expired() && weak_asset.expired() && !source.IsLive() && catalog.DocumentCount() == 0 && cache.Count() == 0 && releases == std::vector<std::uint32_t>{1, 2}, "last catalog alias must retire receiving wrappers in increasing native order, then metadata");
	catalog.Prune();
}

static void TestCacheScopeCapacityAndNoFakeFullBacking() {
	std::uint32_t uploads = 0;
	sgre::scene::TextureCache cache([&](auto, const auto&) {
		return sgre::render::SpriteTextureHandle{++uploads, 1, &uploads};
	},
	    [](auto) {
	    });
	std::uint32_t foreign_uploads = 0;
	sgre::scene::TextureCache foreign([&](auto, const auto&) {
		return sgre::render::SpriteTextureHandle{++foreign_uploads, 1, &foreign_uploads};
	},
	    [](auto) {
	    });
	sgre::scene::MotionAssetCatalog catalog(cache, 1);
	sgre::scene::MotionAssetCatalog foreign_catalog(foreign);
	auto first = std::make_shared<const sgre::scene::MotionReceivingDocument>(cache, FixtureBytes());
	RequireFailure([&] {
		static_cast<void>(catalog.Prepare(std::shared_ptr<const sgre::scene::MotionReceivingDocument>{}));
	},
	    "actual cache scope");
	RequireFailure([&] {
		static_cast<void>(foreign_catalog.Prepare(first));
	},
	    "actual cache scope");
	auto prepared = catalog.Prepare(first);
	const auto old_identity = prepared->Identity.DocumentInstance;
	const auto second = std::make_shared<const sgre::scene::MotionReceivingDocument>(cache, FixtureBytes());
	RequireFailure([&] {
		static_cast<void>(catalog.Prepare(second));
	},
	    "capacity");
	sgre::scene::MotionAssetCatalog ordinary(cache);
	RequireFailure([&] {
		static_cast<void>(ordinary.Prepare(prepared->Document, prepared->Backing));
	},
	    "complete retained");
	RequireFailure([&] {
		static_cast<void>(first->Source(2));
	},
	    "source index");
	Require(uploads == 4 && foreign_uploads == 0 && cache.Count() == 4 && foreign.Count() == 0, "equal bytes from separate loaded receiving documents must stay distinct and reject foreign cache adoption before mutation");
	prepared.reset();
	first.reset();
	prepared = catalog.Prepare(second);
	Require(prepared->Identity.DocumentInstance > old_identity && catalog.ContainsPrepared(*prepared), "pruned receiving catalog capacity must preserve the second real allocated identity");
}

static void TestPartialUploadAndFailingReleaseRetireActualOwners() {
	std::uint32_t attempts = 0;
	std::vector<std::uint32_t> releases;
	sgre::scene::TextureCache cache([&](auto, const auto&) {
		if (++attempts == 2) {
			throw std::runtime_error("second motion preload failed");
		}
		return sgre::render::SpriteTextureHandle{attempts, 1, &attempts};
	},
	    [&](auto texture) {
		    releases.push_back(texture.Index);
	    });
	RequireFailure([&] {
		const sgre::scene::MotionReceivingDocument receiving(cache, FixtureBytes());
	},
	    "second motion preload");
	Require(cache.Count() == 0 && cache.UploadedBytes() == 0 && releases == std::vector<std::uint32_t>{1}, "partial preload rollback must release every actual earlier source");
	std::uint32_t uploaded = 0;
	std::vector<std::uint32_t> failed_releases;
	sgre::scene::TextureCache failing([&](auto, const auto&) {
		return sgre::render::SpriteTextureHandle{++uploaded, 1, &uploaded};
	},
	    [&](auto texture) {
		    failed_releases.push_back(texture.Index);
		    throw std::runtime_error("motion texture release failure");
	    });
	{
		const sgre::scene::MotionReceivingDocument receiving(failing, FixtureBytes());
	}
	Require(failing.Count() == 0 && failed_releases == std::vector<std::uint32_t>{1, 2}, "nonthrowing receiving retirement must finish every ordered wrapper after platform release errors");
}

int main() {
	try {
		TestActualReceivingIdentityAndCatalogPin();
		TestCacheScopeCapacityAndNoFakeFullBacking();
		TestPartialUploadAndFailingReleaseRetireActualOwners();
		std::cout << "motion receiving: actual metadata purge, catalog adoption, ordered preloads and manager cache hits passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}

	return 0;
}
