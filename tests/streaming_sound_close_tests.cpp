#include "sgre/audio/sound_resource_control.hpp"

#include "sgre/audio/offline_buffered_sound_pool.hpp"
#include "sgre/core/background_calls.hpp"
#include "sgre/resource/resource_cipher.hpp"
#include "sgre/resource/resource_manager_process.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
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

static const std::array<std::string_view, 30> kNames = {"id", "spec", "version", "archive", "common", "secondary", "voice", "cue", "other", "low", "high", "request", "device", "volume", "priority", "pitch", "uniq", "channelList", "archData", "pan", "fmt", "data", "nosrc", "missing", "dpds", "expire_suffix_list", "file_info", "configuration.psb", "common.psb", "empty"};

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

static std::vector<std::uint8_t> EncodedImpulse() {
	std::vector<std::uint8_t> bytes(1487);
	std::size_t position = 0;
	const auto append = [&](std::uint32_t value, std::uint8_t count) {
		for (auto bit = count; bit != 0; --bit) {
			bytes[position / 8] |= static_cast<std::uint8_t>(((value >> (bit - 1)) & 1) << (7 - position % 8));
			++position;
		}
	};
	append(0, 4);
	append(6, 4);
	append(0, 12);
	append(0, 3);
	append(0, 3);
	for (std::size_t frame = 0; frame < 6; ++frame) {
		append(0, 3);
		append(1, 1);
		append(63, 7);
		for (std::size_t band = 0; band < 25; ++band) {
			append(0, 1);
		}
		if (frame == 2) {
			append(0x3e6, 11);
			append(128, 9);
			append(0, 11);
			append(1, 1);
		}
		append(0xf6, 9);
	}
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
	bool Wma = true;
	bool CorruptPacket = false;
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
			// Independent bounded fmt/data/dpds chunks; all references belong to this one real PSB document.
			if (vector.Wma && !invalid) {
				chunks.push_back(Bytes{0x61, 1, 1, 0, 0x44, 0xac, 0, 0, 0xa0, 0x0f, 0, 0, 0xcf, 5, 16, 0, 0, 0});
			} else {
				chunks.push_back(Bytes{static_cast<std::uint8_t>(invalid ? 0x63 : 1), static_cast<std::uint8_t>(invalid ? 1 : 0), 1, 0, 0x80, 0xbb, 0, 0, 0, 0x77, 1, 0, 2, 0, 16, 0, 0, 0});
			}
			const auto data_index = static_cast<std::uint8_t>(chunks.size());
			if (vector.Wma && !invalid) {
				auto packet = EncodedImpulse();
				if (vector.CorruptPacket && channel != 0) {
					packet[2] |= 0x0f;
				}
				chunks.push_back(std::move(packet));
			} else {
				chunks.push_back(Bytes{0, 0x20, 0, 0x40, 0, 0xc0, 0, 0});
			}
			std::vector<std::pair<std::string_view, Bytes>> source_fields{{"fmt", {25, format_index}}, {"data", {25, data_index}}, {"nosrc", vector.DisableRateConversion ? Bytes{6, 0, 1} : Bytes{4}}};
			if (vector.Wma && !invalid) {
				const auto dpds_index = static_cast<std::uint8_t>(chunks.size());
				chunks.push_back(Bytes{0, 0x40, 0, 0});
				source_fields.emplace_back("dpds", Bytes{25, dpds_index});
			}
			const auto architecture = Dictionary(std::move(source_fields));
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
	Require(bytes.size() < 16384, "independent actual cue PSB fixture exceeds its bounded backing");
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
	source.Region.MaxBytes = 16384;
	source.Format.Kind = sgre::io::ResourceFileKind::Psb;
	source.OriginalFilename = std::move(read_filename);
	source.MaxDecodedBytes = 16384;
	source.MaxMetadataBytes = 16384;

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

class ActualClosingSound {
public:
	explicit ActualClosingSound(const std::filesystem::path& directory, bool load = true)
	    : Manager(Mounts, Background), Cache(Manager.Cache()), Processor(Manager.Processor()), Sound(Cache, Mounts, Processor, "win", "synthetic-seed", false, Limits()) {
		const std::array<CueVector, 1> vectors{CueVector{}};
		const auto configuration = CatalogPsb();
		const auto archive = CatalogPsb(vectors);
		Bytes body = configuration;
		Append(body, archive);
		const auto body_path = directory / "closing-sound-body.bin";
		static_cast<void>(Prepared(body_path, body, "unused-write-mapping"));
		const auto index = Prepared(directory / "closing-sound-index.bin", Wrap(IndexPsb(configuration.size(), archive.size()), "closing-index.psb.m"), "closing-index.psb.m");
		sgre::resource::ArchiveReadMountInput input;
		input.Prefix = "asset/";
		input.IndexIdentity = "closing-index.psb.m";
		input.BodyIdentity = "actual-closing-sound-body";
		input.BodyFilename = body_path;
		input.BodyBytes = body.size();
		input.IndexRead.Region = index.Region;
		input.IndexRead.OriginalFilename = "closing-index.psb.m";
		input.IndexRead.Seed = "synthetic-seed";
		input.IndexRead.MaxDecodedBytes = 16384;
		input.IndexRead.MaxMetadataBytes = 16384;
		Processor.QueueReadMount(std::move(input));
		FinishReader(Processor, Background);
		Processor.Update();
		Processor.Update();
		Processor.Update();
		Require(Mounts.MountCount() == 1 && !Mounts.HasInFlightReadMount(), "actual mounted Sound owner was not published");
		if (load) {
			Require(Sound.Load("asset/configuration.psb"), "actual containing configuration request failed");
			FinishReader(Processor, Background);
			Sound.AdvancePostLoop();
			Require(Sound.GetLoaded("asset/configuration.psb") && Sound.LoadArchive("common"), "actual configuration did not admit declared archive request");
			FinishReader(Processor, Background);
			Sound.AdvancePostLoop();
			Require(Sound.GetArchiveLoaded("common"), "actual containing archive request did not finish");
		}
	}

	~ActualClosingSound() noexcept {
		try {
			if (Output) {
				Output->Close();
			}
			if (!Sound.IsClosed()) {
				Sound.RequestClose();
				AwaitClose();
				Sound.Close();
			}
			Manager.Close();
			Background.Close();
		} catch (...) {
			std::terminate();
		}
	}

	sgre::resource::ArchiveMounts Mounts;
	sgre::core::BackgroundCallManager Background;
	sgre::resource::ResourceManagerProcess Manager;
	sgre::resource::ResourceCache& Cache;
	sgre::io::FileReadProcessor& Processor;
	sgre::audio::SoundResourceControl Sound;
	std::unique_ptr<sgre::audio::OfflineBufferedSoundPool> Output;

	static sgre::audio::SoundResourceControlLimits Limits() {
		sgre::audio::SoundResourceControlLimits limits;
		limits.Sources.Streaming.BufferFrames = 257;
		limits.Sources.Streaming.PrefillFrames = 127;

		return limits;
	}

	void ConnectOffline() {
		sgre::audio::OfflineSoundMasteringDescription description;
		description.SamplesPerSecond = 48000;
		description.Channels = 2;
		description.MaximumFrames = 256;
		Output = std::make_unique<sgre::audio::OfflineBufferedSoundPool>(Sound, description);
	}

	void AwaitClose() {
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (!Sound.CanClose()) {
			Require(std::chrono::steady_clock::now() < deadline, "containing actual Sound borrowers did not retire within fixture bound");
			FinishReader(Processor, Background);
			Sound.AdvancePostLoop();
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}
};

static void TestRealPendingSourceShutdown(const std::filesystem::path& directory) {
	ActualClosingSound runtime(directory);
	runtime.ConnectOffline();
	sgre::audio::BufferedSoundCueRequest request;
	request.ArchiveName = "common";
	request.CueName = "cue";
	request.StartPaused = true;
	request.PlayCount = -1;
	request.VerifiedWmaEncodeOptions = 31;
	request.IncrementalWma = true;
	sgre::audio::SoundMixParameters parameters;
	const auto cue = runtime.Sound.BufferedCues().Prepare(request, parameters, 2);
	Require(cue.Cue && !runtime.Sound.CanClose(), "actual pending source/cue borrower was absent from containing CanClose");
	const auto source = runtime.Sound.BufferedCues().InspectChannel(*cue.Cue, 0).Source;
	const auto configuration = *runtime.Cache.FindNamedResource("asset/configuration.psb");
	const auto archive = *runtime.Cache.FindNamedResource("asset/common.psb");
	bool saw_actual_postloop = false;
	auto& parallel = runtime.Sound.ParallelTasks();
	sgre::scene::ParallelTaskCallbacks callbacks;
	callbacks.Update = [&](sgre::scene::ParallelTaskHandle task) {
		saw_actual_postloop = true;
		Require(!runtime.Sound.CanClose() && runtime.Sound.GetLoaded("asset/configuration.psb"), "containing close admission escaped actual postloop traversal");
		RequireFailure([&] {
			runtime.Sound.RequestClose();
		},
		    "serialized receiving owner");
		parallel.SetStatus(task, sgre::scene::TaskStatus::Retire);
	};
	static_cast<void>(parallel.Append(std::move(callbacks)));
	runtime.Sound.RequestClose();
	Require(runtime.Sound.BufferedCues().ActiveCount() == 0 && runtime.Cache.Info(configuration).References == 1 && runtime.Cache.Info(archive).References == 1, "close request erased actual archive/configuration storage before final containing close");
	RequireFailure([&] {
		static_cast<void>(runtime.Sound.Load("asset/configuration.psb"));
	},
	    "pending close request");
	RequireFailure([&] {
		static_cast<void>(runtime.Sound.LoadArchive("common"));
	},
	    "pending close request");
	RequireFailure([&] {
		static_cast<void>(runtime.Sound.BufferedCues().Prepare(request, parameters, 2));
	},
	    "pending close request");
	runtime.Sound.AdvancePostLoop();
	Require(saw_actual_postloop, "close request bypassed actual Parallel-first postloop owner");
	runtime.AwaitClose();
	const auto retired = runtime.Sound.BufferedCues().InspectSource(source);
	Require(retired.Streaming && retired.Streaming->WorkerQuiescent && !retired.Streaming->InputBorrowActive && !retired.Samples.CallbackActive && retired.Samples.ConsumedFrames == 0, "containing shutdown invented source progress or omitted real worker/callback/borrow gates");
	runtime.Sound.Close();
	Require(runtime.Sound.IsClosed() && runtime.Sound.CanClose() && !runtime.Cache.Contains(configuration) && !runtime.Cache.Contains(archive), "final containing close did not release the same resource references after real acknowledgement");
}

static void TestPendingConfigurationCompletesThroughRealReader(const std::filesystem::path& directory) {
	ActualClosingSound runtime(directory, false);
	Require(runtime.Sound.Load("asset/configuration.psb") && runtime.Processor.PendingRawCount() == 1, "fixture did not produce an actual queued configuration request");
	const auto handle = *runtime.Cache.FindNamedResource("asset/configuration.psb");
	runtime.Sound.RequestClose();
	Require(!runtime.Sound.CanClose() && runtime.Sound.GetLoading("asset/configuration.psb") && runtime.Cache.Info(handle).References == 1, "close request faked pending serial/cache completion or dropped acquired input");
	RequireFailure([&] {
		runtime.Sound.Close();
	},
	    "pending serial/parallel borrower");
	runtime.AwaitClose();
	Require(runtime.Sound.GetLoaded("asset/configuration.psb"), "close polling skipped the actual original configuration completion callback");
	runtime.Sound.Close();
	Require(!runtime.Cache.Contains(handle), "final resource close leaked the actual acquired configuration generation");
}

static void TestFinalRetirementAfterActualReaderClose(const std::filesystem::path& directory) {
	ActualClosingSound runtime(directory);
	runtime.ConnectOffline();
	sgre::audio::BufferedSoundCueRequest request;
	request.ArchiveName = "common";
	request.CueName = "cue";
	request.StartPaused = true;
	request.PlayCount = -1;
	request.VerifiedWmaEncodeOptions = 31;
	request.IncrementalWma = true;
	const auto cue = runtime.Sound.BufferedCues().Prepare(request, {}, 2);
	Require(cue.Cue.has_value(), "actual source was not retained before serialized final retirement");
	const auto source = runtime.Sound.BufferedCues().InspectChannel(*cue.Cue, 0).Source;
	const auto configuration = *runtime.Cache.FindNamedResource("asset/configuration.psb");
	const auto archive = *runtime.Cache.FindNamedResource("asset/common.psb");
	runtime.Sound.RequestClose();
	runtime.AwaitClose();
	const auto retired = runtime.Sound.BufferedCues().InspectSource(source);
	Require(retired.Streaming && retired.Streaming->WorkerQuiescent && !retired.Streaming->InputBorrowActive && !retired.Samples.CallbackActive, "actual source/worker/input borrower survived the live-reader close join");
	auto& parallel = runtime.Sound.ParallelTasks();
	Require(parallel.Count() == 0 && runtime.Manager.CanClose(), "actual manager/pending Sound task did not reach serialized retirement");
	std::size_t released = 0;
	sgre::scene::ParallelTaskCallbacks callback;
	callback.Release = [&](auto) {
		++released;
	};
	static_cast<void>(parallel.Append(std::move(callback)));
	runtime.Manager.Close();
	Require(runtime.Manager.IsClosed() && runtime.Processor.IsClosed() && runtime.Cache.Info(configuration).References == 1 && runtime.Cache.Info(archive).References == 1, "reader retirement destroyed the concrete cache members or retained Sound references");
	Require(!runtime.Sound.CanClose(), "post-reader final retirement ignored an actual retained Parallel callback");
	RequireFailure([&] {
		runtime.Sound.Close();
	},
	    "actual cue/callback retirement");
	Require(released == 0 && !runtime.Sound.IsClosed(), "rejected post-reader close released a pending actual callback");
	parallel.Clear();
	Require(released == 1 && runtime.Sound.CanClose(), "quiescent final Sound retirement incorrectly requires an operational reader");
	RequireFailure([&] {
		static_cast<void>(runtime.Sound.GetLoaded("asset/configuration.psb"));
	},
	    "serialized receiving owner");
	RequireFailure([&] {
		runtime.Sound.RequestClose();
	},
	    "serialized receiving owner");
	RequireFailure([&] {
		runtime.Sound.AdvancePostLoop();
	},
	    "serialized receiving owner");
	const auto rejected = std::async(std::launch::async, [&] {
		Require(!runtime.Sound.CanClose(), "foreign thread acquired post-reader final retirement");
		try {
			runtime.Sound.Close();
		} catch (const std::runtime_error&) {
			return true;
		}
		return false;
	}).get();
	Require(rejected && !runtime.Sound.IsClosed(), "foreign retirement mutated the genuine serialized Sound owner");
	runtime.Output->Close();
	runtime.Sound.Close();
	Require(runtime.Sound.IsClosed() && !runtime.Cache.Contains(configuration) && !runtime.Cache.Contains(archive), "final Sound destruction did not release the retained actual cache generations");
}

static void TestSerializedAndUnresolvedEmptyShutdown() {
	std::size_t resolutions = 0;
	sgre::audio::SoundResourceControl control([&]() -> sgre::audio::SoundReadServices {
		++resolutions;
		throw std::runtime_error("empty shutdown must not resolve receiving services");
	},
	    "win", "synthetic-seed", false);
	const auto rejected = std::async(std::launch::async, [&] {
		Require(!control.CanClose(), "foreign thread acquired containing close admission");
		try {
			control.RequestClose();
		} catch (const std::runtime_error&) {
			return true;
		}
		return false;
	}).get();
	Require(rejected && resolutions == 0 && !control.IsClosed(), "foreign close request mutated/resolved the actual owner");
	control.RequestClose();
	Require(control.CanClose() && resolutions == 0, "empty actual shutdown invented cache/reader initialization");
	control.Close();
	Require(control.IsClosed() && resolutions == 0, "empty final close resolved unrelated services");
}

int main(int argc, char** argv) {
	try {
		Require(argc == 2, "actual containing Sound shutdown fixture requires ignored output directory");
		const std::filesystem::path directory(argv[1]);
		std::filesystem::create_directories(directory);
		TestRealPendingSourceShutdown(directory);
		TestPendingConfigurationCompletesThroughRealReader(directory);
		TestSerializedAndUnresolvedEmptyShutdown();
		TestFinalRetirementAfterActualReaderClose(directory);
		std::cout << "Actual containing Sound worker/resource shutdown and serialized lifetime fixtures passed; no hardware output\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
