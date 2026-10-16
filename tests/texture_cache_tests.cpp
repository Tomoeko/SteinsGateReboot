#include "sgre/scene/texture_cache.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
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
static void RequireFailure(Operation operation, std::string_view context) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("texture preparation failure was not reported");
}

static sgre::resource::ImageTexture Description(std::uint32_t value_offset) {
	sgre::resource::ImageTexture texture;
	texture.SerializedValueOffset = value_offset;
	texture.Format = sgre::resource::PixelFormat::Alpha8;
	texture.Width = 1;
	texture.Height = 1;
	texture.TruncatedWidth = 1;
	texture.TruncatedHeight = 1;
	texture.PixelRange.Size = 1;

	return texture;
}

static void TestSharedIdentityAndOrderedRetirement() {
	std::uint32_t uploads = 0;
	std::vector<std::uint32_t> retired;
	bool check_native_retirement = true;
	const std::array<std::size_t, 4> upload_counts{0, 1, 2, 2};
	sgre::scene::TextureCache* observed = nullptr;
	sgre::scene::TextureCache cache([&](auto format, const auto& image) {
		Require(format == sgre::resource::PixelFormat::Alpha8 && image.Width == 1 && image.Height == 1 && image.Pixels == std::vector<std::uint8_t>{0, 0, 0, 128}, "first-party decoding must preserve native A8 sampled channels before upload");
		Require(observed->Count() == upload_counts.at(uploads), "cache registration must follow successful physical upload");

		return sgre::render::SpriteTextureHandle{++uploads, 1, &uploads};
	},
	    [&](const auto texture) {
		    if (check_native_retirement) {
			    Require(observed->Count() == 3 && observed->UploadedBytes() == 12, "native GPU release must precede cache-vector compaction and byte retirement");
		    }
		    retired.push_back(texture.Index);
	    });
	observed = &cache;
	const std::array<std::uint8_t, 1> bytes{128};
	const auto first = cache.Acquire(11, Description(100), bytes);
	const auto duplicate = cache.Acquire(11, Description(100), {});
	const auto equal_pixels = cache.Acquire(11, Description(101), bytes);
	const auto another_document = cache.Acquire(22, Description(100), bytes);
	Require(first == duplicate && uploads == 3 && cache.Get(first).References == 2 && equal_pixels != first && another_document != first, "texture sharing must use document/value identity rather than filename or equal pixels");
	cache.SetFiltering(duplicate, 1, 1);
	Require(cache.Get(first).Minification == 1 && cache.Get(first).Magnification == 1, "sampler mutation must affect the shared uploaded texture");
	cache.ReleaseTexture(first);
	Require(retired.empty() && cache.Contains(duplicate) && cache.Get(duplicate).References == 1, "an intermediate reference release must preserve the uploaded texture");
	cache.ReleaseTexture(duplicate);
	Require(retired == std::vector<std::uint32_t>{1} && !cache.Contains(first) && cache.Count() == 2 && cache.UploadedBytes() == 8, "final reference release must retire exactly one physical texture");
	std::array<sgre::scene::CachedTextureHandle, 3> order;
	Require(cache.WriteRegistrationOrder(order) == 2 && order[0] == equal_pixels && order[1] == another_document, "cache compaction must preserve surviving registration order");
	const auto replacement = cache.Acquire(11, Description(100), bytes);
	Require(replacement.Index == first.Index && replacement.Generation != first.Generation && uploads == 4, "a retired document/value must upload anew with a checked handle generation");
	RequireFailure([&] {
		cache.ReleaseTexture(first);
	},
	    "stale");
	Require(!cache.TryAcquire({99, 100}), "a cache miss must not create a physical upload");
	// Subsequent safety close has its own order contract; the native oracle covers reference-driven retirement.
	check_native_retirement = false;
	cache.Close();
	Require(retired == std::vector<std::uint32_t>{1, 2, 3, 4}, "safety close must retire every remaining physical owner exactly once");
}

