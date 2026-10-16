#include "resource_file_fixture.hpp"

#include <cstddef>
#include <memory>
#include <string_view>
#include <variant>

static void TestFourActualModes(const std::filesystem::path& directory) {
	const auto decoded = PayloadPsb();
	const auto document = sgre::resource::ReadPsbMetadata(decoded, decoded.size());
	Require(std::get<sgre::resource::PsbArray>(document.Root.Data).size() == 1 && std::get<bool>(std::get<sgre::resource::PsbArray>(document.Root.Data)[0].Data), "independent PSB payload does not encode its stated root");
	for (const auto mode : {ScriptResourceRequestMode::PsbWrapped, ScriptResourceRequestMode::PsbRaw, ScriptResourceRequestMode::BinaryWrapped, ScriptResourceRequestMode::BinaryRaw}) {
		const bool wrapped = mode == ScriptResourceRequestMode::PsbWrapped || mode == ScriptResourceRequestMode::BinaryWrapped;
		const bool psb = mode == ScriptResourceRequestMode::PsbWrapped || mode == ScriptResourceRequestMode::PsbRaw;
		ResourceCache cache(0);
		ArchiveMounts mounts;
		BackgroundCallManager background;
		FileReadProcessor processor(cache, mounts, background);
		MountActual(processor, mounts, background, directory / "index.bin", directory / "body.bin", wrapped ? Wrap(decoded, "asset/item.m") : decoded);
		ScriptResourceFileService service(cache, mounts, processor, "synthetic-seed");
		auto request = service.RequestService();
		auto poll = request("asset/item", mode);
		const auto handle = *cache.FindNamedResource("asset/item");
		Require(!poll() && cache.Info(handle).References == 1 && cache.Info(handle).Residency == sgre::resource::ResourceResidency::Loading && !service.CanClose(), "actual target must own exactly one acquired pending reference");
		RequireFailure([&]() {
			service.Close();
		});
		mounts.Unmount("asset/");
		processor.Update();
		DrainLane(background);
		auto asset = poll();
		Require(asset && asset->Error.empty() && asset->Backing && *asset->Backing == decoded && cache.Info(handle).References == 1 && cache.Info(handle).ChargedBytes == decoded.size() && processor.Tasks().Serial().Count() == 1, "actual receiving cell must publish decoded payload before background retirement and without a host completion callback");
		Require(asset->Kind == (psb ? sgre::script::ScriptResourceDataKind::Psb : sgre::script::ScriptResourceDataKind::Binary) && static_cast<bool>(asset->Document) == psb, "actual receiver payload kind differs from its immutable request");
		poll = {};
		if (psb) {
			auto found = asset->RetainPsb();
			auto copied = found.Document;
			Require(cache.Info(handle).References == 2 && found.Document.get() == asset->Document.get(), "PSB find must acquire exactly one distinct reference with the original document identity");
			asset.reset();
			Require(cache.Info(handle).References == 1 && !std::get<sgre::resource::PsbArray>(copied->Root.Data).empty(), "Resource target retirement must leave only the PSB find reference");
			found.Document.reset();
			found.Backing.reset();
			Require(cache.Info(handle).References == 1 && copied->SerializedSize == decoded.size(), "document alias alone must retain the same actual find pin");
			copied.reset();
		} else {
			auto found = asset->RetainBacking();
			auto copied = found;
			Require(cache.Info(handle).References == 2 && *found == decoded, "Binary find must acquire one distinct actual manager reference");
			asset.reset();
			Require(cache.Info(handle).References == 1, "Binary return must not prolong the original Resource target lease");
			found.reset();
			Require(cache.Info(handle).References == 1, "ordinary copies share one find reference");
			copied.reset();
		}

		Require(!cache.Contains(handle) && service.CanClose(), "zero-limit last find retirement must release the actual cache generation and target lease");
		service.Close();
		RequireFailure([&]() {
			static_cast<void>(request("asset/item", mode));
		});
		processor.Update();
		processor.Close();
		background.Close();
	}
}

