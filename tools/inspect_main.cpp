#include "sgre/app/display_path.hpp"
#include "sgre/io/file_reader.hpp"
#include "sgre/media/webm.hpp"
#include "sgre/platform/inspect_gui.hpp"
#include "sgre/resource/archive_index.hpp"
#include "sgre/resource/mzs_shell.hpp"
#include "sgre/resource/psb.hpp"
#include "sgre/resource/zstd_decoder.hpp"
#include "sgre/scenario/scenario_search.hpp"
#include "sgre/script/squirrel_bytecode.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

static constexpr std::size_t kMaxMetadataSize = 64 * 1024 * 1024;
static constexpr std::size_t kPsbHeaderSize = 56;

struct Options {
	std::string Command;
	std::filesystem::path Input;
	std::filesystem::path SeedFile;
	std::filesystem::path Body;
	std::filesystem::path Output;
	std::string Entry;
	std::string ResourceName;
	std::string Query;
	std::optional<std::filesystem::path> GameRoot;
	std::string Language = "en";
	std::size_t ResultLimit = 50;
	std::size_t Period = 131;
	bool List = false;
};

static std::size_t ReadSize(std::string_view value) {
	std::size_t number = 0;
	const auto result = std::from_chars(value.data(), value.data() + value.size(), number);
	if (result.ec != std::errc() || result.ptr != value.data() + value.size() || number == 0) {
		throw std::runtime_error("period must be a positive integer");
	}

	return number;
}

static Options ReadSearchOptions(int argc, char** argv) {
	if (argc < 3) {
		throw std::runtime_error("usage: sgre-inspect search QUERY [--game-root PATH] [--language en|ja|tc|sc] [--limit N]");
	}
	Options options;
	options.Command = "search";
	options.Query = argv[2];
	for (int index = 3; index < argc; ++index) {
		const std::string_view option = argv[index];
		if (index + 1 >= argc) {
			throw std::runtime_error("missing value for " + std::string(option));
		}
		const std::string_view value = argv[++index];
		if (option == "--game-root") {
			if (value.empty()) {
				throw std::runtime_error("search --game-root requires an installation path");
			}
			options.GameRoot = std::filesystem::path(std::u8string(value.begin(), value.end()));
		} else if (option == "--language") {
			options.Language = value;
		} else if (option == "--limit") {
			const auto parsed = std::from_chars(value.data(), value.data() + value.size(), options.ResultLimit);
			if (parsed.ec != std::errc() || parsed.ptr != value.data() + value.size() || options.ResultLimit == 0 || options.ResultLimit > 10000) {
				throw std::runtime_error("search --limit requires an integer from 1 to 10000");
			}
		} else {
			throw std::runtime_error("unknown search option: " + std::string(option));
		}
	}

	return options;
}

static Options ReadOptions(int argc, char** argv) {
	if (argc >= 2 && std::string_view(argv[1]) == "search") {
		return ReadSearchOptions(argc, argv);
	}

	if (argc < 3) {
		throw std::runtime_error("usage: sgre-inspect inspect|scan|script|movie|index|extract INPUT [--seed-file FILE] [--period N] [--body FILE] [--list] [--entry NAME --resource-name NAME --output FILE]");
	}

	Options options;
	options.Command = argv[1];
	options.Input = argv[2];
	if (options.Command != "inspect" && options.Command != "scan" && options.Command != "index" && options.Command != "extract" && options.Command != "script" && options.Command != "movie") {
		throw std::runtime_error("unknown command: " + options.Command);
	}

	for (int index = 3; index < argc; ++index) {
		const std::string_view option = argv[index];
		if (option == "--list") {
			options.List = true;
			continue;
		}

		if (index + 1 >= argc) {
			throw std::runtime_error("missing value for " + std::string(option));
		}

		const std::string_view value = argv[++index];
		if (option == "--seed-file") {
			options.SeedFile = value;
		} else if (option == "--body") {
			options.Body = value;
		} else if (option == "--period") {
			options.Period = ReadSize(value);
		} else if (option == "--entry") {
			options.Entry = value;
		} else if (option == "--resource-name") {
			options.ResourceName = value;
		} else if (option == "--output") {
			options.Output = value;
		} else {
			throw std::runtime_error("unknown option: " + std::string(option));
		}
	}

	const bool archive_command = options.Command == "index" || options.Command == "extract";
	if (!archive_command && (!options.Body.empty() || options.List || options.Period != 131 || (!options.SeedFile.empty() && options.Command != "movie"))) {
		throw std::runtime_error("archive options require index or extract; movie accepts --seed-file");
	}

	if (archive_command && options.Body.empty()) {
		throw std::runtime_error("archive commands require --body for range validation");
	}

	if (options.Command == "extract" && (options.Entry.empty() || options.ResourceName.empty() || options.Output.empty() || options.List)) {
		throw std::runtime_error("extract requires --entry, --resource-name, and --output; --list is only for index");
	}

	if (options.Command != "extract" && (!options.Entry.empty() || !options.ResourceName.empty() || !options.Output.empty())) {
		throw std::runtime_error("entry/output options require extract");
	}

	return options;
}

