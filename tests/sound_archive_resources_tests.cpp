#include "sgre/audio/sound_configuration_loads.hpp"

#include "sgre/core/background_calls.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using Bytes = std::vector<std::uint8_t>;
using sgre::audio::SoundArchiveLoadDiagnostic;
using sgre::audio::SoundCacheLoadCompletion;
using sgre::audio::SoundConfigurationDiagnostic;
using sgre::audio::SoundConfigurationLoads;

static void Require(bool condition, const char* message) {
	if (!condition) {
		std::cerr << "sound archive fixture failed: " << message << '\n';
		throw std::runtime_error(message);
	}
}

template <typename Operation>
static void RequireFailure(Operation operation) {
	try {
		operation();
	} catch (const std::runtime_error&) {
		return;
	}

	throw std::runtime_error("sound archive fixture expected a supported-domain rejection");
}

static void WriteWord(Bytes& bytes, std::size_t offset, std::uint32_t value) {
	for (std::uint32_t index = 0; index < 4; ++index) {
		bytes.at(offset + index) = static_cast<std::uint8_t>(value >> (index * 8));
	}
}

static Bytes PackedArray(const std::vector<std::uint32_t>& values) {
	Require(values.size() <= 1024 && std::all_of(values.begin(), values.end(), [](auto value) {
		return value <= 65535;
	}),
	    "independent PSB fixture packed-array bound exceeded");
	const bool wide_count = values.size() > 255;
	const bool wide_value = std::any_of(values.begin(), values.end(), [](auto value) {
		return value > 255;
	});
	Bytes bytes{static_cast<std::uint8_t>(wide_count ? 14 : 13), static_cast<std::uint8_t>(values.size())};
	if (wide_count) {
		bytes.push_back(static_cast<std::uint8_t>(values.size() >> 8));
	}

	bytes.push_back(static_cast<std::uint8_t>(wide_value ? 14 : 13));
	for (const auto value : values) {
		bytes.push_back(static_cast<std::uint8_t>(value));
		if (wide_value) {
			bytes.push_back(static_cast<std::uint8_t>(value >> 8));
		}
	}

	return bytes;
}

static void Append(Bytes& destination, const Bytes& bytes) {
	destination.insert(destination.end(), bytes.begin(), bytes.end());
}

struct FixtureNameNode {
	std::map<std::uint8_t, std::size_t> Children;
	std::uint32_t Position = 0;
};

static const std::array<std::string_view, 14> kNames = {"id", "spec", "version", "archive", "common", "voice", "cue", "device", "volume", "channelList", "archData", "pan", "data", "missing"};

static std::uint32_t NameIndex(std::string_view name) {
	const auto found = std::find(kNames.begin(), kNames.end(), name);
	Require(found != kNames.end(), "fixture key is outside its declared independent name list");

	return static_cast<std::uint32_t>(found - kNames.begin());
}

static Bytes NameTables() {
	// Build the declared byte paths independently, then place their double-array edges into unused positions.
	std::vector<FixtureNameNode> nodes(1);
	std::vector<std::size_t> leaves;
	for (const auto name : kNames) {
		std::size_t parent = 0;
		for (std::size_t index = 0; index <= name.size(); ++index) {
			const auto character = index == name.size() ? std::uint8_t{0} : static_cast<std::uint8_t>(name[index]);
			const auto found = nodes[parent].Children.find(character);
			if (found != nodes[parent].Children.end()) {
				parent = found->second;
				continue;
			}

			const auto next = nodes.size();
			nodes[parent].Children.emplace(character, next);
			nodes.emplace_back();
			parent = next;
		}

		leaves.push_back(parent);
	}

	std::array<bool, 1024> used{};
	used[0] = true;
	std::vector<std::uint32_t> bases(1024, 0);
	std::vector<std::uint32_t> parents(1024, 0);
	std::uint32_t highest = 0;
	for (std::size_t parent = 0; parent < nodes.size(); ++parent) {
		if (nodes[parent].Children.empty()) {
			continue;
		}

		std::uint32_t base = 0;
		while (!std::all_of(nodes[parent].Children.begin(), nodes[parent].Children.end(), [&](const auto& child) {
			return base + child.first < used.size() && !used[base + child.first];
		})) {
			Require(++base < used.size(), "independent name trie placement exceeds its fixed 1024-node bound");
		}

		bases[nodes[parent].Position] = base;
		for (const auto& [character, child] : nodes[parent].Children) {
			const auto position = base + character;
			used[position] = true;
			parents[position] = nodes[parent].Position;
			nodes[child].Position = position;
			highest = std::max(highest, position);
		}
	}

	bases.resize(highest + 1);
	parents.resize(highest + 1);
	std::vector<std::uint32_t> terminal_positions;
	for (const auto leaf : leaves) {
		terminal_positions.push_back(nodes[leaf].Position);
	}

	Bytes bytes;
	Append(bytes, PackedArray(bases));
	Append(bytes, PackedArray(parents));
	Append(bytes, PackedArray(terminal_positions));

	return bytes;
}

