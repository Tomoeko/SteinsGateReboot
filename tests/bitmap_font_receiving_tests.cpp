#include "sgre/scene/bitmap_font_receiving_document.hpp"

#include "bitmap_font_receiving_fixture.hpp"

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

	throw std::runtime_error("font receiving fixture expected failure: " + std::string(expected));
}

static std::vector<std::uint8_t> FixtureBytes() {
	return {kReceivingFontFixture.begin(), kReceivingFontFixture.end()};
}

static void TestMetadataPurgeAndProviderLifetime() {
	std::uint32_t uploads = 0;
	std::vector<std::uint32_t> releases;
	sgre::scene::TextureCache cache([&](auto format, const auto& image) {
		Require(format == sgre::resource::PixelFormat::Alpha8 && image.Width == 1 && image.Height == 1 && image.Pixels == std::vector<std::uint8_t>{0, 0, 0, uploads == 0 ? std::uint8_t{128} : std::uint8_t{64}}, "receiving uploads must preserve independent ordered A8 fixture pixels");

		return sgre::render::SpriteTextureHandle{++uploads, 1, &uploads};
	},
	    [&](auto handle) {
		    releases.push_back(handle.Index);
	    });
	auto document = std::make_shared<const sgre::scene::BitmapFontReceivingDocument>(cache, FixtureBytes());
	const auto weak = std::weak_ptr(document);
	const auto preload = document->Source(0);
	const auto texture = preload.Texture();
	Require(document->SourceCount() == 2 && document->Font().Glyphs.size() == 1 && document->Font().FindGlyph("A")->NominalWidth == 5, "receiving owner must retain parsed independent font metadata");
	Require(document->MetadataBytes().size() == document->Document().Header.DataOffset && document->Document().SerializedSize == kReceivingFontFixture.size() && document->MetadataBytes().size() + 2 == kReceivingFontFixture.size(), "receiving metadata must not pretend the purged original chunk payload still exists");
	Require(uploads == 2 && cache.Get(texture).References == 1, "receiving owner must acquire one real wrapper per source before pixel disposal");
	sgre::scene::BitmapFontTextureOwner first(cache, document);
	sgre::scene::BitmapFontTextureOwner second(cache, document);
	const auto first_source = first.Source(0);
	const auto second_source = second.Source(0);
	Require(uploads == 2 && cache.Get(texture).References == 3 && first_source.Texture() == texture && second_source.Texture() == texture && !preload.SameIdentity(first_source) && !first_source.SameIdentity(second_source), "purged providers must acquire distinct wrappers sharing actual retained document/value cache entries");
	first.SetFiltering(1, 1);
	Require(cache.Get(texture).Minification == 1 && second.Minification(0) == 0, "provider filtering must retain local wrapper state and update its shared physical sampler");
	document.reset();
	Require(!weak.expired() && preload.IsLive(), "providers must strongly pin the receiving resource and its real preload references");
	first.Close();
	Require(!first_source.IsLive() && second_source.IsLive() && cache.Get(texture).References == 2 && releases.empty(), "intermediate provider retirement must preserve preload and other provider references");
	second.Close();
	Require(weak.expired() && !second_source.IsLive() && !preload.IsLive() && cache.Count() == 0 && releases == std::vector<std::uint32_t>{1, 2}, "last provider must retire its wrappers then resource preloads in original source order");
}

static void TestUploadRollbackAndCacheScope() {
	std::uint32_t attempts = 0;
	std::vector<std::uint32_t> releases;
	sgre::scene::TextureCache cache([&](auto, const auto&) {
		if (++attempts == 2) {
			throw std::runtime_error("second receiving upload failure");
		}

		return sgre::render::SpriteTextureHandle{attempts, 1, &attempts};
	},
	    [&](auto handle) {
		    releases.push_back(handle.Index);
	    });
	RequireFailure([&] {
		sgre::scene::BitmapFontReceivingDocument document(cache, FixtureBytes());
	},
	    "second receiving upload");
	Require(cache.Count() == 0 && cache.UploadedBytes() == 0 && releases == std::vector<std::uint32_t>{1}, "failed receiving upload must retire every prior acquired wrapper");

	std::uint32_t good_uploads = 0;
	sgre::scene::TextureCache good([&](auto, const auto&) {
		return sgre::render::SpriteTextureHandle{++good_uploads, 1, &good_uploads};
	},
	    [](auto) {
	    });
	auto document = std::make_shared<const sgre::scene::BitmapFontReceivingDocument>(good, FixtureBytes());
	RequireFailure([&] {
		sgre::scene::BitmapFontTextureOwner wrong(cache, document);
	},
	    "actual cache scope");
	Require(good_uploads == 2 && good.Count() == 2 && cache.Count() == 0, "wrong-cache provider rejection must not mutate either actual cache");
	RequireFailure([&] {
		sgre::scene::BitmapFontTextureOwner absent(good, std::shared_ptr<const sgre::scene::BitmapFontReceivingDocument>{});
	},
	    "loaded receiving document");
	RequireFailure([&] {
		static_cast<void>(document->Source(2));
	},
	    "index exceeds");
}

static void TestFailedReleaseStillRetiresEverySource() {
	std::uint32_t uploads = 0;
	std::vector<std::uint32_t> releases;
	sgre::scene::TextureCache cache([&](auto, const auto&) {
		return sgre::render::SpriteTextureHandle{++uploads, 1, &uploads};
	},
	    [&](auto handle) {
		    releases.push_back(handle.Index);
		    throw std::runtime_error("fixture release failure");
	    });
	{
		const sgre::scene::BitmapFontReceivingDocument document(cache, FixtureBytes());
		Require(cache.Count() == 2, "receiving release fixture requires its real uploaded references");
	}
	Require(cache.Count() == 0 && releases == std::vector<std::uint32_t>{1, 2}, "nonthrowing receiving destruction must finish ordered retirement after platform failures");
}

static void TestEveryTruncatedInputRejectedBeforeUpload() {
	std::uint32_t uploads = 0;
	sgre::scene::TextureCache cache([&](auto, const auto&) {
		++uploads;
		throw std::runtime_error("truncated font must never reach upload");

		return sgre::render::SpriteTextureHandle{};
	},
	    [](auto) {
	    });
	for (std::size_t length = 0; length < kReceivingFontFixture.size(); ++length) {
		try {
			const sgre::scene::BitmapFontReceivingDocument document(cache, std::vector<std::uint8_t>(kReceivingFontFixture.begin(), kReceivingFontFixture.begin() + length));
		} catch (const std::runtime_error&) {
			continue;
		}

		throw std::runtime_error("truncated font receiving input was accepted");
	}
	Require(uploads == 0 && cache.Count() == 0, "all strict truncated input failures must precede texture cache mutation");
}

int main() {
	try {
		TestMetadataPurgeAndProviderLifetime();
		TestUploadRollbackAndCacheScope();
		TestFailedReleaseStillRetiresEverySource();
		TestEveryTruncatedInputRejectedBeforeUpload();
		std::cout << "bitmap font receiving: 4 fixture groups passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