static sgre::resource::PsbDocument ReadDocument(sgre::io::FileReader& input, std::uint64_t offset) {
	const auto header_bytes = input.Read(offset, static_cast<std::size_t>(std::min<std::uint64_t>(kPsbHeaderSize, input.Size() - offset)));
	const auto header = sgre::resource::ReadPsbHeader(header_bytes);
	if (header.DataOffset > kMaxMetadataSize) {
		throw std::runtime_error("PSB metadata exceeds the 64 MiB inspection limit");
	}

	const auto metadata = input.Read(offset, header.DataOffset);

	return sgre::resource::ReadPsbMetadata(metadata, input.Size() - offset);
}

static void PrintSummary(const sgre::resource::PsbDocument& document) {
	std::cout << "PSB v" << document.Header.Version << ": " << document.SerializedSize << " bytes, " << document.Names.size() << " names, " << document.Strings.size() << " strings, " << document.Chunks.size() << " chunks, " << document.ExtraChunks.size() << " extra chunks";
	const auto* identity = document.Root.Find("id");
	if (identity != nullptr) {
		const auto* text = std::get_if<std::string>(&identity->Data);
		if (text != nullptr) {
			std::cout << ", id=" << *text;
		}
	}

	std::cout << '\n';
}

static std::size_t CountScriptFunctions(const sgre::script::SquirrelFunction& function) {
	std::size_t count = 1;
	for (const auto& child : function.Functions) {
		count += CountScriptFunctions(child);
	}

	return count;
}

static void InspectScript(const Options& options) {
	sgre::io::FileReader input(options.Input);
	if (input.Size() > kMaxMetadataSize) {
		throw std::runtime_error("script exceeds the 64 MiB inspection limit");
	}

	const auto bytes = input.Read(0, static_cast<std::size_t>(input.Size()));
	const auto program = sgre::script::ReadSquirrelBytecode(bytes);
	const auto& function = program.MainFunction;
	std::cout << "Squirrel 2.2.4 stream: " << program.SerializedSize << " bytes, " << CountScriptFunctions(function) << " function prototypes, " << function.Instructions.size() << " root instructions, " << function.Literals.size() << " root literals\n";
}

static void InspectPsb(const Options& options) {
	sgre::io::FileReader input(options.Input);
	const auto document = ReadDocument(input, 0);
	PrintSummary(document);
	std::cout << "Remaining file bytes: " << input.Size() - document.SerializedSize << '\n';
}

