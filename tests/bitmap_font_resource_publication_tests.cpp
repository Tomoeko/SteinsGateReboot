#include "sgre/scene/bitmap_font_preparation_service.hpp"

#include "bitmap_font_receiving_fixture.hpp"
#include "resource_file_fixture.hpp"

#include <chrono>
#include <thread>

static void WaitForActualFontJob(sgre::scene::BitmapFontPreparationService& preparation) {
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (preparation.PendingCount() == 0) {
		if (std::chrono::steady_clock::now() >= deadline) {
			preparation.Stop();
			throw std::runtime_error("actual resource worker did not reach font texture preparation");
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}

static sgre::script::SquirrelValue Call(sgre::script::SquirrelVm& vm, const sgre::script::SquirrelValue& instance, std::string name, std::vector<sgre::script::SquirrelValue> parameters = {}) {
	std::vector<sgre::script::SquirrelValue> arguments{instance};
	arguments.insert(arguments.end(), parameters.begin(), parameters.end());

	return vm.Invoke(vm.GetSlot(instance, std::move(name)), arguments).Value;
}

static void MountFontFixture(FileReadProcessor& processor, ArchiveMounts& mounts, BackgroundCallManager& background, const std::filesystem::path& index_path, const std::filesystem::path& body_path, const ByteVector& body) {
	Require(body.size() <= 32767, "font fixture exceeds its independent signed16 entry-size atom");
	auto index_bytes = IndexPsb(4);
	// Widen only the final entry-size atom from signed8 to signed16 and relocate following sections by one byte.
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
	Require(index.Entries.size() == 1 && index.Entries[0].Name == "item" && index.Entries[0].Offset == 0 && index.Entries[0].Size == body.size(), "font fixture index differs from its declared physical region");
	WriteFile(body_path, body);
	WriteFile(index_path, Wrap(index_bytes, "index_info.psb.m"));
	sgre::resource::ArchiveReadMountInput input;
	input.Prefix = "asset/";
	input.IndexIdentity = "index_info.psb.m";
	input.BodyIdentity = "actual-font-body";
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
	Require(mounts.MountCount() == 1 && !mounts.HasInFlightReadMount(), "actual font fixture mount did not complete receiving publication");
}

static void TestActualReceivingAndTypedFind(const std::filesystem::path& directory, bool wrapped) {
	std::uint32_t uploads = 0;
	std::uint32_t releases = 0;
	const auto owner = std::this_thread::get_id();
	auto preparation = std::make_shared<sgre::scene::BitmapFontPreparationService>([&](auto, const auto&) {
		Require(std::this_thread::get_id() == owner, "actual file worker performed texture upload outside preparation owner");

		return sgre::render::SpriteTextureHandle{++uploads, 1, &uploads};
	},
	    [&](auto) {
		    Require(std::this_thread::get_id() == owner, "typed resource retirement performed texture release outside preparation owner");
		    ++releases;
	    });
	ResourceCache cache(0);
	ArchiveMounts mounts;
	BackgroundCallManager background;
	FileReadProcessor processor(cache, mounts, background);
	const ByteVector decoded(kReceivingFontFixture.begin(), kReceivingFontFixture.end());
	MountFontFixture(processor, mounts, background, directory / "index.bin", directory / "body.bin", wrapped ? Wrap(decoded, "asset/item.m") : decoded);
	ScriptResourceFileService files(cache, mounts, processor, "synthetic-seed", {}, preparation);
	std::size_t errors = 0;
	{
		sgre::script::SquirrelVmLimits limits;
		limits.ReclaimObjects = true;
		sgre::script::SquirrelVm vm(limits);
		sgre::script::ScriptObjectBindings objects(vm);
		sgre::script::ScriptPsbBindings psb(vm, objects);
		sgre::script::ScriptBinaryBindings binary(vm, objects);
		sgre::script::ScriptResourceBindings resources(vm, objects, psb, binary, files.RequestService(), [&](auto, auto) {
			++errors;
		});
		const std::array<sgre::script::SquirrelValue, 1> receiver{vm.RootTable()};
		const auto instance = vm.Invoke(resources.ResourceClass(), receiver).Value;
		static_cast<void>(Call(vm, instance, wrapped ? "load" : "loadRaw", {std::string("asset/item")}));
		const auto handle = *cache.FindNamedResource("asset/item");
		Require(std::get<bool>(vm.ReadSlot(instance, std::string("loading"))) && cache.Info(handle).References == 1, "actual Resource target must retain one genuine pending manager reference");
		processor.Update();
		background.PostLoop();
		WaitForActualFontJob(*preparation);
		Require(uploads == 0 && cache.Info(handle).Completion == sgre::resource::ResourceCompletion::Pending && std::get<bool>(vm.ReadSlot(instance, std::string("loading"))), "receiving completion cannot precede actual font upload and purge");
		Require(preparation->PreparePending() == 1, "actual font texture producer did not consume its real worker job");
		DrainLane(background);
		Require(!std::get<bool>(vm.ReadSlot(instance, std::string("loading"))) && errors == 0 && uploads == 2 && cache.HasBitmapFontFile(handle) && cache.Info(handle).ChargedBytes == decoded.size() && cache.Bytes(handle).size() + 2 == decoded.size(), "typed font result must preserve original PSB charge separately from purged retained metadata");
		RequireFailure([&] {
			static_cast<void>(cache.PsbFile(handle));
		});
		RequireFailure([&] {
			static_cast<void>(cache.PinBytes(handle));
		});
		const auto data = Call(vm, instance, "find", {std::string("asset/item")});
		auto retained = psb.DocumentAsset(data);
		Require(retained.Font && retained.Document.get() == &retained.Font->Document() && !retained.Backing && cache.Info(handle).References == 2, "Resource.find must retain one separate typed manager reference without fictitious full backing");
		Require(std::get<std::int32_t>(Call(vm, data, "totalStreamEntitySize")) == 2, "purged font PSB must preserve native original stream-entity size");
		const auto root = vm.ReadSlot(data, std::string("root"));
		Require(sgre::script::StringText(vm.ReadSlot(root, std::string("id"))) == "font", "typed returned PSB root must expose its original immutable metadata");
		RequireFailure([&] {
			static_cast<void>(psb.WrapDocument(retained.Document, std::make_shared<const ByteVector>(retained.Font->MetadataStorage())));
		});
		sgre::scene::BitmapFontTextureOwner provider(preparation->Cache(), retained.Font);
		Require(uploads == 2 && preparation->Cache().Get(provider.Source(0).Texture()).References == 2, "provider from typed returned resource must share actual preload cache entries after purge");
		static_cast<void>(Call(vm, instance, "unload"));
		Require(cache.Info(handle).References == 1 && provider.Source(0).IsLive(), "Resource.unload must release its target and preserve the genuine returned-font pin");
		provider.Close();
		retained.Document.reset();
		retained.Font.reset();
		vm.Close();
	}
	Require(cache.State().ActiveEntryNum == 0 && preparation->LiveDocumentCount() == 0 && releases == 2, "VM and returned PSB retirement must balance manager and preload references");
	files.Close();
	processor.Update();
	processor.Close();
	background.Close();
	preparation->Close();
}

static void TestOrdinaryPsbStillOwnsCompleteBacking(const std::filesystem::path& directory) {
	const auto decoded = PayloadPsb();
	ResourceCache cache(0);
	ArchiveMounts mounts;
	BackgroundCallManager background;
	FileReadProcessor processor(cache, mounts, background);
	MountActual(processor, mounts, background, directory / "ordinary-index.bin", directory / "ordinary-body.bin", decoded);
	ScriptResourceFileService files(cache, mounts, processor, "synthetic-seed");
	auto poll = files.RequestService()("asset/item", ScriptResourceRequestMode::PsbRaw);
	const auto handle = *cache.FindNamedResource("asset/item");
	processor.Update();
	DrainLane(background);
	auto result = poll();
	Require(result && result->Document && result->Backing && !result->Font && *result->Backing == decoded && !cache.HasBitmapFontFile(handle), "ordinary PSB publication must preserve its complete retained backing and separate payload kind");
	auto pin = result->RetainPsb();
	Require(pin.Document && pin.Backing && !pin.Font && *pin.Backing == decoded && cache.Info(handle).References == 2, "ordinary PSB find must preserve its established two-alias one-manager-pin API");
	poll = {};
	result.reset();
	pin.Document.reset();
	pin.Backing.reset();
	Require(!cache.Contains(handle), "ordinary PSB last pin must retain established zero-limit retirement");
	files.Close();
	processor.Update();
	processor.Close();
	background.Close();
}

int main(int argc, char** argv) {
	try {
		Require(argc == 2, "font resource fixture requires an ignored output directory");
		const std::filesystem::path directory(argv[1]);
		std::filesystem::create_directories(directory);
		TestActualReceivingAndTypedFind(directory, false);
		TestActualReceivingAndTypedFind(directory, true);
		TestOrdinaryPsbStillOwnsCompleteBacking(directory);
		std::cout << "bitmap font resource publication: raw/wrapped font and ordinary PSB fixture groups passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
