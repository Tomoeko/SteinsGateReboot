#include "sgre/scene/image_receiving_document.hpp"

#include "image_receiving_fixture.hpp"
#include "sgre/scene/image_assets.hpp"

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

	throw std::runtime_error("expected Image receiving rejection: " + std::string(expected));
}

static std::vector<std::uint8_t> FixtureBytes() {
	return {kReceivingImageFixture.begin(), kReceivingImageFixture.end()};
}

static void TestAllEntriesPreloadAndSelectedOwner() {
	std::uint32_t uploads = 0;
	std::vector<std::uint32_t> releases;
	sgre::scene::TextureCache cache([&](auto format, const auto& image) {
		Require(format == sgre::resource::PixelFormat::Alpha8 && image.Width == 1 && image.Height == 1 && image.Pixels == std::vector<std::uint8_t>{0, 0, 0, uploads == 0 ? std::uint8_t{128} : std::uint8_t{64}}, "Image receiving must upload all serialized image entries in tile order");
		return sgre::render::SpriteTextureHandle{++uploads, 1, &uploads};
	},
	    [&](auto texture) {
		    releases.push_back(texture.Index);
	    });
	sgre::scene::ImageAssetCatalog catalog(cache);
	auto receiving = std::make_shared<const sgre::scene::ImageReceivingDocument>(cache, FixtureBytes());
	const auto weak = std::weak_ptr(receiving);
	const auto first_source = receiving->Source(0);
	const auto second_source = receiving->Source(1);
	const auto first_texture = first_source.Texture();
	const auto second_texture = second_source.Texture();
	Require(receiving->SourceCount() == 2 && uploads == 2 && receiving->MetadataBytes().size() + 2 == kReceivingImageFixture.size() && receiving->MetadataBytes().size() == receiving->Document().Header.DataOffset && receiving->Document().SerializedSize == kReceivingImageFixture.size(), "Image preload must retire pixel bytes only after all real textures are uploaded");
	auto first = catalog.Prepare(receiving);
	auto second = catalog.Prepare(receiving, "second");
	Require(first->Model.Tiles.size() == 1 && second->Model.Tiles.size() == 1 && first->DocumentIdentity == receiving->DocumentIdentity() && second->DocumentIdentity == first->DocumentIdentity && first->Backing.get() == &receiving->MetadataStorage() && first->Document.get() == &receiving->Document() && catalog.DocumentCount() == 1, "selected Image assets must retain the real purged document and shared preload cache identity");
	receiving.reset();
	Require(!weak.expired() && first_source.IsLive() && second_source.IsLive(), "selected image assets must enclose their native resource wrappers");
	{
		sgre::scene::ImageTextureOwner first_owner(cache, first);
		sgre::scene::ImageTextureOwner second_owner(cache, second);
		Require(first_owner.SourceCount() == 1 && first_owner.Source(0).Texture() == first_texture && second_owner.Source(0).Texture() == second_texture && uploads == 2 && cache.Get(first_texture).References == 2 && cache.Get(second_texture).References == 2, "post-purge Image wrappers must hit each true original source without decoding metadata as pixels");
		Require(first_owner.AddressU(0) == 2 && first_owner.AddressV(0) == 2, "selected Image wrapper must apply its native mirror addressing independently of preload");
	}
	Require(cache.Get(first_texture).References == 1 && cache.Get(second_texture).References == 1 && releases.empty(), "selected wrapper retirement must preserve resource preloads");
	first.reset();
	Require(!weak.expired() && releases.empty(), "another selected image must preserve the containing native document");
	second.reset();
	catalog.Prune();
	Require(weak.expired() && !first_source.IsLive() && !second_source.IsLive() && cache.Count() == 0 && releases == std::vector<std::uint32_t>{1, 2} && catalog.DocumentCount() == 0, "final image owner must retire preloads in native source order before metadata");
}

static void TestPartialUploadAndForeignScope() {
	std::uint32_t attempts = 0;
	std::vector<std::uint32_t> releases;
	sgre::scene::TextureCache cache([&](auto, const auto&) {
		if (++attempts == 2) {
			throw std::runtime_error("second image upload failed");
		}
		return sgre::render::SpriteTextureHandle{attempts, 1, &attempts};
	},
	    [&](auto texture) {
		    releases.push_back(texture.Index);
	    });
	RequireFailure([&] {
		const sgre::scene::ImageReceivingDocument receiving(cache, FixtureBytes());
	},
	    "second image upload");
	Require(cache.Count() == 0 && releases == std::vector<std::uint32_t>{1}, "partial Image preload must retire every completed upload");
	const auto receiving = std::make_shared<const sgre::scene::ImageReceivingDocument>(cache, FixtureBytes());
	std::uint32_t foreign_uploads = 0;
	sgre::scene::TextureCache foreign([&](auto, const auto&) {
		return sgre::render::SpriteTextureHandle{++foreign_uploads, 1, &foreign_uploads};
	},
	    [](auto) {
	    });
	sgre::scene::ImageAssetCatalog foreign_catalog(foreign);
	RequireFailure([&] {
		static_cast<void>(foreign_catalog.Prepare(receiving));
	},
	    "actual cache scope");
	sgre::scene::ImageAssetCatalog ordinary(cache);
	RequireFailure([&] {
		const auto document = std::shared_ptr<const sgre::resource::PsbDocument>(receiving, &receiving->Document());
		const auto backing = std::make_shared<const std::vector<std::uint8_t>>(receiving->MetadataStorage());
		static_cast<void>(ordinary.Prepare(document, backing));
	},
	    "immutable PSB/backing");
	Require(foreign_uploads == 0, "foreign Image receiving admission must fail before platform mutation");
}

int main() {
	try {
		TestAllEntriesPreloadAndSelectedOwner();
		TestPartialUploadAndForeignScope();
		std::cout << "Image receiving: ordered preloads, true metadata purge, selected cache hits and lifetime passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}

	return 0;
}
