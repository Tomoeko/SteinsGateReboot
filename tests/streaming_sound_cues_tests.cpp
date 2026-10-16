#include "sgre/audio/buffered_sound_cues.hpp"

#include "sgre/audio/offline_buffered_sound_pool.hpp"
#include "sgre/audio/wma_decoder.hpp"
#include "sgre/core/background_calls.hpp"

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

static const std::array<std::string_view, 25> kNames = {"id", "spec", "version", "archive", "common", "secondary", "voice", "cue", "other", "low", "high", "request", "device", "volume", "priority", "pitch", "uniq", "channelList", "archData", "pan", "fmt", "data", "nosrc", "missing", "dpds"};

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

class ActualStreamingCues {
public:
	ActualStreamingCues(std::filesystem::path directory, std::span<const CueVector> vectors, bool offline = false)
	    : Directory(std::move(directory)), Cache(0), Processor(Cache, Mounts, Background), Loads(Cache, Mounts, Processor, "win"), Cues(Loads, Limits()) {
		Require(Loads.LoadPrepared("configuration.psb", Prepared(Directory / "configuration.bin", CatalogPsb(), "configuration.psb")), "actual configuration preparation failed");
		FinishReader(Processor, Background);
		Loads.AdvanceSerialLoads();
		Require(Loads.LoadArchivePrepared("common", Prepared(Directory / "common.bin", CatalogPsb(vectors), "/common.psb")), "actual archive preparation failed");
		FinishReader(Processor, Background);
		Loads.AdvanceSerialLoads();
		Require(Loads.GetArchiveLoaded("common"), "actual archive serial production failed");
		if (offline) {
			sgre::audio::OfflineSoundMasteringDescription description;
			description.SamplesPerSecond = 48000;
			description.Channels = 2;
			description.MaximumFrames = 256;
			Output = std::make_unique<sgre::audio::OfflineBufferedSoundPool>(Cues, description);
		}
	}

	~ActualStreamingCues() noexcept {
		try {
			if (Output) {
				Output->Close();
			}
			Cues.RequestClose();
			if (!Cues.CanClose()) {
				Await([&] {
					return Cues.CanClose();
				});
			}
			Cues.Close();
			Loads.AdvanceSerialLoads();
			Loads.Close();
			Processor.Close();
			Background.Close();
		} catch (...) {
			std::terminate();
		}
	}

	std::filesystem::path Directory;
	sgre::resource::ResourceCache Cache;
	sgre::resource::ArchiveMounts Mounts;
	sgre::core::BackgroundCallManager Background;
	sgre::io::FileReadProcessor Processor;
	sgre::audio::SoundConfigurationLoads Loads;
	sgre::audio::BufferedSoundCues Cues;
	std::unique_ptr<sgre::audio::OfflineBufferedSoundPool> Output;

	static sgre::audio::BufferedSoundSourceLimits Limits() {
		sgre::audio::BufferedSoundSourceLimits limits;
		limits.Streaming.BufferFrames = 4097;
		limits.Streaming.PrefillFrames = 4000;

		return limits;
	}

