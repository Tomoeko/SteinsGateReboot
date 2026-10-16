#include "sgre/scene/image_preparation_service.hpp"

#include "image_receiving_fixture.hpp"
#include "resource_file_fixture.hpp"
#include "sgre/script/image_bindings.hpp"

#include <chrono>
#include <exception>
#include <string_view>
#include <thread>

class StopImagePreparation {
public:
	explicit StopImagePreparation(sgre::scene::ImagePreparationService& preparation)
	    : m_Preparation(preparation) {
	}

	~StopImagePreparation() noexcept {
		try {
			// A failed assertion must cancel the waiting worker before the encompassing reader joins it.
			m_Preparation.Stop();
		} catch (...) {
			std::terminate();
		}
	}

private:
	sgre::scene::ImagePreparationService& m_Preparation;
};

static void WaitForActualImageJob(sgre::scene::ImagePreparationService& preparation) {
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (preparation.PendingCount() == 0) {
		if (std::chrono::steady_clock::now() >= deadline) {
			preparation.Stop();
			throw std::runtime_error("actual resource worker did not reach Image texture preparation");
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}

static sgre::script::SquirrelValue Call(sgre::script::SquirrelVm& vm, const sgre::script::SquirrelValue& instance, std::string name, std::vector<sgre::script::SquirrelValue> parameters = {}) {
	std::vector<sgre::script::SquirrelValue> arguments{instance};
	arguments.insert(arguments.end(), parameters.begin(), parameters.end());

	return vm.Invoke(vm.GetSlot(instance, std::move(name)), arguments).Value;
}

template <typename Operation>
static void RequirePublicationFailure(Operation operation, std::string_view expected) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		if (std::string_view(error.what()).find(expected) == std::string_view::npos) {
			throw;
		}
		return;
	}

	throw std::runtime_error("Image resource publication expected rejection: " + std::string(expected));
}

static void MountImageFixture(FileReadProcessor& processor, ArchiveMounts& mounts, BackgroundCallManager& background, const std::filesystem::path& directory, const ByteVector& body) {
	Require(body.size() <= 32767, "Image fixture exceeds its independent signed16 entry-size atom");
	auto index_bytes = IndexPsb(4);
	// The shared tiny index uses signed8. Widen its final size atom and relocate the following sections.
	Require(index_bytes.at(0x176) == 5 && index_bytes.at(0x177) == 4, "independent index entry-size atom differs");
	index_bytes[0x176] = 6;
	index_bytes[0x177] = static_cast<std::uint8_t>(body.size());
	index_bytes.insert(index_bytes.begin() + 0x178, static_cast<std::uint8_t>(body.size() >> 8));
	WriteWord(index_bytes, 16, 0x179);
	WriteWord(index_bytes, 20, 0x17e);
	WriteWord(index_bytes, 24, 0x189);
	WriteWord(index_bytes, 28, 0x18c);
	WriteWord(index_bytes, 32, 0x18f);
	RefreshChecksum(index_bytes);
	const auto document = sgre::resource::ReadPsbMetadata(index_bytes, index_bytes.size());
	const auto index = sgre::resource::ReadArchiveIndex(document, body.size());
	Require(index.Entries.size() == 1 && index.Entries[0].Name == "item" && index.Entries[0].Offset == 0 && index.Entries[0].Size == body.size(), "Image fixture index differs from its declared physical region");
	std::filesystem::create_directories(directory);
	const auto index_path = directory / "index.bin";
	const auto body_path = directory / "body.bin";
	WriteFile(body_path, body);
	WriteFile(index_path, Wrap(index_bytes, "index_info.psb.m"));
	sgre::resource::ArchiveReadMountInput input;
	input.Prefix = "asset/";
	input.IndexIdentity = "index_info.psb.m";
	input.BodyIdentity = "actual-image-body";
	input.BodyFilename = body_path;
	input.BodyBytes = body.size();
	input.IndexRead.Region.Filename = index_path;
	input.IndexRead.Region.MaxBytes = 4096;
	input.IndexRead.OriginalFilename = "index_info.psb.m";
	input.IndexRead.Seed = "synthetic-seed";
	input.IndexRead.MaxDecodedBytes = 4096;
	input.IndexRead.MaxMetadataBytes = 4096;
	processor.QueueReadMount(std::move(input));
	processor.Update();
	DrainLane(background);
	processor.Update();
	processor.Update();
	processor.Update();
	Require(mounts.MountCount() == 1 && !mounts.HasInFlightReadMount(), "actual Image fixture mount did not finish receiving publication");
}

