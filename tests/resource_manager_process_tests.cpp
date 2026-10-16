#include "resource_file_fixture.hpp"

#include "sgre/resource/resource_manager_process.hpp"

using sgre::resource::ResourceManagerProcess;

static sgre::io::PreparedRawFileRead RawSource(const std::filesystem::path& file) {
	sgre::io::PreparedRawFileRead source;
	source.Filename = file;
	source.MaxBytes = 4096;

	return source;
}

static void TestConstructionAndActualUpdate(const std::filesystem::path& directory) {
	const auto file = directory / "raw.bin";
	WriteFile(file, {3, 5, 7, 9});
	ArchiveMounts mounts;
	BackgroundCallManager background;
	ResourceManagerProcess owner(mounts, background);
	auto& cache = owner.Cache();
	auto& processor = owner.Processor();
	Require(cache.Limit() == 0 && owner.Tasks().Serial().Count() == 0 && owner.Tasks().Parallel().Count() == 1 && processor.UsesReceivingOwners(cache, mounts), "native manager construction must own limit-zero cache and one actual persistent Parallel reader");
	RequireFailure([&]() {
		FileReadProcessor duplicate(cache, mounts, background);
	});
	const auto handle = cache.Acquire("raw");
	static_cast<void>(processor.QueuePreparedRaw(handle, "raw", RawSource(file)));
	owner.Update();
	owner.Draw(0x87654321);
	Require(processor.Tasks().Serial().Count() == 1 && processor.PendingRawCount() == 1 && cache.Info(handle).Completion == sgre::resource::ResourceCompletion::Pending, "manager update must enqueue real reader work without an implicit wake or fake completion");
	DrainLane(background);
	Require(processor.PendingRawCount() == 0 && cache.Info(handle).Residency == sgre::resource::ResourceResidency::Loading && cache.Info(handle).Completion == sgre::resource::ResourceCompletion::Pending, "worker completion must precede actual manager map publication");
	owner.Update();
	Require(cache.Info(handle).Residency == sgre::resource::ResourceResidency::Active && std::ranges::equal(cache.Bytes(handle), std::array<std::uint8_t, 4>{3, 5, 7, 9}) && processor.Tasks().Serial().Count() == 0, "genuine manager onProcess must publish actual bytes before child serial retirement in that visit");
	cache.Release(handle);
	Require(!cache.Contains(handle), "native initial limit zero must release the last completed target");
	owner.Close();
	Require(owner.IsClosed() && processor.IsClosed() && cache.CanRetireReadDestinations() && owner.Tasks().Parallel().Count() == 0, "manager Close must retire/detach actual processor before its owned receiving cells");
	RequireFailure([&]() {
		owner.Update();
	});
	RequireFailure([&]() {
		static_cast<void>(owner.Cache());
	});
	background.Close();
}

static void TestMountedPublicationAndPin(const std::filesystem::path& directory) {
	ArchiveMounts mounts;
	BackgroundCallManager background;
	sgre::script::ScriptPsbDocumentAsset found;
	const auto payload = PayloadPsb();
	{
		ResourceManagerProcess owner(mounts, background);
		MountActual(owner.Processor(), mounts, background, directory / "index.bin", directory / "body.bin", Wrap(payload, "asset/item.m"));
		ScriptResourceFileService service(owner.Cache(), mounts, owner.Processor(), "synthetic-seed");
		auto poll = service.RequestService()("asset/item", ScriptResourceRequestMode::PsbWrapped);
		const auto handle = *owner.Cache().FindNamedResource("asset/item");
		owner.Update();
		DrainLane(background);
		Require(owner.Cache().Info(handle).Residency == sgre::resource::ResourceResidency::Loading, "manager map must not promote merely because its worker finished");
		owner.Update();
		auto asset = poll();
		Require(asset && asset->Document && *asset->Backing == payload && owner.Cache().Info(handle).Residency == sgre::resource::ResourceResidency::Active && owner.Processor().Tasks().Serial().Count() == 0, "actual common/map/reader join must produce immutable PSB publication");
		found = asset->RetainPsb();
		Require(found.Document.get() == asset->Document.get() && owner.Cache().Info(handle).References == 2, "real find result must preserve document identity and own its separate cache reference");
		poll = {};
		asset.reset();
		Require(service.CanClose() && owner.Cache().Info(handle).References == 1, "find pin must survive actual target retirement independently");
		service.Close();
		owner.Close();
		mounts.Unmount("asset/");
		Require(mounts.RetainedPsbObjectCount() == 1, "manager retirement/unmount must not invent final mounted-index retirement");
	}
	Require(found.Document && std::get<sgre::resource::PsbArray>(found.Document->Root.Data).size() == 1 && *found.Backing == payload, "existing actual find pin must retain cache data beyond C++ facade retirement");
	found = {};
	background.Close();
}

static void TestQueuedAndCompletedFence(const std::filesystem::path& directory) {
	const auto file = directory / "raw.bin";
	WriteFile(file, {11, 13, 17});
	for (const bool completed : {false, true}) {
		ArchiveMounts mounts;
		BackgroundCallManager background;
		ResourceManagerProcess owner(mounts, background);
		auto& cache = owner.Cache();
		auto& processor = owner.Processor();
		const auto handle = cache.Acquire("raw");
		static_cast<void>(processor.QueuePreparedRaw(handle, "raw", RawSource(file)));
		if (completed) {
			owner.Update();
			DrainLane(background);
		}
		Require(!cache.CanRetireReadDestinations(), "actual processor attachment must keep receiving-cell owner alive before fence");
		owner.Close();
		Require(owner.IsClosed() && processor.IsClosed() && cache.CanRetireReadDestinations(), "real Close must fence/detach the actual processor before receiving-cell destruction");
		cache.Advance();
		Require(cache.Info(handle).Completion == (completed ? sgre::resource::ResourceCompletion::Succeeded : sgre::resource::ResourceCompletion::Failed), "real worker completion must survive Close while queued cancellation remains failure through cache publication");
		cache.Release(handle);
		background.Close();
	}
}

static void TestForeignOwnerPreflight() {
	ArchiveMounts mounts;
	BackgroundCallManager background;
	ResourceManagerProcess owner(mounts, background);
	auto result = std::async(std::launch::async, [&]() {
		Require(!owner.CanClose(), "foreign-thread owner preflight must reject");
		RequireFailure([&]() {
			owner.Close();
		});
	});
	result.get();
	Require(!owner.IsClosed() && owner.Tasks().Parallel().Count() == 1, "foreign retirement must reject before common/root mutation");
	owner.Close();
	background.Close();
}

int main(int argc, char** argv) {
	try {
		if (argc != 2) {
			throw std::runtime_error("ResourceManager owner fixtures require one ignored directory");
		}
		const std::filesystem::path directory(argv[1]);
		std::filesystem::create_directories(directory);
		TestConstructionAndActualUpdate(directory);
		TestMountedPublicationAndPin(directory);
		TestQueuedAndCompletedFence(directory);
		TestForeignOwnerPreflight();
		for (const auto name : {"raw.bin", "index.bin", "body.bin"}) {
			std::filesystem::remove(directory / name);
		}
		std::cout << "four actual ResourceManager Common/cache/reader/receiving lifetime groups pass\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