static Bytes Dictionary(std::vector<std::pair<std::string_view, Bytes>> fields) {
	std::vector<std::uint32_t> keys;
	std::vector<std::uint32_t> offsets;
	Bytes atoms;
	for (const auto& [name, atom] : fields) {
		keys.push_back(NameIndex(name));
		offsets.push_back(static_cast<std::uint32_t>(atoms.size()));
		Append(atoms, atom);
	}

	Bytes bytes{33};
	Append(bytes, PackedArray(keys));
	Append(bytes, PackedArray(offsets));
	Append(bytes, atoms);

	return bytes;
}

static Bytes Array(std::vector<Bytes> values) {
	std::vector<std::uint32_t> offsets;
	Bytes atoms;
	for (const auto& value : values) {
		offsets.push_back(static_cast<std::uint32_t>(atoms.size()));
		Append(atoms, value);
	}

	Bytes bytes{32};
	Append(bytes, PackedArray(offsets));
	Append(bytes, atoms);

	return bytes;
}

static Bytes Number(float value) {
	Bytes bytes(5, 0);
	bytes[0] = 30;
	WriteWord(bytes, 1, std::bit_cast<std::uint32_t>(value));

	return bytes;
}

static Bytes CatalogPsb(bool configuration, bool missing_voice = false, float version = 1.02f) {
	const auto channel = Dictionary({{"archData", Dictionary({{"data", {25, 0}}})}, {"pan", Array({Number(0.25f), Number(0.75f), {21, 2}})}});
	const auto voice = Dictionary({{"device", {5, 1}}, {"volume", Number(0.5f)}, {"channelList", Array({channel})}});
	std::vector<std::pair<std::string_view, Bytes>> fields{{"id", {21, 0}}, {"spec", {21, 1}}, {"version", Number(version)}};
	if (configuration) {
		fields.emplace_back("archive", Dictionary({{"common", {2}}}));
	} else if (!missing_voice) {
		fields.emplace_back("voice", Dictionary({{"cue", voice}}));
	}

	Bytes bytes(44, 0);
	bytes[0] = 'P';
	bytes[1] = 'S';
	bytes[2] = 'B';
	bytes[4] = 3;
	WriteWord(bytes, 8, 44);
	WriteWord(bytes, 12, 44);
	Append(bytes, NameTables());
	WriteWord(bytes, 36, static_cast<std::uint32_t>(bytes.size()));
	Append(bytes, Dictionary(std::move(fields)));
	const std::string_view id = configuration ? "sound" : "sound_archive";
	WriteWord(bytes, 16, static_cast<std::uint32_t>(bytes.size()));
	Append(bytes, PackedArray({0, static_cast<std::uint32_t>(id.size() + 1), static_cast<std::uint32_t>(id.size() + 5)}));
	WriteWord(bytes, 20, static_cast<std::uint32_t>(bytes.size()));
	for (const auto text : {id, std::string_view("win"), std::string_view("ignored-extra-pan")}) {
		bytes.insert(bytes.end(), text.begin(), text.end());
		bytes.push_back(0);
	}

	WriteWord(bytes, 24, static_cast<std::uint32_t>(bytes.size()));
	Append(bytes, PackedArray({0}));
	WriteWord(bytes, 28, static_cast<std::uint32_t>(bytes.size()));
	Append(bytes, PackedArray({4}));
	WriteWord(bytes, 32, static_cast<std::uint32_t>(bytes.size()));
	std::uint32_t first = 1;
	std::uint32_t second = 0;
	for (std::size_t index = 8; index < 40; ++index) {
		first += bytes[index];
		second += first;
	}
	WriteWord(bytes, 40, (second << 16) | first);
	bytes.insert(bytes.end(), {10, 20, 30, 40});
	Require(bytes.size() < 4096, "independent catalog PSB exceeds its explicitly bounded fixture input");
	const auto document = sgre::resource::ReadPsbMetadata(bytes, bytes.size());
	Require(document.Names == std::vector<std::string>(kNames.begin(), kNames.end()) && std::get<std::string>(document.Root.Find("id")->Data) == id && std::get<double>(document.Root.Find("version")->Data) == static_cast<double>(version) && document.Chunks.size() == 1 && document.Chunks[0].Offset == bytes.size() - 4, "independent catalog PSB differs from its declared keys/id/float/chunk graph");

	return bytes;
}