static void TestPreparationFailureAndClose() {
	std::uint32_t uploads = 0;
	std::vector<std::uint32_t> retired;
	bool fail_upload = true;
	bool fail_release = false;
	sgre::scene::TextureCacheLimits limits;
	limits.MaxTextures = 2;
	limits.MaxTextureBytes = 8;
	limits.MaxDecodedTextureBytes = 4;
	sgre::scene::TextureCache cache([&](auto, const auto&) {
		if (fail_upload) {
			throw std::runtime_error("upload fixture failure");
		}

		return sgre::render::SpriteTextureHandle{++uploads, 1, &uploads};
	},
	    [&](const auto texture) {
		    retired.push_back(texture.Index);
		    if (fail_release) {
			    throw std::runtime_error("release fixture failure");
		    }
	    },
	    limits);
	const std::array<std::uint8_t, 1> bytes{128};
	RequireFailure([&] {
		static_cast<void>(cache.Acquire(11, Description(100), bytes));
	},
	    "upload fixture failure");
	Require(cache.Count() == 0 && cache.UploadedBytes() == 0, "failed upload must not publish a cache record");
	fail_upload = false;
	const auto first = cache.Acquire(11, Description(100), bytes);
	const auto second = cache.Acquire(11, Description(101), bytes);
	RequireFailure([&] {
		static_cast<void>(cache.Acquire(11, Description(102), bytes));
	},
	    "storage limit");
	RequireFailure([&] {
		static_cast<void>(cache.WriteRegistrationOrder({}));
	},
	    "output is too small");
	fail_release = true;
	RequireFailure([&] {
		cache.ReleaseTexture(first);
	},
	    "release fixture failure");
	Require(!cache.Contains(first) && cache.Contains(second) && cache.Count() == 1 && cache.UploadedBytes() == 4, "failed platform cleanup must still retire the cache identity and bounded bookkeeping");
	RequireFailure([&] {
		cache.Close();
	},
	    "release fixture failure");
	Require(retired == std::vector<std::uint32_t>{1, 2} && cache.Count() == 0 && cache.UploadedBytes() == 0 && !cache.Contains(second), "close must attempt all physical owners despite a reported service failure");
	cache.Close();
	RequireFailure([&] {
		static_cast<void>(cache.TryAcquire({11, 100}));
	},
	    "live preparation owner");
}

static void TestLimitsAndCallbackBoundaries() {
	std::uint32_t uploads = 0;
	sgre::scene::TextureCache* observed = nullptr;
	sgre::scene::TextureCacheLimits limits;
	limits.MaxTextures = 1;
	sgre::scene::TextureCache cache([&](auto, const auto&) {
		RequireFailure([&] {
			static_cast<void>(observed->TryAcquire({11, 100}));
		},
		    "platform callback");

		return sgre::render::SpriteTextureHandle{++uploads, 1, &uploads};
	},
	    [](auto) {
	    },
	    limits);
	observed = &cache;
	const std::array<std::uint8_t, 1> bytes{128};
	RequireFailure([&] {
		static_cast<void>(cache.Acquire(0, Description(100), bytes));
	},
	    "loaded document");
	auto texture = Description(100);
	texture.SerializedValueOffset.reset();
	RequireFailure([&] {
		static_cast<void>(cache.Acquire(11, texture, bytes));
	},
	    "serialized PSB value identity");
	texture = Description(100);
	texture.Width = std::numeric_limits<std::uint32_t>::max();
	texture.Height = std::numeric_limits<std::uint32_t>::max();
	RequireFailure([&] {
		static_cast<void>(cache.Acquire(11, texture, bytes));
	},
	    "storage limit");
	RequireFailure([&] {
		static_cast<void>(cache.Acquire(11, Description(100), {}));
	},
	    "input");
	const auto first = cache.Acquire(11, Description(100), bytes);
	RequireFailure([&] {
		static_cast<void>(cache.Acquire(11, Description(101), bytes));
	},
	    "capacity");
	sgre::scene::TextureCache foreign([](auto, const auto&) {
		return sgre::render::SpriteTextureHandle{};
	},
	    [](auto) {
	    });
	Require(!foreign.Contains(first), "foreign caches must not accept another owner's texture identity");
	cache.Close();
	Require(uploads == 1 && !cache.Contains(first), "invalid preparations must not invoke the upload service or retain owned texture state");
}