	void Await(auto predicate) {
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (true) {
			Cues.CommitAndRetire();
			if (predicate()) {
				return;
			}
			Require(std::chrono::steady_clock::now() < deadline, "actual cue worker/retirement observation exceeded bound");
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}

	sgre::audio::BufferedSoundCuePreparation Prepare(std::string_view name = "cue", bool streaming = true, bool paused = false, std::int32_t count = 1) {
		sgre::audio::BufferedSoundCueRequest request;
		request.ArchiveName = "common";
		request.CueName = name;
		request.StartPaused = paused;
		request.PlayCount = count;
		request.VerifiedWmaEncodeOptions = 31;
		request.IncrementalWma = streaming;
		sgre::audio::SoundMixParameters parameters;

		return Cues.Prepare(request, parameters, 2);
	}
};

static void TestActualArchiveAndTypedConsumption(const std::filesystem::path& directory) {
	const std::array<CueVector, 1> vectors{CueVector{}};
	ActualStreamingCues runtime(directory, vectors);
	const auto archive = *runtime.Cache.FindNamedResource("/common.psb");
	const auto prepared = runtime.Prepare();
	Require(prepared.Cue && (prepared.Status == BufferedSoundCuePreparationStatus::PendingSources || prepared.Status == BufferedSoundCuePreparationStatus::Prepared), "stream cue published fake failure/readiness instead of actual pending/prepared state");
	const auto id = *prepared.Cue;
	runtime.Await([&] {
		return !runtime.Cues.InspectChannel(id, 0).PendingPreparation;
	});
	const auto source = runtime.Cues.InspectChannel(id, 0).Source;
	Require(runtime.Cues.Inspect(id).Preparation == BufferedSoundCuePreparationStatus::Prepared && runtime.Cues.InspectSource(source).Streaming->InputBorrowActive && runtime.Cache.Info(archive).References == 1, "actual cue archive/source/prefill identities differ");
	std::array<float, 17> rejected{};
	RequireFailure([&] {
		static_cast<void>(runtime.Cues.ConsumeChannelFrames(id, 0, rejected));
	},
	    "typed prefill");
	const auto encoded = EncodedImpulse();
	sgre::resource::WaveFormat format;
	format.Encoding = sgre::resource::WaveEncoding::WmaStandard;
	format.Channels = 1;
	format.SamplesPerSecond = 44100;
	format.AverageBytesPerSecond = 4000;
	format.BlockAlignment = 1487;
	format.BitsPerSample = 16;
	const std::array<std::uint32_t, 1> ledger{16384};
	const auto expected = sgre::audio::DecodeWmaStandardAudio(format, encoded, ledger, 31);
	std::vector<float> samples;
	samples.reserve(8192);
	std::array<float, 257> block{};
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (samples.size() < 8192) {
		Require(std::chrono::steady_clock::now() < deadline, "actual cue PCM consumption exceeded its observation bound");
		const auto read = runtime.Cues.ConsumeStreamingChannelFrames(id, 0, block);
		Require(read.Status == sgre::audio::WmaStreamReadStatus::Samples || read.Status == sgre::audio::WmaStreamReadStatus::Starved, "actual streaming cue unexpectedly changed source state");
		samples.insert(samples.end(), block.begin(), block.begin() + static_cast<std::ptrdiff_t>(read.Frames));
		if (read.Frames == 0) {
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}
	Require(samples == expected.Samples, "actual cue source differs from independent impulse decode");
	runtime.Await([&] {
		return !runtime.Cues.InspectSource(source).Samples.CallbackActive;
	});
	runtime.Cues.CommitAndRetire();
	Require(runtime.Cues.ActiveCount() == 0 && runtime.Cues.RetiringCount() == 0 && !runtime.Cues.InspectSource(source).Streaming->InputBorrowActive, "logical retirement kept a dangling encoded span or lost real terminal gates");
	Require(runtime.Cues.UnloadArchive("common") && !runtime.Cache.Contains(archive), "retired actual cue did not release its acquired archive owner");
}

static void TestPendingStopAndArchiveRetention(const std::filesystem::path& directory) {
	const std::array<CueVector, 1> vectors{CueVector{}};
	ActualStreamingCues runtime(directory, vectors);
	const auto configuration = *runtime.Cache.FindNamedResource("configuration.psb");
	const auto archive = *runtime.Cache.FindNamedResource("/common.psb");
	const auto prepared = runtime.Prepare("cue", true, true, -1);
	Require(prepared.Cue.has_value(), "infinite paused stream cue missing actual payload");
	const auto source = runtime.Cues.InspectChannel(*prepared.Cue, 0).Source;
	Require(runtime.Cues.UnloadConfiguration("configuration.psb") && runtime.Cues.ActiveCount() == 0 && runtime.Cues.RetiringCount() == 1, "unload did not erase ID before retaining actual pending source payload");
	runtime.Loads.AdvanceSerialLoads();
	Require(runtime.Cache.Info(configuration).References == 1 && runtime.Cache.Info(archive).References == 1 && !runtime.Loads.CanClose() && runtime.Cues.InspectSource(source).Samples.CallbackActive, "archive/configuration released before real source stop acknowledgement");
	RequireFailure([&] {
		runtime.Loads.Close();
	},
	    "cue resource borrower");
	runtime.Cues.RequestClose();
	runtime.Await([&] {
		return runtime.Cues.CanClose();
	});
	const auto retired = runtime.Cues.InspectSource(source);
	Require(retired.Streaming->WorkerQuiescent && !retired.Samples.CallbackActive && !retired.Streaming->InputBorrowActive, "stop omitted worker/callback/encoded-borrow gates");
	runtime.Cues.Close();
	runtime.Loads.AdvanceSerialLoads();
	Require(!runtime.Cache.Contains(configuration) && !runtime.Cache.Contains(archive), "actual deferred archive/config retirement did not follow final cue borrow");
}

static void TestPartialWorkerFailureAndReuse(const std::filesystem::path& directory) {
	CueVector partial;
	partial.Channels = 2;
	partial.CorruptPacket = true;
	const std::array<CueVector, 1> vectors{partial};
	ActualStreamingCues runtime(directory, vectors);
	const auto prepared = runtime.Prepare("cue", true, true);
	Require(prepared.Cue.has_value(), "partial worker failure lost native logical payload publication");
	const auto id = *prepared.Cue;
	runtime.Await([&] {
		return !runtime.Cues.InspectChannel(id, 0).PendingPreparation && !runtime.Cues.InspectChannel(id, 1).PendingPreparation;
	});
	const auto good = runtime.Cues.InspectChannel(id, 0);
	const auto bad = runtime.Cues.InspectChannel(id, 1);
	Require(runtime.Cues.Inspect(id).Preparation == BufferedSoundCuePreparationStatus::PartiallyPrepared && good.State.Samples.CallbackActive && bad.State.Streaming->Failed && !bad.State.Samples.CallbackActive && !bad.Error.empty(), "actual worker failure became fake EOF/prepared success or discarded the surviving channel");
	Require(runtime.Cues.Stop(id), "partial cue stop failed");
	runtime.Await([&] {
		return runtime.Cues.RetiringCount() == 0;
	});
	const auto again = runtime.Prepare("cue", false, true);
	Require(again.Cue && again.Status == BufferedSoundCuePreparationStatus::PartiallyPrepared && !runtime.Cues.InspectChannel(*again.Cue, 0).State.Streaming, "actual completed slot reuse did not preserve the old whole-buffer path");
}

static void TestUnchangedPcmPolicy(const std::filesystem::path& directory) {
	CueVector pcm;
	pcm.Wma = false;
	const std::array<CueVector, 1> vectors{pcm};
	ActualStreamingCues runtime(directory, vectors);
	const auto prepared = runtime.Prepare("cue", true);
	Require(prepared.Cue && prepared.Status == BufferedSoundCuePreparationStatus::Prepared && !runtime.Cues.InspectChannel(*prepared.Cue, 0).State.Streaming, "explicit WMA streaming policy changed ordinary PCM preparation");
	std::array<float, 4> samples{};
	Require(runtime.Cues.ConsumeChannelFrames(*prepared.Cue, 0, samples) == 4 && samples == std::array<float, 4>{0.25f, 0.5f, -0.5f, 0}, "ordinary PCM changed under typed WMA cue ownership");
}

static void TestRealOfflinePendingAttachment(const std::filesystem::path& directory) {
	const std::array<CueVector, 1> vectors{CueVector{}};
	ActualStreamingCues runtime(directory, vectors, true);
	const auto prepared = runtime.Prepare("cue", true, true);
	Require(prepared.Cue.has_value(), "offline cue failed actual logical publication");
	const auto id = *prepared.Cue;
	const auto source = runtime.Cues.InspectChannel(id, 0).Source;
	runtime.Await([&] {
		return !runtime.Cues.InspectChannel(id, 0).PendingPreparation;
	});
	Require(runtime.Output->SourceNodeCount() == 1 && runtime.Cues.InspectChannel(id, 0).State.StreamingOutputAttached, "prefilled actual cue never attached the same real offline source/matrix");
	Require(runtime.Cues.ApplyControl(id, sgre::audio::BufferedSoundCueControl::Resume), "offline actual cue resume failed");
	std::array<float, 128> output{};
	const auto rendered = runtime.Output->RenderTyped(output);
	Require(rendered.MasterFrames == 64 && rendered.SourceCount == 1 && rendered.Sources[0].Streaming && rendered.Sources[0].ConsumedFrames != 0 && runtime.Cues.InspectChannel(id, 0).State.Samples.ConsumedFrames == rendered.Sources[0].ConsumedFrames, "actual offline callback did not consume the exact cue source in source-clock units");
	Require(runtime.Cues.Stop(id), "offline cue stop failed");
	runtime.Await([&] {
		return runtime.Cues.RetiringCount() == 0;
	});
	Require(runtime.Output->SourceNodeCount() == 1 && !runtime.Cues.InspectSource(source).Streaming->InputBorrowActive, "cue retirement destroyed converter tail or retained a released archive span");
}

int main(int argc, char** argv) {
	try {
		Require(argc == 2, "actual streaming cue fixtures require an ignored output directory");
		const std::filesystem::path directory(argv[1]);
		std::filesystem::create_directories(directory);
		TestActualArchiveAndTypedConsumption(directory);
		TestPendingStopAndArchiveRetention(directory);
		TestPartialWorkerFailureAndReuse(directory);
		TestUnchangedPcmPolicy(directory);
		TestRealOfflinePendingAttachment(directory);
		std::cout << "Actual streaming cue/archive, partial failure, stop acknowledgement, ordinary PCM and offline attachment fixtures passed; no hardware output\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