static sgre::io::PreparedResourceFileRead Prepared(const std::filesystem::path& path, const Bytes& bytes, std::string read_filename) {
	std::ofstream file(path, std::ios::binary | std::ios::trunc);
	for (const auto byte : bytes) {
		file.put(static_cast<char>(byte));
	}
	file.close();
	Require(static_cast<bool>(file), "sound archive fixture could not write its bounded physical source");
	sgre::io::PreparedResourceFileRead source;
	source.Region.Filename = path;
	source.Region.Length = static_cast<std::uint32_t>(bytes.size());
	source.Region.MaxBytes = 4096;
	source.Format.Kind = sgre::io::ResourceFileKind::Psb;
	source.OriginalFilename = std::move(read_filename);
	source.MaxDecodedBytes = 4096;
	source.MaxMetadataBytes = 4096;

	return source;
}

static void FinishReader(sgre::io::FileReadProcessor& processor, sgre::core::BackgroundCallManager& background) {
	// A previous batch's serial owner retires before the next update's serial-empty gate starts new work.
	processor.Update();
	processor.Update();
	std::promise<void> barrier;
	auto done = barrier.get_future();
	sgre::core::BackgroundCallTask after(background, sgre::core::BackgroundCallLane::BelowNormal, [&]() {
		barrier.set_value();
	});
	after.Update();
	background.PostLoop();
	done.get();
	after.Close();
}

static void TestBorrowedDefinitions() {
	const auto bytes = CatalogPsb(false);
	auto document = sgre::resource::ReadPsbMetadata(bytes, bytes.size());
	const auto definitions = sgre::resource::ReadSoundArchiveDefinitions(document);
	const auto& definition = definitions.at("cue");
	const auto& channel = std::get<sgre::resource::PsbArray>(document.Root.Find("voice")->Find("cue")->Find("channelList")->Data).front();
	Require(definition.Device == 1 && definition.Volume == 0.5f && definition.PitchOctaves == 0.0f && definition.Priority == 0.0f && definition.Group.empty() && definition.Uniqueness.empty() && definition.Channels.size() == 1 && definition.Channels.front().Pan == std::array<float, 8>{0.25f, 0.75f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}, "native archive definition field defaults and first-two-only pan differ");
	Require(&definition.Channels.front().ArchitectureData.get() == channel.Find("archData") && std::get<sgre::resource::PsbChunkReference>(definition.Channels.front().ArchitectureData.get().Find("data")->Data).Index == 0, "archive definitions must borrow the original PSB archData atom, without eager format/data decoding");
	Require(sgre::resource::BuildSoundArchiveFilename("", "common") == "/common.psb" && sgre::resource::BuildSoundArchiveFilename("a\\b", "common") == "a\\b/common.psb", "native archive path insertion must preserve exact prefix spelling and empty-prefix slash");
	const auto config_bytes = CatalogPsb(true);
	const auto config = sgre::resource::ReadPsbMetadata(config_bytes, config_bytes.size());
	Require(sgre::resource::ContainsSoundConfigurationArchive(config, "common") && !sgre::resource::ContainsSoundConfigurationArchive(config, "Common"), "configuration archive availability must use the actual exact object key");
	sgre::resource::SoundDefinitionLimits limits;
	limits.MaxOwnedFieldBytes = 1;
	RequireFailure([&]() {
		static_cast<void>(sgre::resource::ReadSoundArchiveDefinitions(document, limits));
	});
	RequireFailure([&]() {
		static_cast<void>(sgre::resource::BuildSoundArchiveFilename("long-prefix", "common", 5));
	});
}