static void TestExplicitStoredBc7Service() {
	std::uint32_t block_uploads = 0;
	std::uint32_t rgba_uploads = 0;
	std::vector<std::uint32_t> retired;
	bool fail = true;
	sgre::scene::TextureCacheLimits limits;
	limits.MaxTextures = 3;
	limits.MaxTextureBytes = 32;
	limits.MaxDecodedTextureBytes = 64;
	std::array<std::uint8_t, 16> bytes{};
	bytes[0] = 64;
	auto description = Description(100);
	description.Format = sgre::resource::PixelFormat::Bc7;
	description.Width = 4;
	description.Height = 4;
	description.PixelRange.Size = 16;
	sgre::scene::TextureCache* observed = nullptr;
	sgre::scene::TextureCache cache([&](auto, const auto&) -> sgre::render::SpriteTextureHandle {
		++rgba_uploads;
		throw std::runtime_error("BC7 service failure must never invoke RGBA fallback");
	},
	    [&](auto handle) {
		    retired.push_back(handle.Index);
	    }, limits,
	    [&](const auto& view) {
		    Require(view.BlockRowBytes == 16 && view.Blocks.data() == bytes.data() && view.Blocks.size() == 16, "BC7 callback must receive the exact original borrowed block");
		    RequireFailure([&] {
			    static_cast<void>(observed->TryAcquire({1, 100}));
		    }, "platform callback");
		    if (fail) {
			    throw std::runtime_error("explicit BC7 upload failure");
		    }

		    return sgre::render::SpriteTextureHandle{++block_uploads, 1, &block_uploads};
	    });
	observed = &cache;
	RequireFailure([&] {
		static_cast<void>(cache.Acquire(1, description, bytes));
	}, "explicit BC7");
	Require(rgba_uploads == 0 && cache.Count() == 0 && cache.UploadedBytes() == 0, "failed compressed upload must leave no publication, byte charge or fallback");
	fail = false;
	const auto first = cache.Acquire(1, description, bytes);
	const auto same = cache.Acquire(1, description, {});
	Require(first == same && block_uploads == 1 && cache.UploadedBytes() == 16 && cache.Get(first).References == 2, "stored-byte cache charge and metadata-only reuse must share the actual document/value identity");
	description.SerializedValueOffset = 101;
	const auto distinct = cache.Acquire(1, description, bytes);
	Require(block_uploads == 2 && cache.UploadedBytes() == 32, "equal block contents must not merge distinct PSB value owners");
	description.SerializedValueOffset = 102;
	RequireFailure([&] {
		static_cast<void>(cache.Acquire(1, description, bytes));
	}, "aggregate upload");
	cache.ReleaseTexture(first);
	Require(retired.empty() && cache.Contains(same), "intermediate block reference retirement must preserve its texture");
	cache.ReleaseTexture(same);
	Require(retired == std::vector<std::uint32_t>{1} && cache.UploadedBytes() == 16, "last block reference must return exactly its stored byte charge");
	cache.ReleaseTexture(distinct);
	const auto retry = cache.Acquire(1, description, bytes);
	Require(retry.Index == first.Index && retry.Generation != first.Generation && cache.UploadedBytes() == 16, "stored texture slot reuse must retain checked generations and bounded accounting");
	cache.Close();
	Require(cache.UploadedBytes() == 0 && retired == std::vector<std::uint32_t>{1, 2, 3} && rgba_uploads == 0, "stored owners must retire once in actual order without RGBA fallback");
}

static void TestBc7FallbackAndExtent() {
	std::uint32_t calls = 0;
	std::array<std::uint8_t, 16> bytes{};
	bytes[0] = 64;
	auto description = Description(100);
	description.Format = sgre::resource::PixelFormat::Bc7;
	description.Width = 4;
	description.Height = 4;
	description.PixelRange.Size = 16;
	sgre::scene::TextureCache cache([&](auto format, const auto& image) {
		Require(format == sgre::resource::PixelFormat::Bc7 && image.Pixels.size() == 64, "absent compressed service must preserve the existing first-party RGBA path");

		return sgre::render::SpriteTextureHandle{++calls, 1, &calls};
	}, [](auto) {});
	const auto texture = cache.Acquire(1, description, bytes);
	Require(cache.UploadedBytes() == 64 && calls == 1, "fallback cache must retain its actual expanded byte charge");
	cache.ReleaseTexture(texture);
	cache.Close();

	sgre::scene::TextureCacheLimits limits;
	limits.MaxDecodedTextureBytes = 63;
	sgre::scene::TextureCache bounded([](auto, const auto&) -> sgre::render::SpriteTextureHandle {
		throw std::runtime_error("unexpected RGBA callback");
	}, [](auto) {}, limits, [&](const auto&) {
		++calls;

		return sgre::render::SpriteTextureHandle{calls, 1, &calls};
	});
	RequireFailure([&] {
		static_cast<void>(bounded.Acquire(1, description, bytes));
	}, "storage limit");
	Require(calls == 1 && bounded.UploadedBytes() == 0, "compressed service must not broaden the existing decoded-extent domain");
	bounded.Close();
}

int main() {
	try {
		TestSharedIdentityAndOrderedRetirement();
		TestPreparationFailureAndClose();
		TestLimitsAndCallbackBoundaries();
		TestExplicitStoredBc7Service();
		TestBc7FallbackAndExtent();
		std::cout << "Shared native texture ownership checks passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