static void TestCoalescingAndModePreflight(const std::filesystem::path& directory) {
	const auto payload = PayloadPsb();
	ResourceCache cache(1024);
	ArchiveMounts mounts;
	BackgroundCallManager background;
	FileReadProcessor processor(cache, mounts, background);
	MountActual(processor, mounts, background, directory / "index.bin", directory / "body.bin", payload);
	ScriptResourceFileService service(cache, mounts, processor, "synthetic-seed");
	auto request = service.RequestService();
	auto first = request("asset/item", ScriptResourceRequestMode::PsbRaw);
	auto second = request("asset/item", ScriptResourceRequestMode::PsbRaw);
	const auto handle = *cache.FindNamedResource("asset/item");
	Require(cache.Info(handle).References == 2 && processor.PendingRawCount() == 1, "coalescing must retain two actual target references but publish only one physical request");
	RequireFailure([&]() {
		static_cast<void>(request("asset/item", ScriptResourceRequestMode::BinaryRaw));
	});
	Require(cache.Info(handle).References == 2 && processor.PendingRawCount() == 1, "unsupported mixed-mode coalescing must reject before reference or queue mutation");
	auto distinct = request("asset\\item", ScriptResourceRequestMode::PsbRaw);
	const auto distinct_handle = *cache.FindNamedResource("asset\\item");
	Require(handle != distinct_handle && processor.PendingRawCount() == 2, "mounted normalization must not rewrite immutable cache/Resource target identities");
	processor.Update();
	DrainLane(background);
	auto first_asset = first();
	auto second_asset = second();
	auto distinct_asset = distinct();
	Require(first_asset && second_asset && distinct_asset && first_asset->Document.get() == second_asset->Document.get() && first_asset->Document.get() != distinct_asset->Document.get(), "coalesced and spelling-distinct actual PSB output identities differ from their receiving owners");
	first = {};
	second = {};
	distinct = {};
	first_asset.reset();
	second_asset.reset();
	distinct_asset.reset();
	cache.ClearCache();
	Require(!cache.Contains(handle) && !cache.Contains(distinct_handle), "actual target release plus clear must retire both receiver generations");
	service.Close();
	processor.Close();
	background.Close();
}

static void TestQueuedClearAndFailureOwnership(const std::filesystem::path& directory) {
	const auto payload = PayloadPsb();
	ResourceCache cache(0);
	ArchiveMounts mounts;
	BackgroundCallManager background;
	FileReadProcessor processor(cache, mounts, background);
	const auto body = directory / "body.bin";
	MountActual(processor, mounts, background, directory / "index.bin", body, payload);
	ScriptResourceFileService service(cache, mounts, processor, "synthetic-seed");
	auto request = service.RequestService();
	auto abandoned = request("asset/item", ScriptResourceRequestMode::PsbRaw);
	const auto old = *cache.FindNamedResource("asset/item");
	abandoned = {};
	Require(cache.Info(old).References == 0 && processor.PendingRawCount() == 1, "last pending target release must retain the actual receiving cell and must not invent cancel3");
	cache.ClearCache();
	Require(processor.PendingRawCount() == 0, "clear must physically remove the real unreferenced typed pending pointer");
	cache.Advance();
	Require(!cache.Contains(old), "cancelled typed receiving entry must retire only after concrete completion import");
	auto failed = request("asset/item", ScriptResourceRequestMode::PsbRaw);
	const auto handle = *cache.FindNamedResource("asset/item");
	std::filesystem::remove(body);
	processor.Update();
	DrainLane(background);
	auto error = failed();
	Require(error && !error->Error.empty() && !error->Document && !error->Backing && cache.Info(handle).References == 1 && cache.Info(handle).Completion == sgre::resource::ResourceCompletion::Failed, "actual physical read failure must preserve its acquired failed target until unload");
	failed = {};
	Require(cache.Info(handle).References == 1, "prepared failure observer retirement must not drop the Resource target reference early");
	error.reset();
	Require(!cache.Contains(handle) && service.CanClose(), "failed target retirement must release its actual active-map reference");
	service.Close();
	processor.Close();
	background.Close();
}