static void ScanPsb(const Options& options) {
	sgre::io::FileReader input(options.Input);
	std::uint64_t offset = 0;
	std::size_t object_count = 0;
	std::size_t chunk_count = 0;
	while (offset < input.Size()) {
		try {
			const auto document = ReadDocument(input, offset);
			if (object_count == 0) {
				PrintSummary(document);
			}

			if (document.SerializedSize == 0 || document.SerializedSize > input.Size() - offset) {
				throw std::runtime_error("PSB object has an invalid extent");
			}

			if (document.Chunks.size() > std::numeric_limits<std::size_t>::max() - chunk_count) {
				throw std::runtime_error("chunk count overflow");
			}

			chunk_count += document.Chunks.size();
			offset += document.SerializedSize;
			++object_count;
		} catch (const std::exception& error) {
			throw std::runtime_error("PSB at file offset " + std::to_string(offset) + ": " + error.what());
		}
	}

	std::cout << "Scanned " << object_count << " objects and " << chunk_count << " chunks across " << offset << " bytes without loading chunk payloads\n";
}

static std::string ReadSeed(const std::filesystem::path& path) {
	if (path.empty()) {
		throw std::runtime_error("index requires --seed-file containing the recovered archive seed");
	}

	sgre::io::FileReader input(path);
	if (input.Size() == 0 || input.Size() > 1024) {
		throw std::runtime_error("seed file must contain 1 to 1024 bytes");
	}

	const auto bytes = input.Read(0, static_cast<std::size_t>(input.Size()));
	std::string seed(bytes.begin(), bytes.end());
	while (!seed.empty() && (seed.back() == '\n' || seed.back() == '\r')) {
		seed.pop_back();
	}

	if (seed.empty() || seed.find('\0') != std::string::npos || seed.find('\n') != std::string::npos || seed.find('\r') != std::string::npos) {
		throw std::runtime_error("seed must be a single nonempty line");
	}

	return seed;
}

static void InspectMovie(const Options& options) {
	const std::string seed = options.SeedFile.empty() ? std::string{} : ReadSeed(options.SeedFile);
	auto reader = options.SeedFile.empty() ? sgre::media::WebmReader(options.Input) : sgre::media::WebmReader(options.Input, seed);
	for (const auto& track : reader.Tracks()) {
		std::cout << "Track " << track.Number << ": " << track.Codec << ", " << track.Width << "x" << track.Height << ", channels=" << track.Channels << ", sample_rate=" << track.SamplingFrequency << ", alpha=" << track.Alpha << '\n';
	}

	std::size_t packets = 0;
	std::size_t frames = 0;
	std::uint64_t payload_bytes = 0;
	std::int64_t first_time = 0;
	std::int64_t last_time = 0;
	while (const auto packet = reader.NextPacket()) {
		if (packets == 0) {
			first_time = packet->TimestampNanoseconds;
		}

		last_time = packet->TimestampNanoseconds;
		++packets;
		frames += packet->Frames.size();
		for (const auto& frame : packet->Frames) {
			payload_bytes += frame.size();
		}
	}

	std::cout << "WebM: " << packets << " packets, " << frames << " frames, " << payload_bytes << " payload bytes, timestamp range " << first_time << ".." << last_time << " ns, duration " << reader.DurationNanoseconds() << " ns\n";
}

static sgre::resource::ArchiveIndex LoadIndex(const Options& options, std::string_view seed, std::uint64_t body_size) {
	sgre::io::FileReader input(options.Input);
	if (input.Size() > kMaxMetadataSize) {
		throw std::runtime_error("index exceeds the 64 MiB inspection limit");
	}

	const auto bytes = input.Read(0, static_cast<std::size_t>(input.Size()));
	const auto shell = sgre::resource::DecodeMzsShell(bytes, options.Input.filename().string(), seed, options.Period, kMaxMetadataSize);
	const auto decoded = sgre::resource::DecodeZstd(shell.CompressedBytes, shell.DecodedSize, kMaxMetadataSize);
	const auto header = sgre::resource::ReadPsbHeader(decoded);
	if (header.DataOffset > decoded.size()) {
		throw std::runtime_error("decoded PSB metadata exceeds decoded bytes");
	}

	const auto document = sgre::resource::ReadPsbMetadata(std::span<const std::uint8_t>(decoded).first(header.DataOffset), decoded.size());
	if (document.SerializedSize != decoded.size()) {
		throw std::runtime_error("decoded index contains unexplained trailing data");
	}

	return sgre::resource::ReadArchiveIndex(document, body_size);
}