static void TestActualReceivingAndTypedFind(const std::filesystem::path& directory, bool wrapped) {
	std::uint32_t uploads = 0;
	std::vector<std::uint32_t> releases;
	const auto owner_thread = std::this_thread::get_id();
	sgre::scene::TextureCache textures([&](auto format, const auto& image) {
		Require(std::this_thread::get_id() == owner_thread, "file worker uploaded an Image texture outside the preparation owner");
		Require(format == sgre::resource::PixelFormat::Alpha8 && image.Width == 1 && image.Height == 1 && image.Pixels == ByteVector{0, 0, 0, uploads == 0 ? std::uint8_t{128} : std::uint8_t{64}}, "actual Image source bytes or preload order differ");

		return sgre::render::SpriteTextureHandle{++uploads, 1, &uploads};
	},
	    [&](auto texture) {
		    Require(std::this_thread::get_id() == owner_thread, "Image texture release escaped its preparation owner");
		    releases.push_back(texture.Index);
	    });
	auto preparation = std::make_shared<sgre::scene::ImagePreparationService>(textures);
	sgre::scene::ImageAssetCatalog catalog(textures);
	std::shared_ptr<const sgre::scene::LoadedImageAsset> asset_lease;
	sgre::scene::MotionTextureWrapper::Borrowed source;
	{
		auto cache = std::make_unique<ResourceCache>(0);
		ArchiveMounts mounts;
		BackgroundCallManager background;
		FileReadProcessor processor(*cache, mounts, background);
		const ByteVector decoded(kReceivingImageFixture.begin(), kReceivingImageFixture.end());
		MountImageFixture(processor, mounts, background, directory, wrapped ? Wrap(decoded, "asset/item.m") : decoded);
		ScriptResourceFileService files(*cache, mounts, processor, "synthetic-seed", {}, {}, {}, preparation);
		std::size_t errors = 0;
		{
			sgre::script::SquirrelVmLimits limits;
			limits.ReclaimObjects = true;
			sgre::script::SquirrelVm vm(limits);
			sgre::script::ScriptObjectBindings objects(vm);
			sgre::script::ScriptPsbBindingLimits psb_limits;
			psb_limits.MaxDocuments = 1;
			sgre::script::ScriptPsbBindings psb(vm, objects, psb_limits);
			sgre::script::ScriptBinaryBindings binary(vm, objects);
			sgre::script::ScriptResourceBindings resources(vm, objects, psb, binary, files.RequestService(), [&](auto, auto) {
				++errors;
			});
			sgre::script::ScriptImageBindings native(vm, objects, psb, catalog, textures);
			const StopImagePreparation stop_before_worker_join(*preparation);
			const std::array<sgre::script::SquirrelValue, 1> receiver{vm.RootTable()};
			const auto instance = vm.Invoke(resources.ResourceClass(), receiver).Value;
			auto instance_root = vm.Retain(instance);
			static_cast<void>(Call(vm, instance, wrapped ? "load" : "loadRaw", {std::string("asset/item")}));
			const auto handle = *cache->FindNamedResource("asset/item");
			Require(std::get<bool>(vm.ReadSlot(instance, std::string("loading"))) && cache->Info(handle).References == 1, "Resource target must own one actual pending manager reference");
			processor.Update();
			background.PostLoop();
			WaitForActualImageJob(*preparation);
			Require(uploads == 0 && preparation->PendingCount() == 1 && cache->Info(handle).Completion == sgre::resource::ResourceCompletion::Pending && std::get<bool>(vm.ReadSlot(instance, std::string("loading"))), "Image publication must wait for the owner-thread texture production");
			Require(preparation->PreparePending() == 1, "actual Image preparation did not consume its worker job");
			DrainLane(background);
			Require(!std::get<bool>(vm.ReadSlot(instance, std::string("loading"))) && errors == 0 && uploads == 2 && cache->HasImageFile(handle) && cache->Info(handle).ChargedBytes == decoded.size() && cache->Bytes(handle).size() + 2 == decoded.size(), "typed Image publication must separate original charge from retained metadata");
			RequireFailure([&] {
				static_cast<void>(cache->PsbFile(handle));
			});
			RequireFailure([&] {
				static_cast<void>(cache->PinBytes(handle));
			});
			const auto data = Call(vm, instance, "find", {std::string("asset/item")});
			auto data_root = vm.Retain(data);
			auto retained = psb.DocumentAsset(data);
			Require(retained.Image && retained.Document.get() == &retained.Image->Document() && !retained.Backing && !retained.Font && cache->Info(handle).References == 2, "Resource.find must return the typed Image owner with an independent manager pin and no fabricated full backing");
			Require(std::get<std::int32_t>(Call(vm, data, "totalStreamEntitySize")) == 2, "typed Image PSB must preserve its original stream-entity extent");
			const auto root = vm.ReadSlot(data, std::string("root"));
			Require(sgre::script::StringText(vm.ReadSlot(root, std::string("id"))) == "image", "typed Image PSB must expose its original metadata");
			RequirePublicationFailure([&] {
				static_cast<void>(Call(vm, instance, "find", {std::string("asset/item")}));
			},
			    "binding limit");
			Require(cache->Info(handle).References == 2 && uploads == 2 && preparation->LiveDocumentCount() == 1, "document-capacity rejection must release the rejected find pin without allocating another document");
			sgre::scene::ImageAssetCatalog complete_only(textures);
			RequireFailure([&] {
				static_cast<void>(complete_only.Prepare(retained.Document, std::make_shared<const ByteVector>(retained.Image->MetadataStorage())));
			});
			asset_lease = catalog.Prepare(retained.Image);
			source = retained.Image->Source(0);
			const auto texture = source.Texture();
			Require(asset_lease->Document.get() == retained.Document.get() && asset_lease->Backing.get() == &retained.Image->MetadataStorage() && asset_lease->Receiving == retained.Image && catalog.DocumentCount() == 1, "Image catalog must adopt the exact purged resource and genuine preload identity");
			const std::array<sgre::script::SquirrelValue, 2> image_args{vm.RootTable(), data};
			const auto image = vm.Invoke(native.ImageClass(), image_args).Value;
			auto image_root = vm.Retain(image);
			Require(std::get<std::int32_t>(vm.ReadSlot(image, std::string("width"))) == 1 && std::get<std::int32_t>(vm.ReadSlot(image, std::string("height"))) == 1 && uploads == 2 && textures.Get(texture).References == 2 && native.BorrowNativeImage(image).Source(0).Texture() == texture, "actual Image constructor must acquire the selected real preload without redecoding retired pixel bytes");
			Require(!preparation->CanClose(), "actual prepared image pins must prevent cache preparation close");
			static_cast<void>(Call(vm, instance, "unload"));
			Require(cache->Info(handle).References == 1 && source.IsLive() && releases.empty(), "unload must preserve returned PSB and selected Image ownership");
			native.ReleaseInstance(image);
			image_root = {};
			Require(textures.Get(texture).References == 1 && source.IsLive() && releases.empty(), "Image retirement must retain the genuine receiving preloads");
			retained = {};
			data_root = {};
			instance_root = {};
			vm.Close();
		}
		Require(cache->State().ActiveEntryNum == 1 && preparation->LiveDocumentCount() == 1 && source.IsLive() && releases.empty(), "a portable catalog lease must retain the real cache entry after VM retirement");
		files.Close();
		processor.Update();
		processor.Close();
		background.Close();
		cache.reset();
		Require(asset_lease->Document->SerializedSize == decoded.size() && asset_lease->Model.Tiles.size() == 1 && source.IsLive() && preparation->LiveDocumentCount() == 1 && releases.empty(), "asset lease must preserve actual metadata and textures beyond the resource-cache facade");
	}
	asset_lease.reset();
	catalog.Prune();
	preparation->RetirePending();
	Require(catalog.DocumentCount() == 0 && preparation->LiveDocumentCount() == 0 && textures.Count() == 0 && !source.IsLive() && releases == std::vector<std::uint32_t>{1, 2}, "last actual asset pin must release all receiving textures in source order");
	preparation->Close();
}

