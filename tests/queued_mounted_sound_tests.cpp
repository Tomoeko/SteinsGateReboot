#include "sgre/resource/resource_cipher.hpp"
#include "sgre/script/sound_bindings.hpp"

#include "sgre/core/background_calls.hpp"
#include "sgre/script/resource_file_service.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using Bytes = std::vector<std::uint8_t>;
using sgre::audio::BufferedSoundCuePreparationStatus;

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
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

	throw std::runtime_error("buffered cue fixture expected rejection: " + std::string(expected));
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

static const std::array<std::string_view, 29> kNames = {"id", "spec", "version", "archive", "common", "secondary", "voice", "cue", "other", "low", "high", "request", "device", "volume", "priority", "pitch", "uniq", "channelList", "archData", "pan", "fmt", "data", "nosrc", "missing", "file_info", "expire_suffix_list", "configuration.psb", "common.psb", "configuration"};

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

struct CueVector {
	std::string_view Name = "cue";
	std::uint8_t Channels = 1;
	std::uint8_t Device = 1;
	float Priority = 0;
	bool Unique = false;
	std::uint8_t InvalidChannelMask = 0;
	bool DisableRateConversion = true;
};

static Bytes CatalogPsb(std::span<const CueVector> vectors = {}) {
	const bool configuration = vectors.empty();
	std::vector<std::pair<std::string_view, Bytes>> voices;
	std::vector<Bytes> chunks;
	for (const auto& vector : vectors) {
		std::vector<Bytes> channels;
		for (std::uint8_t channel = 0; channel < vector.Channels; ++channel) {
			const bool invalid = channel < 8 && (vector.InvalidChannelMask & (std::uint8_t{1} << channel)) != 0;
			const auto format_index = static_cast<std::uint8_t>(chunks.size());
			// Independent WAVEFORMATEX: mono signed16, 48000 Hz, complete two-byte frames.
			chunks.push_back(Bytes{static_cast<std::uint8_t>(invalid ? 0x63 : 1), static_cast<std::uint8_t>(invalid ? 1 : 0), 1, 0, 0x80, 0xbb, 0, 0, 0, 0x77, 1, 0, 2, 0, 16, 0, 0, 0});
			const auto data_index = static_cast<std::uint8_t>(chunks.size());
			chunks.push_back(Bytes{0, 0x20, 0, 0x40, 0, 0xc0, 0, 0});
			const auto architecture = Dictionary({{"fmt", {25, format_index}}, {"data", {25, data_index}}, {"nosrc", vector.DisableRateConversion ? Bytes{6, 0, 1} : Bytes{4}}});
			channels.push_back(Dictionary({{"archData", architecture}, {"pan", Array({Number(0.25f), Number(0.75f)})}}));
		}
		std::vector<std::pair<std::string_view, Bytes>> fields{{"device", {5, vector.Device}}, {"volume", Number(0.5f)}, {"priority", Number(vector.Priority)}, {"channelList", Array(std::move(channels))}};
		if (vector.Unique) {
			fields.emplace_back("uniq", Bytes{21, 2});
		}
		voices.emplace_back(vector.Name, Dictionary(std::move(fields)));
	}
	std::vector<std::pair<std::string_view, Bytes>> fields{{"id", {21, 0}}, {"spec", {21, 1}}, {"version", Number(1.02f)}};
	if (configuration) {
		fields.emplace_back("archive", Dictionary({{"common", {2}}, {"secondary", {2}}}));
	} else {
		fields.emplace_back("voice", Dictionary(std::move(voices)));
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
	for (const auto text : {id, std::string_view("win"), std::string_view("shared")}) {
		bytes.insert(bytes.end(), text.begin(), text.end());
		bytes.push_back(0);
	}
	std::vector<std::uint32_t> offsets;
	std::vector<std::uint32_t> sizes;
	Bytes payload;
	for (const auto& chunk : chunks) {
		offsets.push_back(static_cast<std::uint32_t>(payload.size()));
		sizes.push_back(static_cast<std::uint32_t>(chunk.size()));
		Append(payload, chunk);
	}
	WriteWord(bytes, 24, static_cast<std::uint32_t>(bytes.size()));
	Append(bytes, PackedArray(offsets));
	WriteWord(bytes, 28, static_cast<std::uint32_t>(bytes.size()));
	Append(bytes, PackedArray(sizes));
	WriteWord(bytes, 32, static_cast<std::uint32_t>(bytes.size()));
	std::uint32_t first = 1;
	std::uint32_t second = 0;
	for (std::size_t index = 8; index < 40; ++index) {
		first += bytes[index];
		second += first;
	}
	WriteWord(bytes, 40, (second << 16) | first);
	Append(bytes, payload);
	Require(bytes.size() < 4096, "independent actual cue PSB fixture exceeds its bounded backing");
	const auto document = sgre::resource::ReadPsbMetadata(bytes, bytes.size());
	Require(document.Names == std::vector<std::string>(kNames.begin(), kNames.end()) && document.Chunks.size() == chunks.size(), "independent cue PSB names/chunk graph differ from the declared format vectors");

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

static Bytes IntegerAtom(std::size_t value) {
	Require(value < 32768, "independent mounted fixture integer exceeds its positive signed16 domain");

	return Bytes{6, static_cast<std::uint8_t>(value), static_cast<std::uint8_t>(value >> 8)};
}

static Bytes IndexPsb(std::size_t configuration_size, std::size_t archive_size) {
	Bytes bytes(44, 0);
	bytes[0] = 'P';
	bytes[1] = 'S';
	bytes[2] = 'B';
	bytes[4] = 3;
	WriteWord(bytes, 8, 44);
	WriteWord(bytes, 12, 44);
	Append(bytes, NameTables());
	WriteWord(bytes, 36, static_cast<std::uint32_t>(bytes.size()));
	const auto entries = Dictionary({{"configuration.psb", Array({IntegerAtom(0), IntegerAtom(configuration_size)})}, {"common.psb", Array({IntegerAtom(configuration_size), IntegerAtom(archive_size)})}});
	Append(bytes, Dictionary({{"id", {21, 0}}, {"version", {5, 1}}, {"expire_suffix_list", Array({{21, 1}})}, {"file_info", entries}}));
	WriteWord(bytes, 16, static_cast<std::uint32_t>(bytes.size()));
	Append(bytes, PackedArray({0, 8}));
	WriteWord(bytes, 20, static_cast<std::uint32_t>(bytes.size()));
	bytes.insert(bytes.end(), {'a', 'r', 'c', 'h', 'i', 'v', 'e', 0, '.', 'm', 0});
	WriteWord(bytes, 24, static_cast<std::uint32_t>(bytes.size()));
	Append(bytes, PackedArray({}));
	WriteWord(bytes, 28, static_cast<std::uint32_t>(bytes.size()));
	Append(bytes, PackedArray({}));
	WriteWord(bytes, 32, static_cast<std::uint32_t>(bytes.size()));
	std::uint32_t first = 1;
	std::uint32_t second = 0;
	for (std::size_t index = 8; index < 40; ++index) {
		first += bytes[index];
		second += first;
	}
	WriteWord(bytes, 40, (second << 16) | first);
	const auto document = sgre::resource::ReadPsbMetadata(bytes, bytes.size());
	const auto index = sgre::resource::ReadArchiveIndex(document, configuration_size + archive_size);
	Require(index.Entries.size() == 2 && index.Entries[0].Name == "configuration.psb" && index.Entries[0].Size == configuration_size && index.Entries[1].Offset == configuration_size && index.Entries[1].Size == archive_size, "independent mount PSB actual offset/size graph differs");

	return bytes;
}

static Bytes Wrap(Bytes decoded, const std::string& filename) {
	Require(decoded.size() >= 256 && decoded.size() < 65536, "independent MZS raw-block vector exceeds its explicit size domain");
	const auto size = decoded.size() - 256;
	Bytes frame{0x28, 0xb5, 0x2f, 0xfd, 0x60, static_cast<std::uint8_t>(size), static_cast<std::uint8_t>(size >> 8)};
	const auto block = static_cast<std::uint32_t>((decoded.size() << 3) | 1);
	frame.insert(frame.end(), {static_cast<std::uint8_t>(block), static_cast<std::uint8_t>(block >> 8), static_cast<std::uint8_t>(block >> 16)});
	Append(frame, decoded);
	sgre::resource::ResourceCipher cipher(filename, "synthetic-seed", 131);
	cipher.Transform(frame);
	Bytes bytes{'m', 'z', 's', 0, 0, 0, 0, 0};
	WriteWord(bytes, 4, static_cast<std::uint32_t>(decoded.size()));
	Append(bytes, frame);

	return bytes;
}

class ActualQueuedMountSoundRuntime {
public:
	explicit ActualQueuedMountSoundRuntime(const std::filesystem::path& directory, bool queue_mount = true) : Cache(0), Processor(Cache, Mounts, Background), Sound(Cache, Mounts, Processor, "win", "synthetic-seed", true) {
		const std::array<CueVector, 1> vectors{CueVector{}};
		const auto configuration = Wrap(CatalogPsb(), "asset/configuration.psb.m");
		const auto archive = Wrap(CatalogPsb(vectors), "asset/common.psb.m");
		Bytes body = configuration;
		Append(body, archive);
		const auto body_path = directory / "queued-sound-body.bin";
		static_cast<void>(Prepared(body_path, body, "bounded independent body write"));
		const auto index_path = directory / "queued-sound-index.bin";
		const auto index = Prepared(index_path, Wrap(IndexPsb(configuration.size(), archive.size()), "queued-sound-index.psb.m"), "queued-sound-index.psb.m");
		sgre::resource::ArchiveReadMountInput input;
		input.Prefix = "asset/";
		input.IndexIdentity = "queued-sound-index.psb.m";
		input.BodyIdentity = "actual-queued-sound-body";
		input.BodyFilename = body_path;
		input.BodyBytes = body.size();
		input.IndexRead.Region = index.Region;
		input.IndexRead.OriginalFilename = "queued-sound-index.psb.m";
		input.IndexRead.Seed = "synthetic-seed";
		input.IndexRead.MaxDecodedBytes = 4096;
		input.IndexRead.MaxMetadataBytes = 4096;
		if (queue_mount) {
			Processor.QueueReadMount(std::move(input));
		}
	}

	~ActualQueuedMountSoundRuntime() noexcept {
		try {
			FinishWork();
			Sound.Close();
			Processor.Close();
			Background.Close();
		} catch (...) {
			std::terminate();
		}
	}

	sgre::resource::ResourceCache Cache;
	sgre::resource::ArchiveMounts Mounts;
	sgre::core::BackgroundCallManager Background;
	sgre::io::FileReadProcessor Processor;
	sgre::audio::SoundResourceControl Sound;

	void FinishWork() {
		for (std::size_t step = 0; step < 16; ++step) {
			Cache.Advance();
			FinishReader(Processor, Background);
			Sound.AdvancePostLoop();
			if (Processor.PendingRawCount() == 0 && !Mounts.HasInFlightReadMount() && Mounts.QueuedReadMountCount() == 0 && Sound.CanClose()) {
				return;
			}
		}

		throw std::runtime_error("actual queued mounted fixture still has pending native owner work");
	}
};

static void TestActualMountPriorityBeforeSoundSelection(const std::filesystem::path& directory) {
	ActualQueuedMountSoundRuntime runtime(directory);
	Require(runtime.Mounts.MountCount() == 0 && runtime.Mounts.QueuedReadMountCount() == 1, "fixture must keep its real index queued without manually publishing any mount");
	Require(runtime.Sound.Load("asset/configuration.psb"), "actual Sound load must admit its real logical request before index publication");
	const auto handle = *runtime.Cache.FindNamedResource("asset/configuration.psb");
	Require(runtime.Processor.PendingRawCount() == 1 && runtime.Cache.Info(handle).References == 1 && runtime.Sound.GetLoading("asset/configuration.psb") && !runtime.Sound.GetLoaded("asset/configuration.psb"), "logical Sound request must own the actual pending cache/callback/reader state before mapping exists");
	for (std::size_t step = 0; step < 6; ++step) {
		runtime.Processor.Update();
	}
	Require(runtime.Mounts.HasInFlightReadMount() && runtime.Mounts.MountCount() == 0, "actual processor must claim the pending mount ahead of ordinary Sound work");
	FinishReader(runtime.Processor, runtime.Background);
	Require(runtime.Mounts.MountCount() == 0 && runtime.Mounts.HasInFlightReadMount() && runtime.Processor.PendingRawCount() == 1 && runtime.Cache.Info(handle).Completion == sgre::resource::ResourceCompletion::Pending, "even the actual completed index worker must leave Sound unselected until containing mount publication/retirement");
	runtime.FinishWork();
	Require(runtime.Mounts.MountCount() == 1 && runtime.Mounts.RetainedPsbObjectCount() == 1 && runtime.Sound.GetLoaded("asset/configuration.psb") && runtime.Cache.Info(handle).References == 1 && runtime.Sound.ConfigurationFile("asset/configuration.psb").BackingStorage().size() == runtime.Sound.ConfigurationFile("asset/configuration.psb").Document().SerializedSize, "actual processor publication must precede selected body mapping and genuine full-backed PSB completion");
	Require(runtime.Sound.LoadArchive("common"), "archive request must derive its exact real mounted body name");
	runtime.FinishWork();
	Require(runtime.Sound.GetArchiveLoaded("common") && runtime.Sound.ArchiveFile("common").Document().Root.Find("voice"), "archive request must use the same selected-worker mapping lane and actual decoded document");
}

static void TestActualCoalescingAndDeferredUnload(const std::filesystem::path& directory) {
	ActualQueuedMountSoundRuntime runtime(directory);
	sgre::script::ScriptResourceFileService resource(runtime.Cache, runtime.Mounts, runtime.Processor, "synthetic-seed");
	Require(runtime.Sound.Load("asset/configuration.psb"), "Sound must own the initial actual queued logical source");
	const auto handle = *runtime.Cache.FindNamedResource("asset/configuration.psb");
	auto poll = resource.RequestService()("asset/configuration.psb", sgre::script::ScriptResourceRequestMode::PsbWrapped);
	Require(runtime.Cache.Info(handle).References == 2 && runtime.Processor.PendingRawCount() == 1 && !poll(), "Resource must coalesce the same genuine pending Sound cell without resolving an unavailable mount early");
	Require(runtime.Sound.Unload("asset/configuration.psb") && !runtime.Sound.GetLoading("asset/configuration.psb") && runtime.Cache.Contains(handle) && runtime.Processor.PendingRawCount() == 1, "Sound unload removes lookup while actual queued reader/configuration borrows remain retained");
	runtime.FinishWork();
	auto asset = poll();
	Require(asset && asset->Error.empty() && asset->Document && asset->Backing && runtime.Cache.Info(handle).References == 1, "deferred actual Sound retirement must return only its own reference after real worker completion");
	poll = {};
	asset.reset();
	Require(!runtime.Cache.Contains(handle) && resource.CanClose(), "last actual Resource lease must independently retire the coalesced generation after Sound callbacks finish");
	resource.Close();
}

static void TestActualQueuedCancellation(const std::filesystem::path& directory) {
	ActualQueuedMountSoundRuntime runtime(directory);
	const auto handle = runtime.Cache.Acquire("asset/configuration.psb");
	sgre::io::ResourceReadFormat format;
	format.Kind = sgre::io::ResourceFileKind::Psb;
	format.Wrapped = true;
	auto source = sgre::io::PrepareQueuedMountedResourceRead(runtime.Mounts, "asset/configuration.psb.m", format, "synthetic-seed");
	static_cast<void>(runtime.Processor.QueuePreparedResource(handle, source.OriginalFilename, source));
	Require(runtime.Processor.PendingRawCount() == 1 && runtime.Cache.Info(handle).Completion == sgre::resource::ResourceCompletion::Pending, "queued logical request must own an actual cancellable receiving borrow");
	runtime.Cache.Release(handle);
	runtime.Cache.ClearCache();
	Require(runtime.Processor.PendingRawCount() == 0 && runtime.Cache.Contains(handle), "actual queued cancellation ends the pointer borrow before the next manager observation retires its Loading generation");
	runtime.Cache.Advance();
	Require(runtime.Processor.PendingRawCount() == 0 && !runtime.Cache.Contains(handle), "zero-reference ClearCache must cancel the actual queued node and retire its receiving borrow before mount dispatch");
	runtime.FinishWork();
	Require(runtime.Mounts.MountCount() == 1, "cancelling ordinary source work must leave the actual independent mount/index producer intact");
}

static void TestMissingMappingAndForeignOwnerPreflight(const std::filesystem::path& directory) {
	ActualQueuedMountSoundRuntime runtime(directory, false);
	Require(runtime.Sound.Load("missing-resource"), "a real logical load admits actual receiving work rather than substituting a success or guessing a path");
	const auto failed = *runtime.Cache.FindNamedResource("missing-resource");
	runtime.FinishWork();
	Require(runtime.Cache.Info(failed).Completion == sgre::resource::ResourceCompletion::Failed && runtime.Cache.Info(failed).Error.find("loose-file/path fallback remains unrecovered") != std::string::npos && runtime.Sound.ConfigurationInfo("missing-resource").Diagnostic == sgre::audio::SoundConfigurationDiagnostic::FailedDelivery && runtime.Sound.GetLoading("missing-resource") && !runtime.Sound.GetLoaded("missing-resource"), "selected mapping failure must publish real ReadFailed diagnostics without inventing a completion latch/document");
	static_cast<void>(runtime.Sound.Unload("missing-resource"));
	runtime.Sound.AdvancePostLoop();
	Require(!runtime.Cache.Contains(failed), "actual failed source and final serial captures must retire without dangling worker borrows");
	sgre::resource::ArchiveMounts foreign;
	const auto handle = runtime.Cache.Acquire("foreign");
	sgre::io::ResourceReadFormat format;
	format.Kind = sgre::io::ResourceFileKind::Psb;
	auto source = sgre::io::PrepareQueuedMountedResourceRead(foreign, "foreign", format, "synthetic-seed");
	RequireFailure([&] {
		static_cast<void>(runtime.Processor.QueuePreparedResource(handle, "foreign", source));
	},
	    "this actual processor mount owner");
	Require(runtime.Processor.PendingRawCount() == 0 && !runtime.Cache.ReadFormat(handle), "foreign mount borrow rejection must precede receiving cell and request publication");
	runtime.Cache.DiscardUnstartedRead(handle);
}

int main(int argc, char** argv) {
	try {
		Require(argc == 2, "queued-mounted fixtures require an ignored output directory");
		const std::filesystem::path directory(argv[1]);
		std::filesystem::create_directories(directory);
		TestActualMountPriorityBeforeSoundSelection(directory);
		TestActualCoalescingAndDeferredUnload(directory);
		TestActualQueuedCancellation(directory);
		TestMissingMappingAndForeignOwnerPreflight(directory);
		std::cout << "queued-mounted Sound:4 actual gate/ownership/failure groups passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
