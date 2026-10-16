#include "sgre/resource/resource_cache.hpp"

#include "bitmap_font_receiving_fixture.hpp"
#include "sgre/resource/resource_cipher.hpp"
#include "sgre/scene/bitmap_font_preparation_service.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static void RequireFailure(const auto& operation) {
	try {
		operation();
	} catch (const std::runtime_error&) {
		return;
	}

	throw std::runtime_error("receiving observation fixture expected rejection");
}

static void WriteFile(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	for (const auto byte : bytes) {
		output.put(static_cast<char>(byte));
	}
	Require(static_cast<bool>(output), "cannot write bounded receiving fixture");
}

static std::vector<std::uint8_t> WrappedFont(const std::vector<std::uint8_t>& decoded, const std::string& filename) {
	Require(decoded.size() >= 256 && decoded.size() < 65536, "font fixture exceeds its independent raw-block domain");
	// Independent RFC8878 single raw block, encrypted with the existing first-party cipher.
	const auto content_size = decoded.size() - 256;
	const auto block = static_cast<std::uint32_t>((decoded.size() << 3) | 1);
	std::vector<std::uint8_t> frame{0x28, 0xb5, 0x2f, 0xfd, 0x60, static_cast<std::uint8_t>(content_size), static_cast<std::uint8_t>(content_size >> 8), static_cast<std::uint8_t>(block), static_cast<std::uint8_t>(block >> 8), static_cast<std::uint8_t>(block >> 16)};
	frame.insert(frame.end(), decoded.begin(), decoded.end());
	sgre::resource::ResourceCipher cipher(filename, "synthetic-seed", 131);
	cipher.Transform(frame);
	std::vector<std::uint8_t> bytes{'m', 'z', 's', 0, static_cast<std::uint8_t>(decoded.size()), static_cast<std::uint8_t>(decoded.size() >> 8), 0, 0};
	bytes.insert(bytes.end(), frame.begin(), frame.end());

	return bytes;
}

static void TestQueuedCancellationAndOwnerGuards() {
	sgre::resource::ResourceCache cache(0);
	const auto handle = cache.Acquire("fixture/cancel");
	sgre::io::ResourceReadFormat format;
	format.Kind = sgre::io::ResourceFileKind::Psb;
	auto& destination = cache.CreateResourceReadDestination(handle, format);
	const auto initial = cache.ObserveReceiving(handle);
	Require(initial.Receiving && initial.Receiving->Phase == sgre::io::ResourceReceivingPhase::Unassigned && initial.Receiving->CanRetire, "fresh cell must remain actually unassigned");
	destination.MarkQueued();
	for (std::size_t index = 0; index < 32; ++index) {
		const auto row = cache.ObserveReceiving(handle);
		Require(row.Receiving && row.Receiving->Phase == sgre::io::ResourceReceivingPhase::Queued && row.Receiving->Completion == sgre::io::FileReadCompletion::Pending && !row.Receiving->CanRetire && row.References == 1 && row.Completion == sgre::resource::ResourceCompletion::Pending, "queued observations must not mutate ownership/readiness");
	}
	auto foreign = std::async(std::launch::async, [&]() {
		RequireFailure([&]() {
			static_cast<void>(cache.ObserveReceiving(handle));
		});
	});
	foreign.get();
	Require(destination.CancelQueued(), "fixture must cancel its actual unselected request");
	const auto cancelled = cache.ObserveReceiving(handle);
	Require(cancelled.Receiving && cancelled.Receiving->Completion == sgre::io::FileReadCompletion::Cancelled && cancelled.Receiving->CanRetire && cancelled.Completion == sgre::resource::ResourceCompletion::Pending, "cancel observation must not transfer manager state");
	cache.Advance();
	Require(cache.ObserveReceiving(handle).Completion == sgre::resource::ResourceCompletion::Failed, "actual manager visit must receive cancellation");
	cache.Release(handle);
	RequireFailure([&]() {
		static_cast<void>(cache.ObserveReceiving(handle));
	});
}

static void TestPhysicalFailureAndPublication(const std::filesystem::path& directory) {
	sgre::resource::ResourceCache cache(0);
	const auto handle = cache.Acquire("fixture/missing");
	sgre::io::ResourceReadFormat format;
	format.Kind = sgre::io::ResourceFileKind::Psb;
	auto& destination = cache.CreateResourceReadDestination(handle, format);
	destination.MarkQueued();
	destination.Select();
	const auto selected = cache.ObserveReceiving(handle);
	Require(selected.Receiving && selected.Receiving->Phase == sgre::io::ResourceReceivingPhase::Selected && !selected.Receiving->CanRetire, "selected cell cannot become a ready token");
	sgre::io::PreparedResourceFileRead source;
	source.Format = format;
	source.OriginalFilename = "fixture/missing";
	source.Region.Filename = directory / "nonexistent.bin";
	source.Region.MaxBytes = 4096;
	Require(!destination.ReadPhysicalSelected(source), "genuine missing fixture file must fail physical read");
	Require(destination.PrepareSelectedObject(source) == sgre::io::FileReadCompletion::ReadFailed, "physical failure must retain actual completion word");
	Require(destination.ObserveReceiving().Phase == sgre::io::ResourceReceivingPhase::ObjectPrepared && !destination.ObserveReceiving().CanRetire, "object preparation must not release selected borrow before publication");
	destination.PublishSelected();
	const auto published = cache.ObserveReceiving(handle);
	Require(published.Receiving && published.Receiving->Phase == sgre::io::ResourceReceivingPhase::PublishingCompletion && published.Receiving->Completion == sgre::io::FileReadCompletion::ReadFailed && published.Receiving->CanRetire && published.Completion == sgre::resource::ResourceCompletion::Pending, "publication must be separate from manager reception");
	cache.Advance();
	Require(cache.Info(handle).Completion == sgre::resource::ResourceCompletion::Failed && !cache.Info(handle).Error.empty(), "actual manager must receive physical diagnostic");
	cache.Release(handle);
}