static void TestAdmissionAndOwnerPreflight(const std::filesystem::path& directory) {
	ResourceCache cache(0);
	ArchiveMounts mounts;
	BackgroundCallManager background;
	FileReadProcessor processor(cache, mounts, background);
	ArchiveMounts other_mounts;
	RequireFailure([&]() {
		ScriptResourceFileService invalid(cache, other_mounts, processor, "synthetic-seed");
	});
	ScriptResourceFileService service(cache, mounts, processor, "synthetic-seed");
	auto request = service.RequestService();
	RequireFailure([&]() {
		static_cast<void>(request("missing/item", ScriptResourceRequestMode::BinaryRaw));
	});
	Require(cache.State().LoadingEntryNum == 0, "unrecovered loose fallback must not publish a counterfeit cache target");
	MountActual(processor, mounts, background, directory / "index.bin", directory / "body.bin", PayloadPsb());
	RequireFailure([&]() {
		static_cast<void>(request("asset/item.m", ScriptResourceRequestMode::BinaryRaw));
	});
	Require(cache.State().LoadingEntryNum == 0, "sorter-key unsupported domain must reject before target admission");
	service.Close();
	processor.Close();
	background.Close();
}

static void TestUnsupportedAndRollback(const std::filesystem::path& directory) {
	{
		auto unsupported = PayloadPsb();
		unsupported[6] = 1;
		ResourceCache cache(0);
		ArchiveMounts mounts;
		BackgroundCallManager background;
		FileReadProcessor processor(cache, mounts, background);
		MountActual(processor, mounts, background, directory / "index.bin", directory / "body.bin", unsupported);
		ScriptResourceFileService service(cache, mounts, processor, "synthetic-seed");
		auto poll = service.RequestService()("asset/item", ScriptResourceRequestMode::PsbRaw);
		const auto handle = *cache.FindNamedResource("asset/item");
		processor.Update();
		DrainLane(background);
		RequireFailure([&]() {
			static_cast<void>(poll());
		});
		Require(cache.Info(handle).UnsupportedProduction && cache.Info(handle).References == 1 && !service.CanClose(), "unrecovered filtered/derived work must block publication rather than fabricate completed script data");
		poll = {};
		Require(!cache.Contains(handle) && service.CanClose(), "unsupported source-domain retirement must preserve actual reference cleanup");
		service.Close();
		processor.Close();
		background.Close();
	}
	{
		ResourceCache cache(0);
		ArchiveMounts mounts;
		BackgroundCallManager background;
		sgre::io::RawReadQueueLimits limits;
		limits.MaxRequests = 1;
		FileReadProcessor processor(cache, mounts, background, limits);
		MountActual(processor, mounts, background, directory / "index.bin", directory / "body.bin", PayloadPsb());
		ScriptResourceFileService service(cache, mounts, processor, "synthetic-seed");
		auto request = service.RequestService();
		auto first = request("asset/item", ScriptResourceRequestMode::PsbRaw);
		RequireFailure([&]() {
			static_cast<void>(request("asset\\item", ScriptResourceRequestMode::PsbRaw));
		});
		Require(!cache.Contains("asset\\item") && cache.State().LoadingEntryNum == 1 && processor.PendingRawCount() == 1, "failed real queue admission must roll back only its unstarted new cache slot");
		first = {};
		service.Close();
		processor.Close();
		cache.Advance();
		Require(cache.State().LoadingEntryNum == 0, "concrete queued cancellation/fence must leave no abandoned receiving slot");
		background.Close();
	}
}