static void TestActualArchiveDelivery(const std::filesystem::path& directory) {
	const auto config_source = Prepared(directory / "config.bin", CatalogPsb(true), "configuration.psb");
	const auto archive_bytes = CatalogPsb(false, false, 1.0f);
	const auto archive_source = Prepared(directory / "archive.bin", archive_bytes, "/common.psb");
	sgre::resource::ResourceCache cache(0);
	sgre::resource::ArchiveMounts mounts;
	sgre::core::BackgroundCallManager background;
	sgre::io::FileReadProcessor processor(cache, mounts, background);
	SoundConfigurationLoads loads(cache, mounts, processor, "win");
	Require(!loads.LoadArchivePrepared("common", archive_source) && loads.LoadPrepared("configuration.psb", config_source), "archive creation requires its actual loaded configuration declaration");
	FinishReader(processor, background);
	loads.AdvanceSerialLoads();
	Require(!loads.GetArchiveLoaded("common") && !loads.GetArchiveLoading("common"), "an available declaration is distinct from an actual archive load owner");
	Require(loads.LoadArchivePrepared("common", archive_source) && !loads.LoadArchivePrepared("common", {}), "archive idempotence must suppress another actual load and cache reference");
	const auto handle = *cache.FindNamedResource("/common.psb");
	Require(loads.GetArchiveLoading("common") && !loads.GetArchiveLoaded("common") && cache.Info(handle).References == 1 && loads.SerialLoads().Count() == 2 && loads.ArchiveInfo("common").Filename == "/common.psb", "archive receiving state must own the native path, one manager reference and actual load/parse pair");
	FinishReader(processor, background);
	loads.AdvanceSerialLoads();
	const auto info = loads.ArchiveInfo("common");
	Require(loads.GetArchiveLoaded("common") && !loads.GetArchiveLoading("common") && info.CacheCompletion == SoundCacheLoadCompletion::Delivered && info.Diagnostic == SoundArchiveLoadDiagnostic::DefinitionsReady && info.IdentityDiagnostic == SoundConfigurationDiagnostic::OldVersion && cache.Info(handle).References == 1, "archive version diagnostics must preserve native completion and continue actual voice-definition production");
	const auto& file = loads.ArchiveFile("common");
	const auto& definitions = loads.ArchiveDefinitions("common");
	const auto& channel = std::get<sgre::resource::PsbArray>(file.Document().Root.Find("voice")->Find("cue")->Find("channelList")->Data).front();
	Require(&file == &cache.PsbFile(handle) && file.BackingStorage() == archive_bytes && &definitions.at("cue").Channels.front().ArchitectureData.get() == channel.Find("archData"), "actual archive PSB backing and borrowed architecture view must share the original cache owner without another pin");
	Require(loads.UnloadArchive("common") && loads.ArchiveOwnerCount() == 0 && !cache.Contains(handle) && loads.GetLoaded("configuration.psb"), "completed archive without cue users must release its reference immediately and retain its containing configuration");
	loads.Close();
	processor.Close();
	background.Close();
}

