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

class ActualSoundRuntime {
public:
	explicit ActualSoundRuntime(const std::filesystem::path& directory, bool wrapped = true) : Cache(0), Processor(Cache, Mounts, Background), Sound(Cache, Mounts, Processor, "win", "synthetic-seed", wrapped) {
		const std::array<CueVector, 1> vectors{CueVector{}};
		auto configuration = CatalogPsb();
		auto archive = CatalogPsb(vectors);
		if (wrapped) {
			configuration = Wrap(std::move(configuration), "asset/configuration.psb.m");
			archive = Wrap(std::move(archive), "asset/common.psb.m");
		}
		Bytes body = configuration;
		Append(body, archive);
		const auto body_path = directory / "sound-body.bin";
		static_cast<void>(Prepared(body_path, body, "unused-write-mapping"));
		const auto index_path = directory / "sound-index.bin";
		const auto index = Prepared(index_path, Wrap(IndexPsb(configuration.size(), archive.size()), "sound-index.psb.m"), "sound-index.psb.m");
		sgre::resource::ArchiveReadMountInput input;
		input.Prefix = "asset/";
		input.IndexIdentity = "sound-index.psb.m";
		input.BodyIdentity = "actual-sound-body";
		input.BodyFilename = body_path;
		input.BodyBytes = body.size();
		input.IndexRead.Region = index.Region;
		input.IndexRead.OriginalFilename = "sound-index.psb.m";
		input.IndexRead.Seed = "synthetic-seed";
		input.IndexRead.MaxDecodedBytes = 4096;
		input.IndexRead.MaxMetadataBytes = 4096;
		Processor.QueueReadMount(std::move(input));
		FinishReader(Processor, Background);
		Processor.Update();
		Processor.Update();
		Processor.Update();
		Require(Mounts.MountCount() == 1 && !Mounts.HasInFlightReadMount(), "actual mounted Sound body must pass receiving/publication gates");
	}