static void TestStopQueuedImageWorker(const std::filesystem::path& directory) {
	std::uint32_t uploads = 0;
	sgre::scene::TextureCache textures([&](auto, const auto&) {
		return sgre::render::SpriteTextureHandle{++uploads, 1, &uploads};
	},
	    [](auto) {
	    });
	auto preparation = std::make_shared<sgre::scene::ImagePreparationService>(textures);
	ResourceCache cache(0);
	ArchiveMounts mounts;
	BackgroundCallManager background;
	FileReadProcessor processor(cache, mounts, background);
	MountImageFixture(processor, mounts, background, directory, ByteVector(kReceivingImageFixture.begin(), kReceivingImageFixture.end()));
	ScriptResourceFileService files(cache, mounts, processor, "synthetic-seed", {}, {}, {}, preparation);
	std::string failure;
	{
		sgre::script::SquirrelVm vm;
		sgre::script::ScriptObjectBindings objects(vm);
		sgre::script::ScriptPsbBindings psb(vm, objects);
		sgre::script::ScriptBinaryBindings binary(vm, objects);
		sgre::script::ScriptResourceBindings resources(vm, objects, psb, binary, files.RequestService(), [&](auto, auto error) {
			failure = error;
		});
		const StopImagePreparation stop_before_worker_join(*preparation);
		const std::array<sgre::script::SquirrelValue, 1> receiver{vm.RootTable()};
		const auto instance = vm.Invoke(resources.ResourceClass(), receiver).Value;
		static_cast<void>(Call(vm, instance, "loadRaw", {std::string("asset/item")}));
		const auto handle = *cache.FindNamedResource("asset/item");
		processor.Update();
		background.PostLoop();
		WaitForActualImageJob(*preparation);
		preparation->Stop();
		DrainLane(background);
		Require(!std::get<bool>(vm.ReadSlot(instance, std::string("loading"))) && failure.find("cancelled before actual texture production") != std::string::npos && cache.Info(handle).Completion == sgre::resource::ResourceCompletion::Failed && uploads == 0 && preparation->LiveDocumentCount() == 0 && preparation->PendingCount() == 0, "stopping the actual preparation queue must fail its worker without publishing a fabricated Image owner");
		Require(vm.Type(Call(vm, instance, "find", {std::string("asset/item")})) == sgre::script::SquirrelValueType::Null, "cancelled Image Resource.find must return null");
		static_cast<void>(Call(vm, instance, "unload"));
		vm.Close();
	}
	Require(cache.State().ActiveEntryNum == 0 && textures.Count() == 0, "cancelled file worker must release its actual cache target");
	files.Close();
	processor.Update();
	processor.Close();
	background.Close();
	preparation->Close();
}