static void TestActualBlockedFontOwner(const std::filesystem::path& directory, bool wrapped, bool stop) {
	std::uint32_t uploads = 0;
	std::uint32_t releases = 0;
	const auto owner_thread = std::this_thread::get_id();
	auto preparation = std::make_shared<sgre::scene::BitmapFontPreparationService>([&](auto, const auto& image) {
		Require(std::this_thread::get_id() == owner_thread && !image.Pixels.empty(), "font preparation must upload real fixture pixels on actual owner");

		return sgre::render::SpriteTextureHandle{++uploads, 1, &uploads};
	},
	    [&](auto) {
		    Require(std::this_thread::get_id() == owner_thread, "font release must use actual preparation owner");
		    ++releases;
	    });
	sgre::resource::ResourceCache cache(0);
	const auto handle = cache.Acquire("fixture/font.psb");
	sgre::io::ResourceReadFormat format;
	format.Kind = sgre::io::ResourceFileKind::Psb;
	format.Wrapped = wrapped;
	auto& destination = cache.CreateResourceReadDestination(handle, format);
	destination.MarkQueued();
	destination.Select();
	sgre::io::PreparedResourceFileRead source;
	source.Format = format;
	source.OriginalFilename = wrapped ? "fixture/font.psb.m" : "fixture/font.psb";
	source.Seed = "synthetic-seed";
	source.Region.Filename = directory / (wrapped ? "wrapped.bin" : "raw.bin");
	source.Region.MaxBytes = 4096;
	source.MaxDecodedBytes = 4096;
	source.MaxMetadataBytes = 4096;
	source.FontPreparation = preparation;
	const std::vector<std::uint8_t> decoded(kReceivingFontFixture.begin(), kReceivingFontFixture.end());
	WriteFile(source.Region.Filename, wrapped ? WrappedFont(decoded, source.OriginalFilename) : decoded);
	auto worker = std::async(std::launch::async, [&]() {
		Require(destination.ReadPhysicalSelected(source), "fixture worker must read actual source bytes");
		const auto completion = destination.PrepareSelectedObject(source);
		destination.PublishSelected();

		return completion;
	});
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (preparation->PendingCount() == 0) {
		if (std::chrono::steady_clock::now() >= deadline) {
			preparation->Stop();
			static_cast<void>(worker.get());
			throw std::runtime_error("synthetic worker did not reach real font preparation queue");
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	for (std::size_t index = 0; index < 32; ++index) {
		const auto row = cache.ObserveReceiving(handle);
		Require(row.Receiving && row.Receiving->Phase == sgre::io::ResourceReceivingPhase::PreparingFont && row.Receiving->Completion == sgre::io::FileReadCompletion::Pending && !row.Receiving->CanRetire && !row.HasFontDocument && row.Completion == sgre::resource::ResourceCompletion::Pending && row.References == 1 && row.Format == format, "blocked actual font phase must remain pending without payload or lease mutation");
	}
	Require(uploads == 0 && preparation->LiveDocumentCount() == 0 && cache.State().LoadingEntryNum == 1, "observations must not pump the real font producer");
	if (stop) {
		preparation->Stop();
	} else {
		Require(preparation->PreparePending() == 1, "actual preparation owner must perform the queued fixture job");
	}
	const auto expected = stop ? sgre::io::FileReadCompletion::PostprocessFailed : sgre::io::FileReadCompletion::Succeeded;
	Require(worker.get() == expected, "actual worker must retain production success/failure");
	const auto published = cache.ObserveReceiving(handle);
	Require(published.Receiving && published.Receiving->Phase == sgre::io::ResourceReceivingPhase::PublishingCompletion && published.Receiving->Completion == expected && published.Receiving->CanRetire && published.Completion == sgre::resource::ResourceCompletion::Pending && !published.HasFontDocument, "observation cannot promote published font into manager state");
	cache.Advance();
	const auto received = cache.ObserveReceiving(handle);
	Require(received.HasFontDocument == !stop && received.Completion == (stop ? sgre::resource::ResourceCompletion::Failed : sgre::resource::ResourceCompletion::Succeeded), "actual manager visit must receive typed result");
	Require(uploads == (stop ? 0U : 2U), "only actual successful preparation may upload source atoms");
	cache.Release(handle);
	preparation->RetirePending();
	Require(releases == uploads && preparation->LiveDocumentCount() == 0, "actual cache/resource retirement must dispose every prepared texture");
	preparation->Close();
}

int main(int argc, char** argv) {
	try {
		Require(argc == 2, "receiving observation tests require an owned fixture directory");
		const std::filesystem::path directory(argv[1]);
		std::filesystem::create_directories(directory);
		TestQueuedCancellationAndOwnerGuards();
		TestPhysicalFailureAndPublication(directory);
		TestActualBlockedFontOwner(directory, true, false);
		TestActualBlockedFontOwner(directory, false, true);
		std::cout << "four independent actual receiving/blocked-font observation groups passed; no original scripts/native/GPU/audio\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