	~ActualSoundRuntime() noexcept {
		try {
			FinishReader(Processor, Background);
			Sound.AdvancePostLoop();
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

	void FinishSoundWork() {
		FinishReader(Processor, Background);
		Sound.AdvancePostLoop();
	}
};

static sgre::script::SquirrelValue Method(sgre::script::SquirrelVm& vm, const sgre::script::SquirrelValue& receiver, std::string name, std::vector<sgre::script::SquirrelValue> arguments = {}) {
	arguments.insert(arguments.begin(), receiver);

	return vm.Invoke(vm.GetSlot(receiver, std::move(name)), arguments).Value;
}

static void TestRealBindingResourceLoad(const std::filesystem::path& directory) {
	ActualSoundRuntime runtime(directory);
	sgre::script::SquirrelVm vm;
	sgre::script::ScriptSoundBindings bindings(vm, runtime.Sound);
	const auto sound = bindings.SoundClass();
	Require(vm.GetSlot(vm.RootTable(), "Sound") == sound && vm.ClassTypeTag(sound) != nullptr, "native standalone Sound class must publish its own real class tag");
	const auto other_class = vm.CreateClass();
	const auto instance = vm.Invoke(sound, std::array<sgre::script::SquirrelValue, 2>{vm.RootTable(), true}).Value;
	Require(vm.IsInstanceOf(instance, sound) && !vm.IsInstanceOf(instance, other_class) && runtime.Processor.PendingRawCount() == 0, "CopyOnly Sound() must allocate no host Sound/resource owner or derived Object instance");
	Require(std::holds_alternative<std::monostate>(Method(vm, sound, "load", {"asset/configuration.psb"})), "native Sound.load must return zero values after scheduling genuine work");
	const auto handle = *runtime.Cache.FindNamedResource("asset/configuration.psb");
	Require(std::get<bool>(Method(vm, sound, "getLoading", {"asset/configuration.psb"})) && !std::get<bool>(Method(vm, sound, "getLoaded", {"asset/configuration.psb"})) && runtime.Cache.Info(handle).References == 1 && runtime.Processor.PendingRawCount() == 1 && !runtime.Sound.CanClose(), "load must keep actual pending configuration/cache/task ownership separate from completion/audio readiness");
	RequireFailure([&] {
		runtime.Sound.Close();
	},
	    "pending serial/parallel borrower");
	static_cast<void>(Method(vm, sound, "load", {"asset/configuration.psb"}));
	Require(runtime.Cache.Info(handle).References == 1 && runtime.Processor.PendingRawCount() == 1, "duplicate native load must suppress repeated resource mapping/reference/work");
	runtime.FinishSoundWork();
	Require(std::get<bool>(Method(vm, sound, "getLoaded", {"asset/configuration.psb"})) && !std::get<bool>(Method(vm, sound, "getLoading", {"asset/configuration.psb"})) && runtime.Sound.ConfigurationFile("asset/configuration.psb").BackingStorage().size() == runtime.Sound.ConfigurationFile("asset/configuration.psb").Document().SerializedSize, "actual mounted MZS PSB completion must own strict complete backing");
	static_cast<void>(Method(vm, sound, "loadArchive", {"common"}));
	Require(std::get<bool>(Method(vm, sound, "getArchiveLoading", {"common"})) && !std::get<bool>(Method(vm, sound, "getArchiveLoaded", {"common"})), "native archive query must observe the real acquired pending owner");
	runtime.FinishSoundWork();
	Require(std::get<bool>(Method(vm, sound, "getArchiveLoaded", {"common"})) && runtime.Cache.Info(*runtime.Cache.FindNamedResource("asset/common.psb")).References == 1, "native archive completion must retain its original actual cache reference");
	static_cast<void>(Method(vm, sound, "unloadArchive", {"common"}));
	Require(!runtime.Cache.FindNamedResource("asset/common.psb"), "native archive unload must release an unborrowed actual archive");
	static_cast<void>(Method(vm, sound, "unload", {"asset/configuration.psb"}));
	runtime.Sound.AdvancePostLoop();
	Require(!runtime.Cache.Contains(handle) && !std::get<bool>(Method(vm, sound, "getLoaded", {"asset/configuration.psb"})), "native config unload must remove lookup and actually retire its resource reference");
}

static void TestActualResourceServiceCoalescing(const std::filesystem::path& directory) {
	ActualSoundRuntime runtime(directory);
	sgre::script::ScriptResourceFileService resource(runtime.Cache, runtime.Mounts, runtime.Processor, "synthetic-seed");
	sgre::script::SquirrelVm vm;
	sgre::script::ScriptSoundBindings bindings(vm, runtime.Sound);
	const auto sound = bindings.SoundClass();
	auto poll = resource.RequestService()("asset/configuration.psb", sgre::script::ScriptResourceRequestMode::PsbWrapped);
	const auto handle = *runtime.Cache.FindNamedResource("asset/configuration.psb");
	static_cast<void>(Method(vm, sound, "load", {"asset/configuration.psb"}));
	Require(runtime.Processor.PendingRawCount() == 1 && runtime.Cache.Info(handle).References == 2 && !poll(), "Resource and Sound must retain two actual pending references to one coalesced receiving cell/read");
	runtime.FinishSoundWork();
	auto asset = poll();
	Require(asset && asset->Error.empty() && asset->Document.get() == &runtime.Sound.ConfigurationFile("asset/configuration.psb").Document() && asset->Backing.get() == &runtime.Sound.ConfigurationFile("asset/configuration.psb").BackingStorage(), "Sound and Resource must borrow the same actually delivered document/backing identity");
	static_cast<void>(Method(vm, sound, "unload", {"asset/configuration.psb"}));
	runtime.Sound.AdvancePostLoop();
	Require(runtime.Cache.Contains(handle) && runtime.Cache.Info(handle).References == 1 && !resource.CanClose(), "Sound unload must return only its own reference while the actual Resource observer retains the cache generation");
	poll = {};
	asset.reset();
	Require(!runtime.Cache.Contains(handle) && resource.CanClose(), "last actual Resource lease must independently release the unchanged coalesced cache generation");
	resource.Close();
}

static void SetTableDelegate(sgre::script::SquirrelVm& vm, const sgre::script::SquirrelValue& table, const sgre::script::SquirrelValue& delegate) {
	sgre::script::BytecodeProgram program;
	program.MainFunction.Name = std::string("independent Sound path delegate fixture");
	program.MainFunction.Parameters = {std::string("table"), std::string("delegate")};
	program.MainFunction.StackSize = 3;
	sgre::script::SquirrelInstruction set_delegate;
	set_delegate.Opcode = 53;
	set_delegate.Argument0 = 2;
	set_delegate.Argument1 = 0;
	set_delegate.Argument2 = 1;
	sgre::script::SquirrelInstruction return_value;
	return_value.Opcode = 19;
	return_value.Argument0 = 1;
	return_value.Argument1 = 2;
	program.MainFunction.Instructions = {set_delegate, return_value};
	const std::array<sgre::script::SquirrelValue, 2> arguments{table, delegate};
	static_cast<void>(vm.Invoke(vm.CreateClosure(std::move(program)), arguments));
}

static void TestStringAdapterCaptureAndWeakref(const std::filesystem::path& directory) {
	ActualSoundRuntime runtime(directory, false);
	sgre::script::SquirrelVm vm;
	sgre::script::ScriptSoundBindings bindings(vm, runtime.Sound);
	const auto sound = bindings.SoundClass();
	const auto name = vm.CreateTable();
	const auto delegate = vm.CreateTable();
	vm.NewSlot(delegate, "_tostring", vm.CreateNativeClosure("actual Sound path conversion", [](auto&, auto) {
		return sgre::script::SquirrelNativeResult::ReturnValue("asset/configuration.psb");
	}));
	SetTableDelegate(vm, name, delegate);
	static_cast<void>(Method(vm, sound, "load", {name}));
	runtime.FinishSoundWork();
	const std::string embedded("asset/configuration.psb\0ignored", 30);
	Require(std::get<bool>(Method(vm, sound, "getLoaded", {embedded})), "native sq_tostring and strlen must retain exact prefix before embedded NUL");
	Require(!std::get<bool>(Method(vm, sound, "getLoaded")), "missing string argument must see the actual userdata capture rather than a retained inactive string cell");
	const auto instance = vm.Invoke(sound, std::array<sgre::script::SquirrelValue, 1>{vm.RootTable()}).Value;
	const auto extra = vm.CreateTable();
	const auto weak = Method(vm, instance, "weakref", {extra});
	Require(vm.ResolveWeakReference(weak) == extra, "own native Sound.weakref must use exact top-minus-one input rather than invented receiver semantics");
	static_cast<void>(Method(vm, sound, "load", {"missing-resource"}));
	const auto missing = *runtime.Cache.FindNamedResource("missing-resource");
	Require(runtime.Processor.PendingRawCount() == 1 && runtime.Cache.Info(missing).Completion == sgre::resource::ResourceCompletion::Pending, "native Sound.load must queue actual logical receiving work before selected mapping can fail");
	runtime.FinishSoundWork();
	Require(runtime.Cache.Info(missing).Completion == sgre::resource::ResourceCompletion::Failed && runtime.Cache.Info(missing).Error.find("loose-file/path fallback remains unrecovered") != std::string::npos && runtime.Sound.ConfigurationInfo("missing-resource").Diagnostic == sgre::audio::SoundConfigurationDiagnostic::FailedDelivery && std::get<bool>(Method(vm, sound, "getLoading", {"missing-resource"})) && !std::get<bool>(Method(vm, sound, "getLoaded", {"missing-resource"})), "unsupported selected mapping must preserve actual failure diagnostics and the unchanged native completion latch");
	static_cast<void>(Method(vm, sound, "unload", {"missing-resource"}));
	runtime.Sound.AdvancePostLoop();
	Require(runtime.Processor.PendingRawCount() == 0 && !runtime.Cache.Contains(missing), "actual failed source/cache/callback references must retire without a fabricated PSB");
}

static void TestNativePostloopAndActualCueBorrow(const std::filesystem::path& directory) {
	ActualSoundRuntime runtime(directory);
	sgre::script::SquirrelVm vm;
	sgre::script::ScriptSoundBindings bindings(vm, runtime.Sound);
	const auto sound = bindings.SoundClass();
	static_cast<void>(Method(vm, sound, "load", {"asset/configuration.psb"}));
	FinishReader(runtime.Processor, runtime.Background);
	bool observed_unlatched = false;
	auto& parallel = runtime.Sound.ParallelTasks();
	sgre::scene::ParallelTaskCallbacks callbacks;
	callbacks.Update = [&](sgre::scene::ParallelTaskHandle task) {
		observed_unlatched = !runtime.Sound.GetLoaded("asset/configuration.psb");
		parallel.SetStatus(task, sgre::scene::TaskStatus::Retire);
	};
	static_cast<void>(parallel.Append(std::move(callbacks)));
	runtime.Sound.AdvancePostLoop();
	Require(observed_unlatched && runtime.Sound.GetLoaded("asset/configuration.psb"), "actual Parallel callbacks must precede actual Serial load/completion callbacks");
	static_cast<void>(Method(vm, sound, "loadArchive", {"common"}));
	runtime.FinishSoundWork();
	sgre::audio::BufferedSoundCueRequest request;
	request.ArchiveName = "common";
	request.CueName = "cue";
	sgre::audio::SoundMixParameters parameters;
	const auto cue = runtime.Sound.BufferedCues().Prepare(request, parameters, 2);
	Require(cue.Cue && cue.Status == BufferedSoundCuePreparationStatus::Prepared, "real containing Sound owner must use actual archive-backed decoded cue services");
	std::array<float, 1> sample{};
	Require(runtime.Sound.BufferedCues().ConsumeChannelFrames(*cue.Cue, 0, sample) == 1 && sample[0] == 0.25f, "actual Sound cue must consume an independent real decoded sample");
	const auto archive_handle = *runtime.Cache.FindNamedResource("asset/common.psb");
	const auto source = runtime.Sound.BufferedCues().InspectChannel(*cue.Cue, 0).Source;
	static_cast<void>(Method(vm, sound, "unloadArchive", {"common"}));
	Require(runtime.Cache.Contains(archive_handle) && runtime.Sound.BufferedCues().InspectSource(source).Samples.CallbackActive, "native unload binding must retain actual retiring cue/archive borrowers before flush");
	runtime.Sound.AdvancePostLoop();
	Require(!runtime.Cache.Contains(archive_handle) && !runtime.Sound.BufferedCues().InspectSource(source).Samples.CallbackActive, "cue retirement must precede final archive/cache retirement in the same real postloop");
}

static void TestNamedBoundariesAndExpiredOwner(const std::filesystem::path& directory) {
	ActualSoundRuntime runtime(directory);
	sgre::script::SquirrelVm vm;
	sgre::script::SquirrelValue retained;
	{
		sgre::script::ScriptSoundBindings bindings(vm, runtime.Sound);
		const auto sound = bindings.SoundClass();
		Require(vm.FindRawSlot(sound, "constructor") && vm.FindRawSlot(sound, "weakref") && vm.FindRawSlot(sound, "playVoice") && vm.FindRawSlot(sound, "animateMasterVolume") && vm.FindRawSlot(sound, "connectResourceCache") && vm.FindRawSlot(sound, "disconnectResourceCache"), "standalone native Sound registry must preserve own/direct/captured/cache slots without invented constants");
		for (const auto name : {"animateMasterVolume", "pauseAll", "connectResourceCache"}) {
			RequireFailure([&] {
				static_cast<void>(Method(vm, sound, name));
			},
			    "unrecovered Sound operation: Sound.");
		}
		RequireFailure([&] {
			static_cast<void>(Method(vm, sound, "playVoice", {"tone"}));
		},
		    "actual containing buffered output geometry");
		Require(runtime.Processor.PendingRawCount() == 0, "unavailable native device/clock/focus operations must not turn into successful no-ops or fake resources");
		retained = vm.GetSlot(sound, "load");
	}
	const std::array<sgre::script::SquirrelValue, 2> arguments{vm.RootTable(), "asset/configuration.psb"};
	RequireFailure([&] {
		static_cast<void>(vm.Invoke(retained, arguments));
	},
	    "actual live VM/resource owner");
	Require(runtime.Processor.PendingRawCount() == 0, "expired registry closures must not retarget another live owner");
}

int main(int argc, char** argv) {
	try {
		Require(argc == 2, "Sound bindings fixture requires an ignored output directory");
		const std::filesystem::path directory(argv[1]);
		std::filesystem::create_directories(directory);
		TestRealBindingResourceLoad(directory);
		TestActualResourceServiceCoalescing(directory);
		TestStringAdapterCaptureAndWeakref(directory);
		TestNativePostloopAndActualCueBorrow(directory);
		TestNamedBoundariesAndExpiredOwner(directory);
		std::cout << "Sound resource bindings: 5 actual owner/VM fixture groups passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
