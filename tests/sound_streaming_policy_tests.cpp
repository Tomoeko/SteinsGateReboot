#include "sgre/script/sound_bindings.hpp"

#include "sgre/script/source_compiler.hpp"

#include "sgre/audio/offline_buffered_sound_pool.hpp"
#include "sgre/core/background_calls.hpp"
#include "sgre/resource/resource_cipher.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
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
#include <variant>
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

class ActualScriptStreaming {
public:
	explicit ActualScriptStreaming(const std::filesystem::path& directory)
	    : Cache(0), Processor(Cache, Mounts, Background), Sound(Cache, Mounts, Processor, "win", "synthetic-seed", false, Limits()) {
		CueVector pcm;
		pcm.Name = "other";
		pcm.Wma = false;
		const std::array<CueVector, 2> vectors{CueVector{}, pcm};
		const auto configuration = CatalogPsb();
		const auto archive = CatalogPsb(vectors);
		Bytes body = configuration;
		Append(body, archive);
		const auto body_path = directory / "policy-sound-body.bin";
		static_cast<void>(Prepared(body_path, body, "unused-write-mapping"));
		const auto index = Prepared(directory / "policy-sound-index.bin", Wrap(IndexPsb(configuration.size(), archive.size()), "policy-index.psb.m"), "policy-index.psb.m");
		sgre::resource::ArchiveReadMountInput input;
		input.Prefix = "asset/";
		input.IndexIdentity = "policy-index.psb.m";
		input.BodyIdentity = "actual-policy-sound-body";
		input.BodyFilename = body_path;
		input.BodyBytes = body.size();
		input.IndexRead.Region = index.Region;
		input.IndexRead.OriginalFilename = "policy-index.psb.m";
		input.IndexRead.Seed = "synthetic-seed";
		input.IndexRead.MaxDecodedBytes = 16384;
		input.IndexRead.MaxMetadataBytes = 16384;
		Processor.QueueReadMount(std::move(input));
		FinishReader(Processor, Background);
		Processor.Update();
		Processor.Update();
		Processor.Update();
		Require(Mounts.MountCount() == 1 && !Mounts.HasInFlightReadMount(), "script policy requires its actual received archive index");
		Require(Sound.Load("asset/configuration.psb"), "script policy actual configuration request failed");
		FinishReader(Processor, Background);
		Sound.AdvancePostLoop();
		Require(Sound.GetLoaded("asset/configuration.psb") && Sound.LoadArchive("common"), "actual configuration did not admit declared archive request");
		FinishReader(Processor, Background);
		Sound.AdvancePostLoop();
		Require(Sound.GetArchiveLoaded("common"), "actual script-policy archive did not finish serial production");
		sgre::audio::OfflineSoundMasteringDescription description;
		description.SamplesPerSecond = 48000;
		description.Channels = 2;
		description.MaximumFrames = 256;
		Output = std::make_unique<sgre::audio::OfflineBufferedSoundPool>(Sound, description);
		Bindings = std::make_unique<sgre::script::ScriptSoundBindings>(Vm, Sound);
		Vm.NewSlot(Vm.RootTable(), "play_options", Vm.CreateTable());
		Vm.NewSlot(Vm.RootTable(), "play_count", std::int32_t{-1});
	}