static void TestOrderedConfigurationAndCancellation(const std::filesystem::path& directory) {
	const auto first = Prepared(directory / "config-a.bin", CatalogPsb(true), "a/configuration.psb");
	const auto second = Prepared(directory / "config-z.bin", CatalogPsb(true), "z/configuration.psb");
	const auto archive = Prepared(directory / "archive-a.bin", CatalogPsb(false), "a/common.psb");
	sgre::resource::ResourceCache cache(0);
	sgre::resource::ArchiveMounts mounts;
	sgre::core::BackgroundCallManager background;
	sgre::io::FileReadProcessor processor(cache, mounts, background);
	sgre::audio::SoundConfigurationLoadLimits limits;
	limits.MaxArchives = 1;
	SoundConfigurationLoads loads(cache, mounts, processor, "win", limits);
	Require(loads.LoadPrepared("z/configuration.psb", second) && loads.LoadPrepared("a/configuration.psb", first), "fixture did not admit both actual configurations in reverse insertion order");
	FinishReader(processor, background);
	loads.AdvanceSerialLoads();
	Require(loads.LoadArchivePrepared("common", archive) && loads.ArchiveInfo("common").ConfigurationFilename == "a/configuration.psb", "native archive selection must use exact ordered config traversal rather than insertion order");
	const auto archive_handle = *cache.FindNamedResource("a/common.psb");
	const auto config_handle = *cache.FindNamedResource("a/configuration.psb");
	Require(loads.UnloadArchive("common") && loads.ArchiveOwnerCount() == 1 && cache.Info(archive_handle).References == 0 && loads.SerialLoads().Count() == 2 && processor.PendingRawCount() == 1, "pending archive unload must cancel only its original cache task while retaining callback and receiving owners");
	RequireFailure([&]() {
		static_cast<void>(loads.LoadArchivePrepared("common", archive));
	});
	loads.AdvanceSerialLoads();
	Require(loads.ArchiveOwnerCount() == 0 && loads.LoadArchivePrepared("common", archive) && cache.Info(archive_handle).References == 1 && processor.PendingRawCount() == 1, "serial cancel completion must free only the archive owner; a real reload reuses the still-queued receiving entry");
	Require(loads.Unload("a/configuration.psb") && loads.RetiringCount() == 1 && loads.ArchiveOwnerCount() == 1 && cache.Info(config_handle).References == 1 && cache.Info(archive_handle).References == 0, "containing config unload must retain config backing and cancelled archive captures until archive completion");
	loads.AdvanceSerialLoads();
	Require(loads.RetiringCount() == 0 && loads.ArchiveOwnerCount() == 0 && !cache.Contains(config_handle) && !loads.GetArchiveLoaded("common") && !loads.GetArchiveLoading("common"), "archive completion must precede config retirement; subsequent queries select the next actual config declaration");
	loads.Close();
	processor.Close();
	background.Close();
	cache.Advance();
	Require(!cache.Contains(archive_handle), "reader close must retire its actual still-queued destination after the resource owner has cancelled its task");
}

static void TestFailedAndMalformedArchive(const std::filesystem::path& directory) {
	for (const bool failed_delivery : {false, true}) {
		const auto config = Prepared(directory / "failure-config.bin", CatalogPsb(true), "configuration.psb");
		const auto archive = Prepared(directory / "failure-archive.bin", failed_delivery ? Bytes{1, 2, 3} : CatalogPsb(false, true), "/common.psb");
		sgre::resource::ResourceCache cache(0);
		sgre::resource::ArchiveMounts mounts;
		sgre::core::BackgroundCallManager background;
		sgre::io::FileReadProcessor processor(cache, mounts, background);
		SoundConfigurationLoads loads(cache, mounts, processor, "win");
		Require(loads.LoadPrepared("configuration.psb", config), "fixture did not admit its actual configuration");
		FinishReader(processor, background);
		loads.AdvanceSerialLoads();
		Require(loads.LoadArchivePrepared("common", archive), "fixture did not admit its actual archive");
		FinishReader(processor, background);
		loads.AdvanceSerialLoads();
		const auto info = loads.ArchiveInfo("common");
		Require(loads.GetArchiveLoaded("common") && !loads.GetArchiveLoading("common") && info.CacheCompletion == SoundCacheLoadCompletion::Delivered && !info.Error.empty() && info.Diagnostic == (failed_delivery ? SoundArchiveLoadDiagnostic::FailedDelivery : SoundArchiveLoadDiagnostic::InvalidDefinitions), "native archive completion byte and callback status must remain separate from actual delivery/definition success");
		RequireFailure([&]() {
			static_cast<void>(loads.ArchiveDefinitions("common"));
		});
		Require(loads.UnloadArchive("common") && loads.ArchiveOwnerCount() == 0, "failed delivery and malformed definition owners must still retire without source/cue production");
		loads.Close();
		processor.Close();
		background.Close();
	}
}

int main(int argc, char** argv) {
	try {
		if (argc != 2) {
			throw std::runtime_error("sound archive fixture requires an ignored output directory");
		}

		const std::filesystem::path directory(argv[1]);
		std::filesystem::create_directories(directory);
		TestBorrowedDefinitions();
		TestActualArchiveDelivery(directory);
		TestOrderedConfigurationAndCancellation(directory);
		TestFailedAndMalformedArchive(directory);
		std::cout << "sound archive resource fixtures passed (four groups)\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