static void TestDerivedGateAndCellProtocol(const std::filesystem::path& directory) {
	auto document = sgre::resource::ReadPsbMetadata(kArchivePsbFixture, kArchivePsbFixture.size());
	Require(!sgre::io::RequiresDerivedPsbProduction(document), "observed archive id/no-spec must skip actual derived production");
	std::get<sgre::resource::PsbObject>(document.Root.Data).emplace_back("spec", sgre::resource::PsbValue{});
	Require(sgre::io::RequiresDerivedPsbProduction(document), "present null spec remains outside the proven missing-spec domain");
	std::erase_if(std::get<sgre::resource::PsbObject>(document.Root.Data), [](const auto& entry) {
		return entry.first == "id";
	});
	Require(!sgre::io::RequiresDerivedPsbProduction(document), "native derived gate requires both id and spec");
	sgre::io::ResourceReadFormat format;
	format.Kind = sgre::io::ResourceFileKind::Psb;
	sgre::io::ResourceFileReadDestination destination(format);
	sgre::io::PreparedResourceFileRead source;
	source.Format = format;
	source.Region.Filename = directory / "cell.bin";
	source.Region.MaxBytes = 4096;
	source.OriginalFilename = "cell.psb";
	source.MaxDecodedBytes = 4096;
	source.MaxMetadataBytes = 4096;
	WriteFile(source.Region.Filename, PayloadPsb());
	destination.MarkQueued();
	Require(!destination.CanRetire() && destination.Completion() == FileReadCompletion::Pending, "queued typed cell must retain its actual request borrow");
	destination.Select();
	Require(!destination.CancelQueued(), "selected typed work cannot become pending cancel3");
	Require(destination.ReadPhysicalSelected(source), "actual selected physical PSB region failed");
	Require(destination.PrepareSelectedObject(source) == FileReadCompletion::Succeeded && destination.Completion() == FileReadCompletion::Pending && !destination.CanRetire(), "prepared object cannot publish native success or release its worker borrow early");
	destination.PublishSelected();
	Require(destination.Completion() == FileReadCompletion::Succeeded && destination.CanRetire(), "last typed worker publication must expose actual completed receiving ownership");
	sgre::io::ResourceFileReadDestination cancelled(format);
	cancelled.MarkQueued();
	Require(cancelled.CancelQueued() && cancelled.Completion() == FileReadCompletion::Cancelled && cancelled.CanRetire(), "actual still-queued cancellation must publish3 and release only that queue borrow");
	std::filesystem::remove(source.Region.Filename);
}

static ByteVector AudioReceivingPsb(std::string_view id, std::string_view spec) {
	Require(id.size() <= 32 && spec.size() <= 32 && id.find('\0') == std::string_view::npos && spec.find('\0') == std::string_view::npos, "independent audio receiving identifiers exceed their fixture domain");
	ByteVector bytes(44, 0);
	bytes[0] = 'P';
	bytes[1] = 'S';
	bytes[2] = 'B';
	bytes[4] = 3;
	WriteWord(bytes, 8, 44);
	WriteWord(bytes, 12, 44);

	// Independent key trie: root->i->d->NUL and root->s->p->e->c->NUL.
	std::array<std::uint8_t, 123> bases{};
	std::array<std::uint8_t, 123> parents{};
	bases[100] = 121;
	bases[99] = 122;
	parents[100] = 105;
	parents[112] = 115;
	parents[101] = 112;
	parents[99] = 101;
	parents[121] = 100;
	parents[122] = 99;
	bytes.insert(bytes.end(), {13, 123, 13});
	bytes.insert(bytes.end(), bases.begin(), bases.end());
	bytes.insert(bytes.end(), {13, 123, 13});
	bytes.insert(bytes.end(), parents.begin(), parents.end());
	bytes.insert(bytes.end(), {13, 2, 13, 121, 122});
	WriteWord(bytes, 36, static_cast<std::uint32_t>(bytes.size()));

	// Object keys [id,spec], value offsets [0,2], and two string-reference atoms.
	bytes.insert(bytes.end(), {33, 13, 2, 13, 0, 1, 13, 2, 13, 0, 2, 21, 0, 21, 1});
	WriteWord(bytes, 16, static_cast<std::uint32_t>(bytes.size()));
	bytes.insert(bytes.end(), {13, 2, 13, 0, static_cast<std::uint8_t>(id.size() + 1)});
	WriteWord(bytes, 20, static_cast<std::uint32_t>(bytes.size()));
	bytes.insert(bytes.end(), id.begin(), id.end());
	bytes.push_back(0);
	bytes.insert(bytes.end(), spec.begin(), spec.end());
	bytes.push_back(0);
	WriteWord(bytes, 24, static_cast<std::uint32_t>(bytes.size()));
	bytes.insert(bytes.end(), {13, 1, 13, 0});
	WriteWord(bytes, 28, static_cast<std::uint32_t>(bytes.size()));
	bytes.insert(bytes.end(), {13, 1, 13, 4});
	WriteWord(bytes, 32, static_cast<std::uint32_t>(bytes.size()));
	RefreshChecksum(bytes);
	bytes.insert(bytes.end(), {10, 20, 30, 40});

	return bytes;
}