static void TestOrdinaryPsbKeepsCompleteBacking(const std::filesystem::path& directory) {
	std::filesystem::create_directories(directory);
	const auto decoded = PayloadPsb();
	ResourceCache cache(0);
	ArchiveMounts mounts;
	BackgroundCallManager background;
	FileReadProcessor processor(cache, mounts, background);
	MountActual(processor, mounts, background, directory / "index.bin", directory / "body.bin", decoded);
	ScriptResourceFileService files(cache, mounts, processor, "synthetic-seed");
	auto poll = files.RequestService()("asset/item", ScriptResourceRequestMode::PsbRaw);
	const auto handle = *cache.FindNamedResource("asset/item");
	processor.Update();
	DrainLane(background);
	auto result = poll();
	Require(result && result->Document && result->Backing && !result->Image && !result->Font && *result->Backing == decoded && !cache.HasImageFile(handle), "ordinary PSB publication must preserve its complete backing and distinct payload kind");
	auto pin = result->RetainPsb();
	Require(pin.Document && pin.Backing && !pin.Image && *pin.Backing == decoded && cache.Info(handle).References == 2, "ordinary PSB find must preserve its established real manager pin");
	poll = {};
	result.reset();
	pin = {};
	Require(!cache.Contains(handle), "ordinary PSB must retire after its actual final pin");
	files.Close();
	processor.Update();
	processor.Close();
	background.Close();
}

static void TestMissingActualImageProducer(const std::filesystem::path& directory) {
	ResourceCache cache(0);
	ArchiveMounts mounts;
	BackgroundCallManager background;
	FileReadProcessor processor(cache, mounts, background);
	MountImageFixture(processor, mounts, background, directory, ByteVector(kReceivingImageFixture.begin(), kReceivingImageFixture.end()));
	ScriptResourceFileService files(cache, mounts, processor, "synthetic-seed");
	auto poll = files.RequestService()("asset/item", ScriptResourceRequestMode::PsbRaw);
	const auto handle = *cache.FindNamedResource("asset/item");
	processor.Update();
	DrainLane(background);
	RequirePublicationFailure([&] {
		static_cast<void>(poll());
	},
	    "Resource publication requires unsupported native production");
	Require(cache.Info(handle).UnsupportedProduction && cache.Info(handle).Completion == sgre::resource::ResourceCompletion::Failed, "missing Image producer must retain its exact real unsupported publication failure");
	poll = {};
	files.Close();
	processor.Update();
	processor.Close();
	background.Close();
}

int main(int argc, char** argv) {
	try {
		Require(argc == 2, "Image resource publication fixture requires an ignored output directory");
		const std::filesystem::path directory(argv[1]);
		TestActualReceivingAndTypedFind(directory / "raw", false);
		TestActualReceivingAndTypedFind(directory / "wrapped", true);
		TestStopQueuedImageWorker(directory / "cancelled");
		TestOrdinaryPsbKeepsCompleteBacking(directory / "ordinary");
		TestMissingActualImageProducer(directory / "unconfigured");
		std::cout << "Image resource publication: raw/wrapped workers, typed VM/catalog pins, capacity rejection and cancellation passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
