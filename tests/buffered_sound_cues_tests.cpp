#include "sgre/audio/buffered_sound_cues.hpp"

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

static const std::array<std::string_view, 24> kNames = {"id", "spec", "version", "archive", "common", "secondary", "voice", "cue", "other", "low", "high", "request", "device", "volume", "priority", "pitch", "uniq", "channelList", "archData", "pan", "fmt", "data", "nosrc", "missing"};

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

class ActualCueRuntime {
public:
	ActualCueRuntime(std::filesystem::path directory, std::span<const CueVector> vectors, sgre::audio::BufferedSoundCueLimits cue_limits = {}) : Directory(std::move(directory)), Cache(0), Processor(Cache, Mounts, Background), Loads(Cache, Mounts, Processor, "win"), Cues(Loads, {}, cue_limits) {
		Require(Loads.LoadPrepared("configuration.psb", Prepared(Directory / "configuration.bin", CatalogPsb(), "configuration.psb")), "actual configuration preparation failed");
		FinishReader(Processor, Background);
		Loads.AdvanceSerialLoads();
		LoadArchive("common", vectors);
	}

	~ActualCueRuntime() noexcept {
		try {
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

	void LoadArchive(std::string name, std::span<const CueVector> vectors) {
		const auto bytes = CatalogPsb(vectors);
		Require(Loads.LoadArchivePrepared(name, Prepared(Directory / (name + ".bin"), bytes, "/" + name + ".psb")), "actual archive preparation failed");
		FinishReader(Processor, Background);
		Loads.AdvanceSerialLoads();
		Require(Loads.GetArchiveLoaded(name), "actual archive serial production failed");
	}

	[[nodiscard]] sgre::audio::BufferedSoundCuePreparation Prepare(std::string_view name = "cue", float priority = 0, bool paused = false, std::string_view archive = "common", bool limit_to_archive = true, const sgre::audio::SoundMixParameters& parameters = sgre::audio::SoundMixParameters()) {
		sgre::audio::BufferedSoundCueRequest request;
		request.ArchiveName = archive;
		request.CueName = name;
		request.Priority = priority;
		request.StartPaused = paused;
		request.LimitUniquenessToArchive = limit_to_archive;
		auto actual_parameters = parameters;
		actual_parameters.UseSourcePan = true;

		return Cues.Prepare(request, actual_parameters, 2);
	}
};

static void TestActualResourcesAndMixedConsumption(const std::filesystem::path& directory) {
	const std::array<CueVector, 1> vectors{CueVector{}};
	ActualCueRuntime runtime(directory, vectors);
	const auto archive_handle = *runtime.Cache.FindNamedResource("/common.psb");
	const auto result = runtime.Prepare();
	Require(result.Cue && result.Status == BufferedSoundCuePreparationStatus::Prepared && runtime.Cues.ActiveCount() == 1 && runtime.Cache.Info(archive_handle).References == 1, "cue must own actual prepared source/resource payload without another cache Acquire");
	const auto info = runtime.Cues.Inspect(*result.Cue);
	Require(info.ConfigurationFilename == "configuration.psb" && info.ArchiveName == "common" && info.CueName == "cue" && info.Channels == 1 && info.CallbackActive, "actual cue selected resource/definition identity differs");
	const auto channel = runtime.Cues.InspectChannel(*result.Cue, 0);
	Require(channel.State.Samples.BufferFrames == 4 && channel.State.Samples.CallbackActive && channel.State.Samples.Running && !channel.State.NativeBufferContextPresent && channel.Mix && !channel.Mix->FrequencyRatio && channel.Mix->Matrix[0] == 0.125f && channel.Mix->Matrix[1] == 0.375f, "actual source frames, corrected nosrc or recovered definition/pan mix differ");
	std::array<float, 4> first{};
	Require(runtime.Cues.ConsumeMixedChannelFrames(*result.Cue, 0, first) == 2 && first == std::array<float, 4>{0.03125f, 0.09375f, 0.0625f, 0.1875f}, "actual mixed cue output differs from independent PCM/gain/pan arithmetic");
	Require(runtime.Cues.Inspect(*result.Cue).CallbackActive && runtime.Cues.InspectChannel(*result.Cue, 0).State.Samples.RemainingBufferFrames == 2, "cue may not complete before its real decoded frames");
	std::array<float, 6> tail{1, 1, 1, 1, 1, 1};
	Require(runtime.Cues.ConsumeMixedChannelFrames(*result.Cue, 0, tail) == 2 && tail == std::array<float, 6>{0.9375f, 0.8125f, 1, 1, 1, 1} && !runtime.Cues.Inspect(*result.Cue).CallbackActive, "final actual mix must accumulate complete frames and preserve unused destination tail");
	runtime.Cues.CommitAndRetire();
	Require(runtime.Cues.ActiveCount() == 0 && runtime.Cues.RetiringCount() == 0 && runtime.Cues.InspectSource(channel.Source).RecoveredSchedule.Exists && !runtime.Cues.InspectSource(channel.Source).Samples.CallbackActive, "cue retirement must release real cue/slots while retaining idle pooled source storage");
	RequireFailure([&] {
		static_cast<void>(runtime.Cues.Inspect(*result.Cue));
	},
	    "active ID lookup");
	Require(runtime.Cues.UnloadArchive("common") && runtime.Loads.ArchiveOwnerCount() == 0 && !runtime.Cache.Contains(archive_handle), "archive without remaining cue borrows must release its actual acquired reference");
}

static void TestStopConfigurationAndActualRetiredBorrows(const std::filesystem::path& directory) {
	const std::array<CueVector, 1> vectors{CueVector{}};
	ActualCueRuntime runtime(directory, vectors);
	const auto configuration_handle = *runtime.Cache.FindNamedResource("configuration.psb");
	const auto archive_handle = *runtime.Cache.FindNamedResource("/common.psb");
	const auto result = runtime.Prepare("cue", 0, true);
	Require(result.Cue && runtime.Cues.Inspect(*result.Cue).Paused && !runtime.Cues.InspectChannel(*result.Cue, 0).State.Samples.Running && runtime.Cues.Inspect(*result.Cue).CallbackActive, "paused cue requires actual queued source and callback owner");
	const auto source = runtime.Cues.InspectChannel(*result.Cue, 0).Source;
	Require(runtime.Cues.UnloadConfiguration("configuration.psb") && runtime.Cues.ActiveCount() == 0 && runtime.Cues.RetiringCount() == 1 && !runtime.Loads.GetLoaded("configuration.psb"), "native unload must remove lookup and move actual paused cue into retirement before configuration retirement");
	runtime.Loads.AdvanceSerialLoads();
	Require(runtime.Loads.RetiringCount() == 1 && runtime.Loads.ArchiveOwnerCount() == 1 && runtime.Cache.Info(configuration_handle).References == 1 && runtime.Cache.Info(archive_handle).References == 1 && runtime.Cues.InspectSource(source).Samples.CallbackActive && !runtime.Loads.CanClose(), "configuration/archive/cache owners must survive requested stop until actual source flush completion");
	RequireFailure([&] {
		runtime.Loads.Close();
	},
	    "cue resource borrower");
	runtime.Cues.CommitAndRetire();
	Require(runtime.Cues.RetiringCount() == 0 && !runtime.Cues.InspectSource(source).Samples.CallbackActive && runtime.Cues.InspectSource(source).RecoveredSchedule.Exists, "committed real flush must gate cue retirement while leaving the pooled source alive");
	runtime.Loads.AdvanceSerialLoads();
	Require(runtime.Loads.RetiringCount() == 0 && runtime.Loads.ArchiveOwnerCount() == 0 && !runtime.Cache.Contains(configuration_handle) && !runtime.Cache.Contains(archive_handle), "final real cue borrower release must precede actual archive/configuration cache retirement");
}

static void TestPartialAndFailedSourcePublication(const std::filesystem::path& directory) {
	CueVector partial;
	partial.Channels = 2;
	partial.InvalidChannelMask = 2;
	CueVector failed;
	failed.Name = "other";
	failed.InvalidChannelMask = 1;
	const std::array<CueVector, 2> vectors{partial, failed};
	ActualCueRuntime runtime(directory, vectors);
	const auto archive_handle = *runtime.Cache.FindNamedResource("/common.psb");
	const auto result = runtime.Prepare();
	Require(result.Cue && result.Status == BufferedSoundCuePreparationStatus::PartiallyPrepared && runtime.Cues.Inspect(*result.Cue).CallbackActive, "native logical cue publication must continue after one explicit source failure");
	Require(runtime.Cues.InspectChannel(*result.Cue, 0).State.Samples.CallbackActive && !runtime.Cues.InspectChannel(*result.Cue, 1).State.RecoveredSchedule.Exists && !runtime.Cues.InspectChannel(*result.Cue, 1).Error.empty(), "failed channel must not acquire nominal source readiness or replace real successful channel activity");
	std::array<float, 4> samples{};
	RequireFailure([&] {
		static_cast<void>(runtime.Cues.ConsumeChannelFrames(*result.Cue, 1, samples));
	},
	    "explicit source preparation failure");
	Require(runtime.Cues.ConsumeChannelFrames(*result.Cue, 0, samples) == 4 && samples == std::array<float, 4>{0.25f, 0.5f, -0.5f, 0}, "partially prepared cue must consume the successful channel's actual independent PCM");
	const auto next = runtime.Prepare("other");
	Require(next.Cue && next.Status == BufferedSoundCuePreparationStatus::FailedSources && !runtime.Cues.Inspect(*next.Cue).CallbackActive && !runtime.Cues.InspectChannel(*next.Cue, 0).State.RecoveredSchedule.Exists, "all-source failure may identify a real logical cue but must remain an explicit failed preparation");
	RequireFailure([&] {
		static_cast<void>(runtime.Cues.Inspect(*result.Cue));
	},
	    "active ID lookup");
	runtime.Cues.CommitAndRetire();
	Require(runtime.Cues.ActiveCount() == 0 && runtime.Cues.RetiringCount() == 0 && runtime.Cache.Info(archive_handle).References == 1, "failed and completed cues must release actual slot/resource borrows after their last callback activity");
}

static void TestRejectedAllocationRetainsEarlierEvictions(const std::filesystem::path& directory) {
	CueVector low;
	low.Name = "low";
	low.Channels = 7;
	low.Priority = 1;
	CueVector high;
	high.Name = "high";
	high.Priority = 100;
	CueVector request;
	request.Name = "request";
	request.Channels = 8;
	request.Priority = 2;
	CueVector replacement;
	replacement.Name = "other";
	replacement.Channels = 7;
	replacement.Priority = 2;
	const std::array<CueVector, 4> vectors{low, high, request, replacement};
	ActualCueRuntime runtime(directory, vectors);
	const auto first = runtime.Prepare("low");
	const auto second = runtime.Prepare("high");
	Require(first.Cue && second.Cue && runtime.Cues.ActiveCount() == 2, "allocator fixture requires real decoded ownership of all eight pool1 slots");
	const auto first_source = runtime.Cues.InspectChannel(*first.Cue, 0).Source;
	const auto blocked = runtime.Prepare("request");
	Require(!blocked.Cue && blocked.Status == BufferedSoundCuePreparationStatus::RejectedSlots && runtime.Cues.ActiveCount() == 1 && runtime.Cues.RetiringCount() == 1 && !runtime.Cues.InspectSource(first_source).Samples.CallbackActive && runtime.Cues.Inspect(*second.Cue).CallbackActive, "rejected native selection must retain ordered earlier eviction and its real retiring payload");
	RequireFailure([&] {
		static_cast<void>(runtime.Cues.Inspect(*first.Cue));
	},
	    "active ID lookup");
	runtime.Cues.CommitAndRetire();
	const auto reused = runtime.Prepare("other");
	Require(reused.Cue && reused.Status == BufferedSoundCuePreparationStatus::Prepared && runtime.Cues.ActiveCount() == 2 && runtime.Cues.RetiringCount() == 0, "actual completed retired slots must become available to real replacement sources");
	for (std::size_t channel = 0; channel < 7; ++channel) {
		const auto info = runtime.Cues.InspectChannel(*reused.Cue, channel);
		Require(info.Source.Slot == channel && info.State.Samples.BufferFrames == 4 && info.State.Samples.CallbackActive, "reused source prefix must preserve native free-slot order with real decoded buffers");
	}
}

static void TestUniquenessAndOrderedArchiveLookup(const std::filesystem::path& directory) {
	CueVector low;
	low.Name = "low";
	low.Priority = 1;
	low.Unique = true;
	CueVector request;
	request.Name = "request";
	request.Priority = 50;
	request.Unique = true;
	const std::array<CueVector, 2> common{low, request};
	ActualCueRuntime runtime(directory, common);
	CueVector high;
	high.Name = "high";
	high.Priority = 100;
	high.Unique = true;
	const std::array<CueVector, 1> secondary{high};
	runtime.LoadArchive("secondary", secondary);
	const auto first = runtime.Prepare("low");
	const auto second = runtime.Prepare("high", 0, false, "");
	Require(first.Cue && second.Cue && runtime.Cues.Inspect(*second.Cue).ArchiveName == "secondary", "empty archive lookup must use actual ordered completed archives until the cue exists");
	const auto blocked = runtime.Prepare("request", 0, false, "common", false);
	Require(!blocked.Cue && blocked.Status == BufferedSoundCuePreparationStatus::RejectedUniqueness && runtime.Cues.ActiveCount() == 1 && runtime.Cues.RetiringCount() == 1 && runtime.Cues.Inspect(*second.Cue).CallbackActive, "global uniqueness rejection must retain its earlier real low-priority eviction");
	RequireFailure([&] {
		static_cast<void>(runtime.Cues.Inspect(*first.Cue));
	},
	    "active ID lookup");
	runtime.Cues.CommitAndRetire();
	const auto common_handle = *runtime.Cache.FindNamedResource("/common.psb");
	Require(runtime.Cues.UnloadArchive("common") && !runtime.Cache.Contains(common_handle) && runtime.Cues.Inspect(*second.Cue).CallbackActive, "unloading one selected actual archive must preserve another archive's actual cue/source borrows");
	const auto secondary_handle = *runtime.Cache.FindNamedResource("/secondary.psb");
	Require(runtime.Cues.UnloadArchive("secondary") && runtime.Cache.Contains(secondary_handle), "retiring active archive must retain its actual reference until source flush completion");
	runtime.Cues.CommitAndRetire();
	runtime.Loads.AdvanceSerialLoads();
	Require(!runtime.Cache.Contains(secondary_handle), "second archive must retire after its last actual queued source/cue borrower");
}

static void TestUserListeningControls(const std::filesystem::path& directory) {
	const std::array<CueVector, 1> vectors{CueVector{}};
	ActualCueRuntime runtime(directory, vectors);
	sgre::audio::UserAudioSettings settings;
	settings.Music = 20;
	settings.Voice = 90;
	settings.Effects = 60;
	runtime.Cues.SetUserAudioSettings(settings);
	sgre::audio::BufferedSoundCueRequest request;
	request.ArchiveName = "common";
	request.CueName = "cue";
	request.Group = "bgm";
	sgre::audio::SoundMixParameters parameters;
	parameters.UseSourcePan = true;
	const auto prepared = runtime.Cues.Prepare(request, parameters, 2);
	Require(prepared.Cue.has_value(), "listening fixture could not prepare its real cue");
	const auto id = *prepared.Cue;
	const auto baseline = runtime.Cues.InspectChannel(id, 0).Mix->Matrix[0] / 0.2f;
	Require(baseline > 0 && runtime.Cues.GroupVolume("bgm") == 1 && runtime.Cues.VoiceVolume(id) == 1, "user music level overwrote original script gains");
	runtime.Cues.SetGroupVolume("bgm", 0.5f);
	settings.Music = 0;
	runtime.Cues.SetUserAudioSettings(settings);
	Require(runtime.Cues.InspectChannel(id, 0).Mix->Matrix[0] == 0, "user mute failed on a running source");
	Require(runtime.Cues.SetVoiceVolume(id, 0.5f), "original voice gain update failed");
	settings.Music = 100;
	runtime.Cues.SetUserAudioSettings(settings);
	Require(runtime.Cues.InspectChannel(id, 0).Mix->Matrix[0] == baseline * 0.25f && runtime.Cues.GroupVolume("bgm") == 0.5f && runtime.Cues.VoiceVolume(id) == 0.5f, "unmuting lost original script fade factors");
	Require(sgre::audio::UserAudioGroupGain(settings, "voice") == 0.9f && sgre::audio::UserAudioGroupGain(settings, "se") == 0.6f, "voice and sound-effect controls were crossed");
}

static void TestExplicitMixAndReadinessBoundaries(const std::filesystem::path& directory) {
	CueVector pitched;
	pitched.Name = "other";
	pitched.DisableRateConversion = false;
	CueVector continuous;
	continuous.Name = "high";
	continuous.Device = 2;
	const std::array<CueVector, 3> vectors{CueVector{}, pitched, continuous};
	sgre::audio::BufferedSoundCueLimits limits;
	limits.MaxConsumeFrames = 2;
	ActualCueRuntime runtime(directory, vectors, limits);
	const auto missing = runtime.Prepare("missing");
	Require(!missing.Cue && missing.Status == BufferedSoundCuePreparationStatus::MissingDefinition, "missing actual definition may not become a nominal cue/source");
	const auto prepared = runtime.Prepare();
	Require(prepared.Cue && runtime.Cues.Pause(*prepared.Cue), "actual cue pause must address its owned source slots");
	runtime.Cues.CommitAndRetire();
	std::array<float, 2> paused{9, 9};
	Require(runtime.Cues.ConsumeChannelFrames(*prepared.Cue, 0, paused) == 0 && paused == std::array<float, 2>{9, 9} && runtime.Cues.Inspect(*prepared.Cue).CallbackActive, "paused cue must retain actual frame/borrow ownership");
	Require(runtime.Cues.Resume(*prepared.Cue), "actual cue resume failed");
	runtime.Cues.CommitAndRetire();
	std::array<float, 6> too_many{};
	RequireFailure([&] {
		static_cast<void>(runtime.Cues.ConsumeMixedChannelFrames(*prepared.Cue, 0, too_many));
	},
	    "bounded complete");
	Require(runtime.Cues.InspectChannel(*prepared.Cue, 0).State.Samples.ConsumedFrames == 0, "unsupported mix extent must fail before consuming actual samples");
	Require(runtime.Cues.Stop(*prepared.Cue), "actual cue stop failed");
	runtime.Cues.CommitAndRetire();
	sgre::audio::SoundMixParameters parameters;
	parameters.VoicePitchOctaves = 1;
	const auto pitched_result = runtime.Prepare("other", 0, false, "common", true, parameters);
	Require(pitched_result.Cue && runtime.Cues.InspectChannel(*pitched_result.Cue, 0).Mix->FrequencyRatio == 2, "actual requested/definition pitch must reach its recovered frequency decision");
	std::array<float, 4> raw{};
	RequireFailure([&] {
		static_cast<void>(runtime.Cues.ConsumeMixedChannelFrames(*pitched_result.Cue, 0, raw));
	},
	    "resampling remains open");
	Require(runtime.Cues.InspectChannel(*pitched_result.Cue, 0).State.Samples.ConsumedFrames == 0 && runtime.Cues.ConsumeChannelFrames(*pitched_result.Cue, 0, raw) == 4 && raw == std::array<float, 4>{0.25f, 0.5f, -0.5f, 0}, "unrecovered pitch timing must fail before consumption while source-rate samples remain real");
	RequireFailure([&] {
		static_cast<void>(runtime.Prepare("high"));
	},
	    "continuous type2 remains open");
	Require(runtime.Loads.ArchiveFile("common").Document().SerializedSize == runtime.Loads.ArchiveFile("common").BackingStorage().size(), "ordinary full-backing guard must remain strict through actual cue integration");
}

int main(int argc, char** argv) {
	try {
		Require(argc == 2, "buffered cue fixtures require an ignored output directory");
		const std::filesystem::path directory(argv[1]);
		std::filesystem::create_directories(directory);
		TestActualResourcesAndMixedConsumption(directory);
		TestStopConfigurationAndActualRetiredBorrows(directory);
		TestPartialAndFailedSourcePublication(directory);
		TestRejectedAllocationRetainsEarlierEvictions(directory);
		TestUniquenessAndOrderedArchiveLookup(directory);
		TestExplicitMixAndReadinessBoundaries(directory);
		TestUserListeningControls(directory);
		std::cout << "buffered sound cues: 7 actual resource/source fixture groups passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