static void TestAudioIdentifierAdmission() {
	for (const auto id : {"sound", "sound_archive"}) {
		const auto bytes = AudioReceivingPsb(id, "win");
		auto document = sgre::resource::ReadPsbMetadata(bytes, bytes.size());
		Require(document.Names == std::vector<std::string>{"id", "spec"} && document.SerializedSize == bytes.size() && document.Chunks.size() == 1 && document.Chunks[0].Size == 4 && document.Chunks[0].Offset == bytes.size() - 4, "independent audio receiving fixture differs from its named root and exact retained chunk extent");
		Require(std::get<std::string>(document.Root.Find("id")->Data) == id && std::get<std::string>(document.Root.Find("spec")->Data) == "win" && !sgre::io::RequiresDerivedPsbProduction(document), "both exact audio ids require the proved empty-texture receiving lane");
		for (const auto unsupported_spec : {"linux", "Win", "", "win "}) {
			const auto unsupported_bytes = AudioReceivingPsb(id, unsupported_spec);
			document = sgre::resource::ReadPsbMetadata(unsupported_bytes, unsupported_bytes.size());
			Require(sgre::io::RequiresDerivedPsbProduction(document), "non-exact target specs must preserve conservative production rejection");
		}

		document = sgre::resource::ReadPsbMetadata(bytes, bytes.size());
		auto& fields = std::get<sgre::resource::PsbObject>(document.Root.Data);
		fields[1].second.Data = std::monostate{};
		Require(sgre::io::RequiresDerivedPsbProduction(document), "present null audio spec must reject");
		fields[1].second.Data = std::int64_t{1};
		Require(sgre::io::RequiresDerivedPsbProduction(document), "non-string audio spec must reject before receiving publication");
		fields[1].second.Data = std::string("win\0suffix", 10);
		Require(sgre::io::RequiresDerivedPsbProduction(document), "source admission must not manufacture native C-string truncation parity");
		fields[1].second.Data = std::string("win");
		fields[0].second.Data = std::monostate{};
		Require(sgre::io::RequiresDerivedPsbProduction(document), "present null audio id must reject");
		fields[0].second.Data = std::int64_t{1};
		Require(sgre::io::RequiresDerivedPsbProduction(document), "non-string audio id must reject");
		fields[0].second.Data = std::string(id) + std::string("\0suffix", 7);
		Require(sgre::io::RequiresDerivedPsbProduction(document), "non-exact audio id must reject");
	}

	for (const auto id : {"rsc", "motion", "font", "map", "image", "Sound", "sound_archive_extra", "unknown", ""}) {
		const auto bytes = AudioReceivingPsb(id, "win");
		const auto document = sgre::resource::ReadPsbMetadata(bytes, bytes.size());
		Require(sgre::io::RequiresDerivedPsbProduction(document), "new audio receiving admission must preserve every other populated target rejection");
	}
}