	~ActualScriptStreaming() noexcept {
		try {
			Bindings.reset();
			Vm.Close();
			Output->Close();
			Sound.RequestClose();
			Await([&] {
				return Sound.CanClose();
			});
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
	sgre::script::SquirrelVm Vm;
	std::unique_ptr<sgre::audio::OfflineBufferedSoundPool> Output;
	std::unique_ptr<sgre::script::ScriptSoundBindings> Bindings;
	std::size_t GeometryReads = 0;

	static sgre::audio::SoundResourceControlLimits Limits() {
		sgre::audio::SoundResourceControlLimits limits;
		limits.Sources.Streaming.BufferFrames = 4097;
		limits.Sources.Streaming.PrefillFrames = 4000;

		return limits;
	}

	std::function<std::uint8_t()> Geometry() {
		return [this] {
			++GeometryReads;
			return Output->Description().Channels;
		};
	}

	void Await(auto predicate) {
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (true) {
			Sound.AdvancePostLoop();
			if (predicate()) {
				return;
			}
			Require(std::chrono::steady_clock::now() < deadline, "actual script-policy source/retirement observation exceeded bound");
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}

	sgre::script::SquirrelValue Run(std::string_view source) {
		const auto closure = Vm.CreateClosure(sgre::script::CompileSquirrelSource(Vm, source, "actual-streaming-policy-fixture"));
		const std::array<sgre::script::SquirrelValue, 1> arguments{Vm.RootTable()};
		const auto result = Vm.Invoke(closure, arguments);
		Require(result.State == sgre::script::SquirrelRunState::Completed && result.Instructions != 0, "compiled policy fixture skipped actual Squirrel instructions/native call");
		return result.Value;
	}

	sgre::audio::BufferedSoundCueId Play(std::string_view source) {
		const auto result = Run(source);
		Require(std::holds_alternative<std::int32_t>(result) && std::get<std::int32_t>(result) >= 0, "actual script call returned no genuine logical cue ID");
		return {static_cast<std::uint32_t>(std::get<std::int32_t>(result))};
	}

	void Select(sgre::audio::BufferedSoundCueId id) {
		const sgre::script::SquirrelString key{"selected_cue"};
		if (Vm.HasSlot(Vm.RootTable(), key)) {
			Vm.SetSlot(Vm.RootTable(), key, static_cast<std::int32_t>(id.Value));
		} else {
			Vm.NewSlot(Vm.RootTable(), key, static_cast<std::int32_t>(id.Value));
		}
	}

	void RequireUnchangedWma() {
		const auto& file = Sound.ArchiveFile("common");
		const auto definitions = sgre::resource::ReadSoundArchiveDefinitions(file.Document());
		const auto data = sgre::audio::ReadBufferedSoundSourceData(file, definitions.at("cue"), 0, 1);
		const Bytes expected_format{0x61, 1, 1, 0, 0x44, 0xac, 0, 0, 0xa0, 0x0f, 0, 0, 0xcf, 5, 16, 0, 0, 0};
		const auto expected_packet = EncodedImpulse();
		Require(std::equal(data.FormatBytes.begin(), data.FormatBytes.end(), expected_format.begin(), expected_format.end()) && data.Description.Format.Extra.empty() && std::equal(data.EncodedBytes.begin(), data.EncodedBytes.end(), expected_packet.begin(), expected_packet.end()) && data.Description.DecodedPacketCumulativeBytes == std::vector<std::uint32_t>{16384}, "playback policy rewrote actual fmt/encoded/dpds inputs or inferred an absent extension");
	}
};

static void TestValidationRollbackAndDefaultWholeBuffer(const std::filesystem::path& directory) {
	ActualScriptStreaming fixture(directory);
	const auto verified = sgre::script::SoundBufferedDecoderProfile::VerifiedWmaOptions31;
	const auto stream = sgre::script::SoundBufferedPlaybackMode::IncrementalWma;
	RequireFailure([&] {
		fixture.Bindings->ConfigureBufferedPlayback({}, verified, stream);
	},
	    "one actual live output");
	RequireFailure([&] {
		fixture.Bindings->ConfigureBufferedPlayback(fixture.Geometry(), std::nullopt, stream);
	},
	    "explicitly verified WMA options31");
	RequireFailure([&] {
		fixture.Bindings->ConfigureBufferedPlayback(fixture.Geometry(), static_cast<sgre::script::SoundBufferedDecoderProfile>(7), stream);
	},
	    "decoder profile is not explicitly verified");
	RequireFailure([&] {
		fixture.Bindings->ConfigureBufferedPlayback(fixture.Geometry(), verified, static_cast<sgre::script::SoundBufferedPlaybackMode>(7));
	},
	    "mode is outside");
	Require(fixture.GeometryReads == 0 && fixture.Output->SourceNodeCount() == 0 && fixture.Sound.BufferedCues().ActiveCount() == 0, "rejected configure partially committed policy, read geometry or created nominal sources");
	fixture.Bindings->ConfigureBufferedPlayback(fixture.Geometry(), verified);
	const auto whole = fixture.Play("return Sound.playVoice(\"common\", \"cue\");");
	const auto channel = fixture.Sound.BufferedCues().InspectChannel(whole, 0);
	Require(!channel.State.Streaming && channel.State.Samples.BufferFrames == 8192 && channel.State.Samples.CallbackActive && channel.State.Samples.LoopRepeatsRemaining == 0 && fixture.Output->SourceNodeCount() == 1, "existing two-argument configure or omitted play count changed the complete-buffer/native submission path");
	fixture.RequireUnchangedWma();
	const auto pcm = fixture.Play("return Sound.playVoice(\"common\", \"other\");");
	std::array<float, 4> samples{};
	Require(!fixture.Sound.BufferedCues().InspectChannel(pcm, 0).State.Streaming && fixture.Sound.BufferedCues().ConsumeChannelFrames(pcm, 0, samples) == 4 && samples == std::array<float, 4>{0.25f, 0.5f, -0.5f, 0}, "new policy changed ordinary PCM admission/source samples");
	RequireFailure([&] {
		fixture.Bindings->ConfigureBufferedPlayback(fixture.Geometry(), verified, stream);
	},
	    "one actual live output");
}

static void TestAbsentProfileDoesNotInferWma(const std::filesystem::path& directory) {
	ActualScriptStreaming fixture(directory);
	fixture.Bindings->ConfigureBufferedPlayback(fixture.Geometry());
	const auto id = fixture.Play("return Sound.playVoice(\"common\", \"cue\");");
	const auto failed = fixture.Sound.BufferedCues().InspectChannel(id, 0);
	Require(!failed.State.Streaming && !failed.State.Samples.CallbackActive && failed.Error.find("explicitly verified options31") != std::string_view::npos && fixture.Output->SourceNodeCount() == 0, "absent measured profile synthesized WMA readiness from empty extension");
	fixture.RequireUnchangedWma();
	const auto pcm = fixture.Play("return Sound.playVoice(\"common\", \"other\");");
	Require(fixture.Sound.BufferedCues().InspectChannel(pcm, 0).Error.empty() && fixture.Output->SourceNodeCount() == 1, "absent WMA profile blocked ordinary PCM after explicit source failure");
}

static void TestCompiledStreamingAndStopAcknowledgement(const std::filesystem::path& directory) {
	ActualScriptStreaming fixture(directory);
	fixture.Bindings->ConfigureBufferedPlayback(fixture.Geometry(), sgre::script::SoundBufferedDecoderProfile::VerifiedWmaOptions31, sgre::script::SoundBufferedPlaybackMode::IncrementalWma);
	const auto id = fixture.Play("return Sound.playVoice(\"common\", \"cue\", play_options, play_count);");
	fixture.Select(id);
	const auto initial = fixture.Sound.BufferedCues().InspectChannel(id, 0);
	Require(initial.State.Streaming && initial.State.Samples.CallbackActive && initial.State.Samples.LoopRepeatsRemaining == 255 && (fixture.Sound.BufferedCues().Inspect(id).Preparation == BufferedSoundCuePreparationStatus::PendingSources || fixture.Sound.BufferedCues().Inspect(id).Preparation == BufferedSoundCuePreparationStatus::Prepared), "compiled VM call replaced real pending worker/source state or original play-count semantics");
	fixture.Await([&] {
		return !fixture.Sound.BufferedCues().InspectChannel(id, 0).PendingPreparation;
	});
	const auto source = fixture.Sound.BufferedCues().InspectChannel(id, 0).Source;
	Require(fixture.Output->SourceNodeCount() == 1 && fixture.Sound.BufferedCues().InspectChannel(id, 0).State.StreamingOutputAttached && fixture.GeometryReads == 1, "actual script worker prefill never reached the same typed offline source/matrix");
	std::array<float, 128> output{};
	const auto report = fixture.Output->RenderTyped(output);
	const auto consumed = fixture.Sound.BufferedCues().InspectSource(source);
	Require(report.MasterFrames == 64 && report.SourceCount == 1 && report.Sources[0].Streaming && report.Sources[0].ConsumedFrames != 0 && report.Sources[0].SamplesPerSecond == 44100 && report.Sources[0].ConsumedFrames == consumed.Samples.ConsumedFrames && !report.Sources[0].InputComplete && !consumed.NativeBufferContextPresent, "compiled cue lost actual source/master clocks, source samples or native null context");
	Require(std::get<std::int32_t>(fixture.Run("return Sound.pauseVoice(selected_cue);")) == static_cast<std::int32_t>(id.Value) && !fixture.Sound.BufferedCues().InspectSource(source).Samples.Running, "native pause physical return cell or real source running transition changed");
	Require(std::get<std::int32_t>(fixture.Run("return Sound.resumeVoice(selected_cue);")) == static_cast<std::int32_t>(id.Value), "native resume physical return cell changed");
	fixture.RequireUnchangedWma();
	const auto archive = *fixture.Cache.FindNamedResource("asset/common.psb");
	Require(std::get<std::int32_t>(fixture.Run("return Sound.stopVoice(selected_cue);")) == static_cast<std::int32_t>(id.Value) && !std::get<bool>(fixture.Run("return Sound.getVoicePlaying(selected_cue);")), "actual script stop retained active ID or altered original physical return");
	fixture.Await([&] {
		return fixture.Sound.BufferedCues().RetiringCount() == 0;
	});
	const auto retired = fixture.Sound.BufferedCues().InspectSource(source);
	Require(retired.Streaming->WorkerQuiescent && !retired.Streaming->InputBorrowActive && !retired.Samples.CallbackActive && retired.Samples.ConsumedFrames == consumed.Samples.ConsumedFrames && fixture.Output->SourceNodeCount() == 1, "script stop invented source progress or destroyed tail/history before genuine acknowledgement");
	static_cast<void>(fixture.Run("return Sound.unloadArchive(\"common\");"));
	fixture.Sound.AdvancePostLoop();
	Require(!fixture.Cache.Contains(archive), "actual script archive unload retained a released encoded borrower");
}

static void TestBindingExpirationRetainsActualSource(const std::filesystem::path& directory) {
	ActualScriptStreaming fixture(directory);
	fixture.Bindings->ConfigureBufferedPlayback(fixture.Geometry(), sgre::script::SoundBufferedDecoderProfile::VerifiedWmaOptions31, sgre::script::SoundBufferedPlaybackMode::IncrementalWma);
	const auto id = fixture.Play("return Sound.playVoice(\"common\", \"cue\", play_options, play_count);");
	fixture.Await([&] {
		return !fixture.Sound.BufferedCues().InspectChannel(id, 0).PendingPreparation;
	});
	const auto sound = fixture.Bindings->SoundClass();
	const auto play = fixture.Vm.GetSlot(sound, "playVoice");
	const auto retained = fixture.Vm.Retain(play);
	fixture.Bindings.reset();
	const std::array<sgre::script::SquirrelValue, 3> arguments{sound, "common", "cue"};
	RequireFailure([&] {
		static_cast<void>(fixture.Vm.Invoke(play, arguments));
	},
	    "actual live VM/resource owner");
	std::array<float, 128> output{};
	const auto report = fixture.Output->RenderTyped(output);
	Require(fixture.GeometryReads == 1 && fixture.Output->SourceNodeCount() == 1 && report.SourceCount == 1 && report.Sources[0].ConsumedFrames != 0 && fixture.Sound.BufferedCues().InspectChannel(id, 0).State.Streaming->InputBorrowActive, "binding retirement invalidated independently owned worker/source/archive data");
}

int main(int argc, char** argv) {
	try {
		Require(argc == 2, "compiled actual Sound streaming policy fixtures require ignored output directory");
		const std::filesystem::path directory(argv[1]);
		std::filesystem::create_directories(directory);
		TestValidationRollbackAndDefaultWholeBuffer(directory);
		TestAbsentProfileDoesNotInferWma(directory);
		TestCompiledStreamingAndStopAcknowledgement(directory);
		TestBindingExpirationRetainsActualSource(directory);
		std::cout << "Compiled Squirrel actual streaming/default/profile rollback, source clocks and lifetime fixtures passed; no hardware output\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