static void InspectIndex(const Options& options) {
	sgre::io::FileReader body(options.Body);
	const auto seed = ReadSeed(options.SeedFile);
	const auto index = LoadIndex(options, seed, body.Size());
	for (const auto& entry : index.Entries) {
		if (options.List) {
			std::cout << entry.Name << '\t' << entry.Offset << '\t' << entry.Size << '\n';
		}
	}

	std::cout << "Archive entries: " << index.Entries.size() << ", extent: " << index.SerializedExtent << " bytes, paired body: " << body.Size() << " bytes\n";
	std::cout << "Expired suffixes:";
	for (const auto& suffix : index.ExpireSuffixes) {
		std::cout << ' ' << suffix;
	}

	std::cout << '\n';
}

static void WriteResource(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
	if (std::filesystem::exists(path)) {
		throw std::runtime_error("output already exists; choose a new path");
	}

	if (!path.parent_path().empty()) {
		std::filesystem::create_directories(path.parent_path());
	}

	std::ofstream output(path, std::ios::binary);
	output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	output.close();
	if (!output) {
		throw std::runtime_error("cannot write complete decoded resource");
	}
}

static void ExtractEntry(const Options& options) {
	sgre::io::FileReader body(options.Body);
	const auto seed = ReadSeed(options.SeedFile);
	const auto index = LoadIndex(options, seed, body.Size());
	const auto entry = std::find_if(index.Entries.begin(), index.Entries.end(), [&](const sgre::resource::ArchiveEntry& candidate) {
		return candidate.Name == options.Entry;
	});
	if (entry == index.Entries.end()) {
		throw std::runtime_error("entry was not found in the archive index");
	}

	if (entry->Size > kMaxMetadataSize) {
		throw std::runtime_error("entry exceeds the 64 MiB inspection limit");
	}

	const auto packed = body.Read(entry->Offset, static_cast<std::size_t>(entry->Size));
	if (packed.size() >= 4 && packed[0] == 'm' && packed[1] == 'z' && packed[2] == 's' && packed[3] == 0) {
		const auto shell = sgre::resource::DecodeMzsShell(packed, options.ResourceName, seed, options.Period, kMaxMetadataSize);
		const auto decoded = sgre::resource::DecodeZstd(shell.CompressedBytes, shell.DecodedSize, kMaxMetadataSize);
		WriteResource(options.Output, decoded);
		std::cout << "Decoded " << decoded.size() << " bytes from " << entry->Size << " packed bytes\n";
	} else if (packed.size() >= 4 && packed[0] == 'P' && packed[1] == 'S' && packed[2] == 'B' && packed[3] == 0) {
		const auto header = sgre::resource::ReadPsbHeader(packed);
		if (header.DataOffset > packed.size()) {
			throw std::runtime_error("entry metadata exceeds entry bytes");
		}

		const auto document = sgre::resource::ReadPsbMetadata(std::span<const std::uint8_t>(packed).first(header.DataOffset), packed.size());
		if (document.SerializedSize != packed.size()) {
			throw std::runtime_error("PSB entry contains unexplained trailing bytes");
		}

		WriteResource(options.Output, packed);
		std::cout << "Copied validated PSB entry: " << packed.size() << " bytes\n";
	} else {
		throw std::runtime_error("entry uses an unsupported shell or signature");
	}
}

static std::string DisplayText(std::string_view text) {
	static constexpr std::string_view kHex = "0123456789abcdef";
	std::string result;
	for (const auto character : text) {
		if (character == '\n') {
			result += "\\n";
		} else if (character == '\r') {
			result += "\\r";
		} else if (character == '\t') {
			result += "\\t";
		} else if (character == '\\') {
			result += "\\\\";
		} else if (static_cast<unsigned char>(character) < 32 || character == 127) {
			const auto byte = static_cast<unsigned char>(character);
			result += "\\x";
			result += kHex[byte >> 4];
			result += kHex[byte & 15];
		} else {
			result += character;
		}
	}

	return result;
}