static void TestAudioReceivingOwner(const std::filesystem::path& directory) {
	for (const auto id : {"sound", "sound_archive"}) {
		for (const bool wrapped : {false, true}) {
			const auto decoded = AudioReceivingPsb(id, "win");
			std::shared_ptr<const sgre::io::PublishedPsbFile> retained;
			{
				ResourceCache cache(0);
				ArchiveMounts mounts;
				BackgroundCallManager background;
				FileReadProcessor processor(cache, mounts, background);
				const std::string name = "audio/item";
				const auto handle = cache.Acquire(name);
				sgre::io::PreparedResourceFileRead source;
				source.Format.Kind = sgre::io::ResourceFileKind::Psb;
				source.Format.Wrapped = wrapped;
				source.OriginalFilename = wrapped ? name + ".m" : name;
				source.Seed = "synthetic-seed";
				source.Region.Filename = directory / "audio-cell.bin";
				source.Region.MaxBytes = 4096;
				source.MaxDecodedBytes = 4096;
				source.MaxMetadataBytes = 4096;
				WriteFile(source.Region.Filename, wrapped ? Wrap(decoded, source.OriginalFilename) : decoded);
				static_cast<void>(processor.QueuePreparedResource(handle, source.OriginalFilename, source));
				Require(processor.PendingRawCount() == 1 && cache.Info(handle).Completion == sgre::resource::ResourceCompletion::Pending, "audio receiving must use the actual typed pending cell");
				processor.Update();
				DrainLane(background);
				const std::array targets{handle};
				Require(!cache.ObserveLoadingTargets(targets) && cache.Info(handle).Completion == sgre::resource::ResourceCompletion::Succeeded && !cache.Info(handle).UnsupportedProduction && cache.Info(handle).ChargedBytes == decoded.size(), "real audio receiving completion must charge full decoded storage with the proved empty texture lane");
				retained = cache.PinPsbFile(handle);
				Require(cache.Info(handle).References == 2 && retained.get() == &cache.PsbFile(handle) && retained->BackingStorage() == decoded && std::get<std::string>(retained->Document().Root.Find("id")->Data) == id, "audio pin must retain the same genuine published document and backing");
				cache.Release(handle);
				Require(cache.Contains(handle) && cache.Info(handle).References == 1, "actual audio find pin must enclose target release");
				processor.Update();
				processor.Close();
				background.Close();
				std::filesystem::remove(source.Region.Filename);
			}

			Require(retained->BackingStorage() == decoded && retained->Document().Chunks[0].Offset == decoded.size() - 4, "real audio resource pin must outlive the reader and cache facade with its unchanged encoded chunk bytes");
			const std::weak_ptr<const sgre::io::PublishedPsbFile> weak = retained;
			retained.reset();
			Require(weak.expired(), "last actual audio pin must retire its existing cache owner");
		}
	}
}

int main(int argc, char** argv) {
	try {
		if (argc != 2) {
			throw std::runtime_error("resource-file service fixtures require one ignored directory");
		}

		const std::filesystem::path directory(argv[1]);
		std::filesystem::create_directories(directory);
		TestFourActualModes(directory);
		TestCoalescingAndModePreflight(directory);
		TestQueuedClearAndFailureOwnership(directory);
		TestAdmissionAndOwnerPreflight(directory);
		TestUnsupportedAndRollback(directory);
		TestDerivedGateAndCellProtocol(directory);
		TestAudioIdentifierAdmission();
		TestAudioReceivingOwner(directory);
		std::filesystem::remove(directory / "index.bin");
		std::filesystem::remove(directory / "body.bin");
		std::cout << "eight actual resource-file receiving/mounted/target/pin groups pass\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
