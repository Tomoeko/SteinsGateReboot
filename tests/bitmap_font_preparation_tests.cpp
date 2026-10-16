#include "sgre/scene/bitmap_font_preparation_service.hpp"

#include "bitmap_font_receiving_fixture.hpp"

#include <chrono>
#include <cstdint>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
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

	throw std::runtime_error("font preparation expected failure: " + std::string(expected));
}

static std::vector<std::uint8_t> FixtureBytes() {
	return {kReceivingFontFixture.begin(), kReceivingFontFixture.end()};
}

static void WaitPending(sgre::scene::BitmapFontPreparationService& service) {
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (service.PendingCount() == 0) {
		if (std::chrono::steady_clock::now() >= deadline) {
			service.Stop();
			throw std::runtime_error("font fixture worker did not submit its real preparation job");
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}

static void TestWorkerCompletionAndDeferredRetirement() {
	const auto owner = std::this_thread::get_id();
	std::uint32_t uploads = 0;
	std::vector<std::uint32_t> releases;
	sgre::scene::BitmapFontPreparationService service([&](auto, const auto&) {
		Require(std::this_thread::get_id() == owner, "worker must never invoke texture upload");

		return sgre::render::SpriteTextureHandle{++uploads, 1, &uploads};
	},
	    [&](auto handle) {
		    Require(std::this_thread::get_id() == owner, "final off-thread pin must never invoke texture release");
		    releases.push_back(handle.Index);
	    });
	auto worker = std::async(std::launch::async, [&] {
		return service.Prepare(FixtureBytes());
	});
	WaitPending(service);
	Require(uploads == 0 && worker.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready, "worker cannot claim completion before actual preparation-owner uploads");
	Require(service.PreparePending() == 1, "owner did not prepare the actual queued font");
	auto document = worker.get();
	Require(uploads == 2 && service.LiveDocumentCount() == 1 && document->MetadataBytes().size() + 2 == kReceivingFontFixture.size(), "worker result must own real preloads and purged metadata");
	RequireFailure([&] {
		service.Close();
	},
	    "actual returned");
	auto release = std::async(std::launch::async, [document = std::move(document)]() mutable {
		document.reset();
	});
	release.get();
	Require(releases.empty() && service.LiveDocumentCount() == 1 && service.Cache().Count() == 2 && service.CanClose(), "off-thread final release must queue actual retirement without mutating the cache");
	service.RetirePending();
	Require(service.LiveDocumentCount() == 0 && service.Cache().Count() == 0 && releases == std::vector<std::uint32_t>{1, 2}, "owner must retire each source in its actual resource order");
	service.Close();
}

static void TestStopReleasesWorkerWithoutDiscardingLivePins() {
	std::uint32_t uploads = 0;
	sgre::scene::BitmapFontPreparationService service([&](auto, const auto&) {
		return sgre::render::SpriteTextureHandle{++uploads, 1, &uploads};
	},
	    [](auto) {
	    });
	auto document = service.Prepare(FixtureBytes());
	auto worker = std::async(std::launch::async, [&] {
		return service.Prepare(FixtureBytes());
	});
	WaitPending(service);
	service.Stop();
	RequireFailure([&] {
		static_cast<void>(worker.get());
	},
	    "cancelled before actual texture");
	Require(uploads == 2 && document->Source(0).IsLive() && service.LiveDocumentCount() == 1 && !service.CanClose(), "stopping jobs must preserve every genuine already returned font owner");
	RequireFailure([&] {
		static_cast<void>(service.Prepare(FixtureBytes()));
	},
	    "stopped");
	document.reset();
	service.Close();
}

static void TestQueueAndLiveCapacity() {
	std::uint32_t uploads = 0;
	sgre::scene::BitmapFontPreparationLimits limits;
	limits.MaxPendingJobs = 1;
	limits.MaxQueuedBytes = kReceivingFontFixture.size();
	limits.MaxLiveDocuments = 1;
	sgre::scene::BitmapFontPreparationService service([&](auto, const auto&) {
		return sgre::render::SpriteTextureHandle{++uploads, 1, &uploads};
	},
	    [](auto) {
	    },
	    {}, limits);
	auto first = std::async(std::launch::async, [&] {
		return service.Prepare(FixtureBytes());
	});
	WaitPending(service);
	auto rejected = std::async(std::launch::async, [&] {
		return service.Prepare(FixtureBytes());
	});
	RequireFailure([&] {
		static_cast<void>(rejected.get());
	},
	    "pending job/byte bound");
	Require(service.PendingCount() == 1 && uploads == 0, "queue bound rejection must preserve the original actual queued job");
	static_cast<void>(service.PreparePending());
	auto document = first.get();
	auto full = std::async(std::launch::async, [&] {
		return service.Prepare(FixtureBytes());
	});
	WaitPending(service);
	static_cast<void>(service.PreparePending());
	RequireFailure([&] {
		static_cast<void>(full.get());
	},
	    "live-document capacity");
	Require(uploads == 2 && service.LiveDocumentCount() == 1, "live capacity failure must precede another texture upload");
	document.reset();
	service.Close();
}

static void TestUploadFailureAndCallbackReentry() {
	std::uint32_t uploads = 0;
	std::uint32_t releases = 0;
	sgre::scene::BitmapFontPreparationService* borrowed = nullptr;
	sgre::scene::BitmapFontPreparationService service([&](auto, const auto&) {
		RequireFailure([&] {
			static_cast<void>(borrowed->Prepare(FixtureBytes()));
		},
		    "outside platform callbacks");
		if (++uploads == 2) {
			throw std::runtime_error("second real upload rejected");
		}

		return sgre::render::SpriteTextureHandle{uploads, 1, &uploads};
	},
	    [&](auto) {
		    ++releases;
	    });
	borrowed = &service;
	auto worker = std::async(std::launch::async, [&] {
		return service.Prepare(FixtureBytes());
	});
	WaitPending(service);
	static_cast<void>(service.PreparePending());
	RequireFailure([&] {
		static_cast<void>(worker.get());
	},
	    "second real upload");
	Require(service.LiveDocumentCount() == 0 && service.Cache().Count() == 0 && releases == 1, "failed real preparation must return its error and balance acquired texture ownership");
	service.Close();
}

static void TestOptionalBc7PreparationService() {
	std::uint32_t uploads = 0;
	std::uint32_t block_uploads = 0;
	std::vector<std::uint32_t> released;
	sgre::scene::TextureCacheLimits limits;
	limits.MaxTextures = 3;
	limits.MaxTextureBytes = 24;
	sgre::scene::BitmapFontPreparationService preparation([&](auto format, const auto& image) {
		Require(format == sgre::resource::PixelFormat::Alpha8 && image.Pixels.size() == 4, "optional BC7 service must preserve genuine A8 preparation");

		return sgre::render::SpriteTextureHandle{++uploads, 1, &uploads};
	}, [&](auto handle) {
		released.push_back(handle.Index);
	}, limits, sgre::scene::BitmapFontPreparationLimits{}, [&](const auto& view) {
		Require(view.Width == 4 && view.Height == 4 && view.BlockRowBytes == 16 && view.Blocks.size() == 16, "preparation owner lost the exact BC7 upload service");
		++block_uploads;

		return sgre::render::SpriteTextureHandle{++uploads, 1, &uploads};
	});
	std::vector<std::uint8_t> blocks(16, 0);
	blocks[0] = 64;
	sgre::resource::ImageTexture texture;
	texture.SerializedValueOffset = 1;
	texture.Format = sgre::resource::PixelFormat::Bc7;
	texture.Width = 4;
	texture.Height = 4;
	texture.PixelRange.Size = blocks.size();
	const auto document_identity = preparation.Cache().AllocateDocumentIdentity();
	const auto cached = preparation.Cache().Acquire(document_identity, texture, blocks);
	blocks.clear();
	auto document = preparation.Prepare(FixtureBytes());
	Require(uploads == 3 && block_uploads == 1 && preparation.Cache().Count() == 3 && preparation.Cache().UploadedBytes() == 24, "one real receiving cache must preserve separate stored BC7 and expanded A8 charges");
	document.reset();
	Require(preparation.Cache().Count() == 1 && preparation.Cache().UploadedBytes() == 16, "font retirement must preserve the independent BC7 owner");
	preparation.Cache().ReleaseTexture(cached);
	preparation.Close();
	Require(released == std::vector<std::uint32_t>{2, 3, 1}, "preparation service must forward the optional route without changing ordered retirement");
}

int main() {
	try {
		TestWorkerCompletionAndDeferredRetirement();
		TestStopReleasesWorkerWithoutDiscardingLivePins();
		TestQueueAndLiveCapacity();
		TestUploadFailureAndCallbackReentry();
		TestOptionalBc7PreparationService();
		std::cout << "bitmap font preparation: 5 groups passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