static std::string ShellArgument(std::string_view value) {
	std::string result = "'";
	for (const auto character : value) {
		if (character == '\'') {
			result += "'\\''";
		} else {
			result += character;
		}
	}
	result += '\'';

	return result;
}

static std::filesystem::path InspectorHome() {
	const char* value = std::getenv("HOME");
	if (value == nullptr) {
		return {};
	}
	const std::string_view home(value);

	return std::filesystem::path(std::u8string(home.begin(), home.end()));
}

static void SearchScenarios(const Options& options) {
	auto location = sgre::scenario::ResolveScenarioSearchLocation(std::filesystem::current_path(), options.GameRoot);
	std::string game_command = "sgre-game";
	if (options.GameRoot) {
		game_command += " " + sgre::app::QuotePosixUserPathArgument(location.Installation, InspectorHome());
	}
	sgre::scenario::ScenarioSearchCatalog catalog(std::move(location));
	sgre::scenario::ScenarioSearchRequest request;
	request.Query = options.Query;
	request.Language = options.Language;
	request.Limit = options.ResultLimit;
	const auto results = catalog.Search(request);
	std::cout << results.Matches.size() << (results.Matches.size() == 1 ? " match" : " matches") << (results.Truncated ? " (more matches available; increase --limit)" : "") << '\n';
	for (std::size_t index = 0; index < results.Matches.size(); ++index) {
		const auto& match = results.Matches[index];
		std::cout << '\n';
		std::cout << index + 1 << ". " << DisplayText(match.Storage) << ':' << match.SourceLine << "  scene=" << DisplayText(match.SceneLabel) << "  character=" << DisplayText(match.Character) << '\n';
		std::cout << "   original: " << DisplayText(match.OriginalText) << '\n';
		std::cout << "   " << DisplayText(match.Language) << ": " << DisplayText(match.Translation) << '\n';
		if (match.TranslatedSourceLine) {
			std::cout << "   translated source line: " << *match.TranslatedSourceLine << '\n';
		}
		std::cout << "   " << game_command << " --jump " << ShellArgument(match.JumpTarget) << '\n';
	}
}

static int RunGui(int argc, char** argv) {
#ifdef SGRE_HAS_INSPECT_GUI
	sgre::platform::InspectGuiOptions options;
	for (int index = 2; index < argc; ++index) {
		const std::string_view option = argv[index];
		if (option != "--game-root" || options.Installation || index + 1 >= argc) {
			throw std::runtime_error("usage: sgre-inspect --gui [--game-root PATH]");
		}

		const std::string_view value = argv[++index];
		if (value.empty() || value.starts_with("--")) {
			throw std::runtime_error("--game-root requires an installation path");
		}
		options.Installation = std::filesystem::path(std::u8string(value.begin(), value.end()));
	}

	return sgre::platform::RunInspectGui(options);
#else
	(void)argc;
	(void)argv;
	throw std::runtime_error("the inspector GUI is currently available on macOS");
#endif
}

int main(int argc, char** argv) {
	try {
		if (argc >= 2 && std::string_view(argv[1]) == "--gui") {
			return RunGui(argc, argv);
		}

		const auto options = ReadOptions(argc, argv);
		if (options.Command == "search") {
			SearchScenarios(options);
		} else if (options.Command == "inspect") {
			InspectPsb(options);
		} else if (options.Command == "scan") {
			ScanPsb(options);
		} else if (options.Command == "script") {
			InspectScript(options);
		} else if (options.Command == "movie") {
			InspectMovie(options);
		} else if (options.Command == "index") {
			InspectIndex(options);
		} else {
			ExtractEntry(options);
		}

		return 0;
	} catch (const std::exception& error) {
		std::cerr << "sgre-inspect: " << sgre::app::RedactUserHome(error.what(), InspectorHome()) << '\n';
		return 1;
	}
}
