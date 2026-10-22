#include "sgre/scenario/scenario_search.hpp"

#include "sgre/app/launch_configuration.hpp"
#include "sgre/io/file_reader.hpp"
#include "sgre/resource/archive_reader.hpp"

#include <algorithm>
#include <array>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <utility>
#include <variant>

namespace sgre::scenario {

	static void Require(bool condition, std::string_view reason) {
		if (!condition) {
			throw std::runtime_error(std::string(reason));
		}
	}

	static void Consume(std::size_t& used, std::size_t amount, std::size_t limit, std::string_view reason) {
		Require(used <= limit && amount <= limit - used, reason);
		used += amount;
	}

	static void RequireLimits(const ScenarioSearchLimits& limits) {
		Require(limits.MaxEntries > 0 && limits.MaxEntries <= 65536 && limits.MaxScenes > 0 && limits.MaxScenes <= 1000000 && limits.MaxRecords > 0 && limits.MaxRecords <= 16000000, "scenario search entry/scene/record limits exceed their supported bounds");
		Require(limits.MaxDecodedEntryBytes > 0 && limits.MaxDecodedEntryBytes <= 256 * 1024 * 1024 && limits.MaxDecodedTotalBytes > 0 && limits.MaxDecodedTotalBytes <= 1024 * 1024 * 1024, "scenario search decoded byte limits exceed their supported bounds");
		Require(limits.MaxTextBytes > 0 && limits.MaxTextBytes <= 16 * 1024 * 1024 && limits.MaxQueryBytes > 0 && limits.MaxQueryBytes <= 65536 && limits.MaxResults > 0 && limits.MaxResults <= 100000 && limits.MaxResultBytes > 0 && limits.MaxResultBytes <= 64 * 1024 * 1024, "scenario search text/query/result limits exceed their supported bounds");
	}

	static std::string_view Text(const resource::PsbValue* value, std::size_t limit, std::string_view reason, bool nullable = false) {
		if (nullable && (value == nullptr || std::holds_alternative<std::monostate>(value->Data))) {
			return {};
		}
		Require(value != nullptr, reason);
		const auto* text = std::get_if<std::string>(&value->Data);
		Require(text != nullptr && text->size() <= limit, reason);

		return *text;
	}

	static const resource::PsbArray& Array(const resource::PsbValue* value, std::string_view reason) {
		Require(value != nullptr, reason);
		const auto* array = std::get_if<resource::PsbArray>(&value->Data);
		Require(array != nullptr, reason);

		return *array;
	}

	static std::int32_t Line(const resource::PsbValue* value, std::string_view reason) {
		Require(value != nullptr, reason);
		const auto* integer = std::get_if<std::int64_t>(&value->Data);
		Require(integer != nullptr && *integer >= 0 && *integer <= std::numeric_limits<std::int32_t>::max(), reason);

		return static_cast<std::int32_t>(*integer);
	}

	static std::string LowerAscii(std::string_view value) {
		std::string result(value);
		for (auto& character : result) {
			if (character >= 'A' && character <= 'Z') {
				character = static_cast<char>(character - 'A' + 'a');
			}
		}

		return result;
	}

	static void RequireStorage(std::string_view storage) {
		Require(storage.size() > 3 && storage.size() <= 4096 && storage.ends_with(".ks") && storage.find_first_of("/\\:") == std::string_view::npos && storage != ".ks" && storage.find("..") == std::string_view::npos, "scenario jump requires an exact bounded .ks basename");
		Require(std::none_of(storage.begin(), storage.end(), [](char value) {
			return static_cast<unsigned char>(value) < 32 || value == 127;
		}),
		    "scenario jump storage contains a control byte");
	}

	struct ScenarioSnapshot {
		std::int32_t SourceLine = 0;
		std::int32_t SearchLine = 0;
		std::int32_t Point = 0;
		std::size_t Index = 0;
	};

	struct ScenarioScene {
		const resource::PsbValue* Value = nullptr;
		std::string_view Label;
		std::int32_t FirstLine = 0;
		std::vector<ScenarioSnapshot> Snapshots;
	};

	struct ScenarioMetadata {
		std::string_view Storage;
		std::vector<std::string_view> Languages;
		std::vector<ScenarioScene> Scenes;
	};

	struct SearchBudget {
		const ScenarioSearchLimits& Limits;
		std::size_t Scenes = 0;
		std::size_t Records = 0;
		std::size_t ComparedBytes = 0;
		std::size_t ResultBytes = 0;
		std::size_t DecodedBytes = 0;
	};

	static ScenarioMetadata ReadMetadata(const resource::PsbDocument& document, SearchBudget& budget) {
		ScenarioMetadata result;
		result.Storage = Text(document.Root.Find("name"), 4096, "scenario metadata requires its exact storage name");
		RequireStorage(result.Storage);
		const auto& languages = Array(document.Root.Find("languages"), "scenario metadata languages is not an array");
		Require(languages.size() <= 32, "scenario language count exceeds its supported bound");
		result.Languages.emplace_back("ja");
		for (const auto& value : languages) {
			const auto language = Text(&value, 32, "scenario language identity is not a bounded string");
			Require(!language.empty() && std::find(result.Languages.begin(), result.Languages.end(), language) == result.Languages.end(), "scenario language identities are empty or duplicated");
			result.Languages.push_back(language);
		}
		const auto& scenes = Array(document.Root.Find("scenes"), "scenario metadata scenes is not an array");
		Consume(budget.Scenes, scenes.size(), budget.Limits.MaxScenes, "scenario search exceeded its scene limit");
		for (const auto& value : scenes) {
			const auto* version = value.Find("version");
			Require(version != nullptr && ((std::get_if<double>(&version->Data) != nullptr && std::get<double>(version->Data) == 1.0) || (std::get_if<std::int64_t>(&version->Data) != nullptr && std::get<std::int64_t>(version->Data) == 1)), "scenario search requires the recovered version-one text schema");
			ScenarioScene scene;
			scene.Value = &value;
			scene.Label = Text(value.Find("label"), budget.Limits.MaxTextBytes, "scenario scene label is not a bounded string");
			scene.FirstLine = Line(value.Find("firstLine"), "scenario scene firstLine is not a nonnegative source line");
			Require(result.Scenes.empty() || scene.FirstLine >= result.Scenes.back().FirstLine, "scenario scene firstLine values are out of source order");
			const auto& lines = Array(value.Find("lines"), "scenario scene lines is not an array");
			Consume(budget.Records, lines.size(), budget.Limits.MaxRecords, "scenario search exceeded its record limit");
			for (std::size_t index = 0; index < lines.size(); ++index) {
				const auto* row = std::get_if<resource::PsbArray>(&lines[index].Data);
				if (row == nullptr || row->empty() || !std::holds_alternative<std::int64_t>(row->front().Data)) {
					continue;
				}
				Require(row->size() >= 5, "scenario position record lacks its original source line");
				ScenarioSnapshot snapshot;
				snapshot.SourceLine = Line(&(*row)[4], "scenario position record has an invalid original source line");
				snapshot.SearchLine = scene.Snapshots.empty() ? snapshot.SourceLine : std::max(snapshot.SourceLine, scene.Snapshots.back().SearchLine);
				snapshot.Point = Line(&row->front(), "scenario position record has an invalid original point");
				snapshot.Index = index;
				scene.Snapshots.push_back(snapshot);
			}
			result.Scenes.push_back(std::move(scene));
		}

		return result;
	}

	static const ScenarioScene& FindScene(const ScenarioMetadata& metadata, std::int32_t line) {
		Require(!metadata.Scenes.empty(), "scenario jump storage has no original scenes");
		for (std::size_t index = 0; index + 1 < metadata.Scenes.size(); ++index) {
			if (line < metadata.Scenes[index + 1].FirstLine) {
				return metadata.Scenes[index];
			}
		}

		return metadata.Scenes.back();
	}

	static ScenarioJumpPoint JumpPoint(const ScenarioMetadata& metadata, std::string_view supplied_storage, std::int32_t line) {
		Require(line >= 0, "scenario jump requires a nonnegative source line");
		const auto& scene = FindScene(metadata, line);
		// Prefix maxima preserve the original linear first-snapshot>=line choice even for repeated/decreasing records.
		const auto snapshot = std::lower_bound(scene.Snapshots.begin(), scene.Snapshots.end(), line, [](const ScenarioSnapshot& value, std::int32_t requested) {
			return value.SearchLine < requested;
		});
		Require(snapshot != scene.Snapshots.end(), "scenario jump source line has no following original snapshot in its selected scene");
		ScenarioJumpPoint result;
		result.Storage = supplied_storage;
		result.CanonicalStorage = metadata.Storage;
		result.SceneLabel = scene.Label;
		result.SourceLine = line;
		result.SnapshotLine = snapshot->SourceLine;
		result.SnapshotIndex = snapshot->Index;
		result.Point = snapshot->Point;

		return result;
	}

	class QueryMatcher {
	public:
		explicit QueryMatcher(std::string_view query) : Query(query), Prefix(query.size()) {
			for (std::size_t index = 1, matched = 0; index < Query.size(); ++index) {
				while (matched != 0 && Query[index] != Query[matched]) {
					matched = Prefix[matched - 1];
				}
				if (Query[index] == Query[matched]) {
					++matched;
				}
				Prefix[index] = matched;
			}
		}

		bool Contains(std::string_view text, SearchBudget& budget) const {
			Consume(budget.ComparedBytes, text.size(), budget.Limits.MaxDecodedTotalBytes, "scenario search exceeded its compared-text byte limit");
			if (Query.empty()) {
				return true;
			}
			for (std::size_t index = 0, matched = 0; index < text.size(); ++index) {
				while (matched != 0 && text[index] != Query[matched]) {
					matched = Prefix[matched - 1];
				}
				if (text[index] == Query[matched]) {
					++matched;
				}
				if (matched == Query.size()) {
					return true;
				}
			}

			return false;
		}

	private:
		std::string_view Query;
		std::vector<std::size_t> Prefix;
	};

	static void RequireRequest(const ScenarioSearchRequest& request, const ScenarioSearchLimits& limits) {
		RequireLimits(limits);
		Require(request.Query.size() <= limits.MaxQueryBytes && request.Query.find('\0') == std::string::npos, "scenario search query exceeds its limit or contains NUL");
		Require(!request.Language.empty() && request.Language.size() <= 32 && request.Language.find('\0') == std::string::npos, "scenario search language must be an exact bounded identity");
		Require(request.Limit > 0 && request.Limit <= limits.MaxResults, "scenario search result limit exceeds its supported bound");
	}

	static bool Cancelled(const ScenarioSearchRequest& request, ScenarioSearchResults& result) {
		if (request.Cancelled && request.Cancelled()) {
			result.Cancelled = true;
			return true;
		}

		return false;
	}

	static std::optional<std::int32_t> TranslatedLine(const resource::PsbDocument& document, std::size_t language, std::string_view label, std::size_t text_index) {
		const auto* mapping = document.Root.Find("llmap");
		if (language == 0 || mapping == nullptr) {
			return {};
		}
		const auto& languages = Array(mapping, "scenario translated line map is not an array");
		if (language - 1 >= languages.size()) {
			return {};
		}
		const auto* scene = languages[language - 1].Find(label);
		if (scene == nullptr) {
			return {};
		}
		const auto& lines = Array(scene, "scenario translated scene line map is not an array");
		if (text_index >= lines.size() || std::holds_alternative<std::monostate>(lines[text_index].Data)) {
			return {};
		}

		return Line(&lines[text_index], "scenario translated source line is not a nonnegative integer");
	}

	static bool SearchDocument(const resource::PsbDocument& document, const ScenarioSearchRequest& request, const QueryMatcher& matcher, SearchBudget& budget, ScenarioSearchResults& result) {
		if (document.Root.Find("scenes") == nullptr) {
			return true;
		}
		const auto metadata = ReadMetadata(document, budget);
		const auto language = std::find(metadata.Languages.begin(), metadata.Languages.end(), request.Language);
		Require(language != metadata.Languages.end(), "scenario language is not declared by this resource: " + request.Language);
		const auto language_index = static_cast<std::size_t>(language - metadata.Languages.begin());
		for (std::size_t scene_index = 0; scene_index < metadata.Scenes.size(); ++scene_index) {
			const auto& scene = metadata.Scenes[scene_index];
			const auto* value = scene.Value->Find("texts");
			const resource::PsbArray empty;
			const auto& texts = value == nullptr ? empty : Array(value, "scenario scene texts is not an array");
			const auto& lines = Array(scene.Value->Find("lines"), "scenario scene lines is not an array");
			std::optional<std::int32_t> current_line;
			for (const auto& line : lines) {
				if (Cancelled(request, result)) {
					return false;
				}
				if (const auto* row = std::get_if<resource::PsbArray>(&line.Data); row != nullptr && !row->empty() && std::holds_alternative<std::int64_t>(row->front().Data)) {
					current_line = Line(&(*row)[4], "scenario snapshot has no valid source line");
					continue;
				}
				const auto* reference = std::get_if<std::int64_t>(&line.Data);
				if (reference == nullptr) {
					continue;
				}
				Consume(budget.Records, 1, budget.Limits.MaxRecords, "scenario search exceeded its text record limit");
				++result.TextRowsRead;
				Require(*reference > 0 && static_cast<std::uint64_t>(*reference) <= texts.size(), "scenario text reference is outside its original one-based text array");
				Require(current_line.has_value(), "scenario text has no preceding original position record");
				const auto text_index = static_cast<std::size_t>(*reference - 1);
				const auto& text = Array(&texts[text_index], "scenario text record is not an array");
				Require(text.size() >= 5, "scenario text record is not the recovered version-one tuple");
				const auto base_name = Text(&text[0], budget.Limits.MaxTextBytes, "scenario character is not a bounded string", true);
				const auto& variants = Array(&text[1], "scenario text language variants is not an array");
				Require(variants.size() == 1 || variants.size() == metadata.Languages.size(), "scenario text variants differ from the recovered singleton or declared language count");
				// Original convertSceneText publishes no language array for a singleton: its base fields remain selected.
				const auto selected_language = variants.size() == 1 ? std::size_t{0} : language_index;
				const auto& original = Array(&variants[0], "scenario original text variant is not an array");
				const auto& translated = Array(&variants[selected_language], "scenario selected text variant is not an array");
				Require(original.size() >= 2 && translated.size() >= 2, "scenario text variant lacks name/text fields");
				const auto original_text = Text(&original[1], budget.Limits.MaxTextBytes, "scenario original text is not a bounded string", true);
				const auto translated_text = Text(&translated[1], budget.Limits.MaxTextBytes, "scenario translation is not a bounded string", true);
				const auto display_name = Text(&translated[0], budget.Limits.MaxTextBytes, "scenario translated character is not a bounded string", true);
				const auto character = display_name.empty() ? base_name : display_name;
				if (!matcher.Contains(original_text, budget) && !matcher.Contains(translated_text, budget) && !matcher.Contains(character, budget) && !matcher.Contains(metadata.Storage, budget) && !matcher.Contains(scene.Label, budget)) {
					continue;
				}
				if (result.Matches.size() == request.Limit) {
					result.Truncated = true;
					return false;
				}
				// Boundary snapshots can share the next scene's firstLine; select through the containing scene's interval.
				auto target_line = std::max(*current_line, scene.FirstLine);
				if (scene_index + 1 < metadata.Scenes.size()) {
					target_line = std::min(target_line, metadata.Scenes[scene_index + 1].FirstLine - 1);
				}
				const auto jump = JumpPoint(metadata, metadata.Storage, target_line);
				Require(jump.SceneLabel == scene.Label, "scenario text containing scene has no selectable source-line interval");
				const auto jump_line = std::to_string(target_line);
				for (const auto field : std::array<std::string_view, 6>{metadata.Storage, scene.Label, character, original_text, translated_text, request.Language}) {
					Consume(budget.ResultBytes, field.size(), budget.Limits.MaxResultBytes, "scenario search exceeded its owned result byte limit");
				}
				Consume(budget.ResultBytes, metadata.Storage.size() + 1 + jump_line.size(), budget.Limits.MaxResultBytes, "scenario search exceeded its owned result byte limit");
				ScenarioSearchMatch match;
				match.Storage = metadata.Storage;
				match.SceneLabel = scene.Label;
				match.SourceLine = *current_line;
				match.TranslatedSourceLine = TranslatedLine(document, selected_language, scene.Label, text_index);
				match.Character = character;
				match.OriginalText = original_text;
				match.Translation = translated_text;
				match.Language = request.Language;
				match.TextId = text_index + 1;
				match.JumpLine = target_line;
				match.SnapshotLine = jump.SnapshotLine;
				match.JumpTarget = match.Storage + ":" + jump_line;
				result.Matches.push_back(std::move(match));
			}
		}

		return true;
	}

	ScenarioSearchResults SearchScenarioDocument(const resource::PsbDocument& document, const ScenarioSearchRequest& request, ScenarioSearchLimits limits) {
		RequireRequest(request, limits);
		ScenarioSearchResults result;
		if (!Cancelled(request, result)) {
			SearchBudget budget{limits};
			const QueryMatcher matcher(request.Query);
			result.EntriesRead = 1;
			static_cast<void>(SearchDocument(document, request, matcher, budget, result));
		}

		return result;
	}

	ScenarioJumpPoint ValidateScenarioJump(const resource::PsbDocument& document, std::string_view storage, std::int32_t target_line, ScenarioSearchLimits limits) {
		RequireLimits(limits);
		RequireStorage(storage);
		Require(target_line >= 0, "scenario jump requires a nonnegative source line");
		SearchBudget budget{limits};
		const auto metadata = ReadMetadata(document, budget);
		Require(LowerAscii(storage) == LowerAscii(metadata.Storage), "scenario jump storage differs from its actual parsed resource identity");

		return JumpPoint(metadata, storage, target_line);
	}

	std::vector<ScenarioTestScene> ReadScenarioTestDocument(const resource::PsbDocument& document, ScenarioSearchLimits limits) {
		RequireLimits(limits);
		if (document.Root.Find("scenes") == nullptr) {
			return {};
		}
		SearchBudget budget{limits};
		const auto metadata = ReadMetadata(document, budget);
		ScenarioNavigation navigation;
		navigation.AddDocument(document);
		std::vector<ScenarioTestScene> result;
		for (std::size_t index = 0; index < metadata.Scenes.size(); ++index) {
			const auto& source = metadata.Scenes[index];
			ScenarioTestScene scene;
			scene.Storage = metadata.Storage;
			scene.Label = source.Label;
			scene.Exits = navigation.Scenes().at(index).Exits;
			if (const auto* nexts = source.Value->Find("nexts")) {
				for (const auto& exit : Array(nexts, "scenario test nexts requires an array")) {
					if (Line(exit.Find("type"), "scenario test exit type is invalid") == 1) {
						++scene.TerminalExits;
					}
				}
			}
			// Invert the original first-snapshot>=line selection in linear time. Repeated
			// and decreasing lines remain inventory records, never invented jump targets.
			std::int64_t minimum = index == 0 ? 0 : source.FirstLine;
			const auto maximum = index + 1 < metadata.Scenes.size() ? std::int64_t(metadata.Scenes[index + 1].FirstLine) - 1 : std::int64_t(std::numeric_limits<std::int32_t>::max());
			for (const auto& snapshot : source.Snapshots) {
				ScenarioTestSnapshot target;
				target.Location = {scene.Storage, scene.Label, snapshot.SourceLine, snapshot.Point, static_cast<std::int32_t>(snapshot.Index)};
				const auto upper = std::min<std::int64_t>(snapshot.SourceLine, maximum);
				if (minimum <= upper) {
					target.JumpLine = static_cast<std::int32_t>(upper);
					target.JumpFirstLine = static_cast<std::int32_t>(minimum);
				}
				minimum = std::max(minimum, std::int64_t(snapshot.SourceLine) + 1);
				scene.Snapshots.push_back(std::move(target));
			}
			result.push_back(std::move(scene));
		}

		return result;
	}

	static std::string ReadSmallText(const std::filesystem::path& path, std::size_t maximum) {
		io::FileReader input(path);
		Require(input.Size() != 0 && input.Size() <= maximum, "scenario search config/seed input is empty or exceeds its limit");
		const auto bytes = input.Read(0, static_cast<std::size_t>(input.Size()));

		return std::string(bytes.begin(), bytes.end());
	}

	ScenarioSearchLocation ResolveScenarioSearchLocation(const std::filesystem::path& workspace, std::optional<std::filesystem::path> installation) {
		ScenarioSearchLocation result;
		std::filesystem::path seed_file;
		if (installation) {
			result.Installation = *installation;
		} else {
			const auto config_path = workspace / "Files/config.json";
			const auto config = app::ParseLaunchConfiguration(ReadSmallText(config_path, 65536));
			result.Installation = config.Installation;
			seed_file = config.SeedFile;
		}
		if (result.Installation.is_relative()) {
			result.Installation = workspace / result.Installation;
		}
		result.Installation = std::filesystem::canonical(result.Installation);
		Require(std::filesystem::is_directory(result.Installation), "scenario search installation is not a directory");
		if (seed_file.empty()) {
			result.ArchiveSeed = app::ReadOriginalArchiveSeed(result.Installation / "sgre_steam.exe");
		} else {
			if (seed_file.is_relative()) {
				seed_file = workspace / seed_file;
			}
			result.ArchiveSeed = ReadSmallText(seed_file, 1024);
			while (!result.ArchiveSeed.empty() && (result.ArchiveSeed.back() == '\r' || result.ArchiveSeed.back() == '\n')) {
				result.ArchiveSeed.pop_back();
			}
			Require(!result.ArchiveSeed.empty() && result.ArchiveSeed.find_first_of(std::string_view("\0\r\n", 3)) == std::string::npos, "scenario archive seed must be one nonempty line");
		}

		return result;
	}

	class ScenarioSearchCatalog::Implementation {
	public:
		Implementation(ScenarioSearchLocation location, ScenarioSearchLimits limits) : Limits(limits), Location(std::move(location)) {
			RequireLimits(limits);
			resource::ArchiveLimits archive_limits;
			archive_limits.MaxMetadataBytes = limits.MaxDecodedEntryBytes;
			archive_limits.MaxPackedEntryBytes = limits.MaxDecodedEntryBytes;
			archive_limits.MaxDecodedEntryBytes = limits.MaxDecodedEntryBytes;
			const auto data = Location.Installation / "wind3d11data";
			Archive = std::make_unique<resource::ArchiveReader>(data / "scenario_info.psb.m", data / "scenario_body.bin", Location.ArchiveSeed, archive_limits);
			Require(Archive->Index().Entries.size() <= limits.MaxEntries && Archive->Index().ExpireSuffixes == std::vector<std::string>{".scn.m"}, "scenario archive index exceeds its entry limit or has an unrecovered shell suffix");
			for (const auto& entry : Archive->Index().Entries) {
				Entries.push_back({entry.Name, entry.Offset, entry.Size});
			}
		}

		resource::PsbDocument Read(std::string_view name, SearchBudget& budget) {
			const auto bytes = Archive->Read(name, std::string(name) + ".scn.m");
			Consume(budget.DecodedBytes, bytes.size(), Limits.MaxDecodedTotalBytes, "scenario search exceeded its whole-query decoded byte limit");
			const auto header = resource::ReadPsbHeader(bytes);
			Require(header.DataOffset <= bytes.size(), "scenario PSB metadata exceeds its decoded entry");
			auto document = resource::ReadPsbMetadata(std::span<const std::uint8_t>(bytes).first(header.DataOffset), bytes.size());
			Require(document.SerializedSize == bytes.size(), "scenario PSB extent differs from its actual archive entry");

			return document;
		}

		ScenarioSearchLimits Limits;
		ScenarioSearchLocation Location;
		std::unique_ptr<resource::ArchiveReader> Archive;
		std::vector<ScenarioArchiveEntry> Entries;
	};

	ScenarioSearchCatalog::ScenarioSearchCatalog(ScenarioSearchLocation location, ScenarioSearchLimits limits) : m_Implementation(std::make_unique<Implementation>(std::move(location), limits)) {
	}

	ScenarioSearchCatalog::~ScenarioSearchCatalog() = default;

	std::span<const ScenarioArchiveEntry> ScenarioSearchCatalog::Entries() const noexcept {
		return m_Implementation->Entries;
	}

	ScenarioSearchResults ScenarioSearchCatalog::Search(const ScenarioSearchRequest& request) {
		auto& state = *m_Implementation;
		RequireRequest(request, state.Limits);
		ScenarioSearchResults result;
		SearchBudget budget{state.Limits};
		const QueryMatcher matcher(request.Query);
		for (const auto& entry : state.Entries) {
			if (Cancelled(request, result)) {
				break;
			}
			try {
				const auto document = state.Read(entry.Name, budget);
				++result.EntriesRead;
				if (document.Root.Find("scenes") != nullptr) {
					Require(Text(document.Root.Find("name"), 4096, "scenario entry lacks its actual storage identity") == entry.Name, "scenario index key differs from its parsed storage identity");
				}
				if (!SearchDocument(document, request, matcher, budget, result)) {
					break;
				}
			} catch (const std::exception& error) {
				throw std::runtime_error("scenario search entry " + entry.Name + ": " + error.what());
			}
		}

		return result;
	}

	ScenarioJumpPoint ScenarioSearchCatalog::ValidateJump(std::string_view storage, std::int32_t target_line) {
		RequireStorage(storage);
		auto& state = *m_Implementation;
		const auto key = LowerAscii(storage);
		Require(state.Archive->Contains(key), "scenario jump storage does not exist in the original scenario archive");
		SearchBudget budget{state.Limits};
		const auto document = state.Read(key, budget);
		Require(Text(document.Root.Find("name"), 4096, "scenario entry lacks its actual storage identity") == key, "scenario index key differs from its parsed storage identity");

		return ValidateScenarioJump(document, storage, target_line, state.Limits);
	}

	ScenarioNavigation ScenarioSearchCatalog::ReadNavigation() {
		auto& state = *m_Implementation;
		SearchBudget budget{state.Limits};
		ScenarioNavigation result;
		for (const auto& entry : state.Entries) {
			try {
				result.AddDocument(state.Read(entry.Name, budget));
			} catch (const std::exception& error) {
				throw std::runtime_error("scenario navigation entry " + entry.Name + ": " + error.what());
			}
		}

		const auto data = state.Location.Installation / "wind3d11data";
		resource::ArchiveLimits limits;
		limits.MaxMetadataBytes = state.Limits.MaxDecodedEntryBytes;
		limits.MaxPackedEntryBytes = state.Limits.MaxDecodedEntryBytes;
		limits.MaxDecodedEntryBytes = state.Limits.MaxDecodedEntryBytes;
		resource::ArchiveReader configuration(data / "config_info.psb.m", data / "config_body.bin", state.Location.ArchiveSeed, limits);
		const auto phone_master = configuration.Read("phone_master", "phone_master.psb.m");
		Consume(budget.DecodedBytes, phone_master.size(), state.Limits.MaxDecodedTotalBytes, "scenario navigation exceeded its whole-query decoded byte limit");
		result.AddPhoneMaster(resource::ReadPsbMetadata(phone_master, phone_master.size()));

		return result;
	}

	std::vector<ScenarioTestScene> ScenarioSearchCatalog::ReadTestPlan() {
		auto& state = *m_Implementation;
		SearchBudget budget{state.Limits};
		std::vector<ScenarioTestScene> result;
		std::size_t snapshots = 0;
		for (const auto& entry : state.Entries) {
			try {
				const auto document = state.Read(entry.Name, budget);
				if (document.Root.Find("scenes") != nullptr) {
					Require(Text(document.Root.Find("name"), 4096, "scenario entry lacks its actual storage identity") == entry.Name, "scenario index key differs from its parsed storage identity");
				}
				auto scenes = ReadScenarioTestDocument(document, state.Limits);
				Require(scenes.size() <= state.Limits.MaxScenes - result.size(), "scenario test plan exceeds scene limit");
				for (const auto& scene : scenes) {
					Consume(snapshots, scene.Snapshots.size(), state.Limits.MaxRecords, "scenario test plan exceeds snapshot limit");
				}
				result.insert(result.end(), std::make_move_iterator(scenes.begin()), std::make_move_iterator(scenes.end()));
			} catch (const std::exception& error) {
				throw std::runtime_error("scenario test plan entry " + entry.Name + ": " + error.what());
			}
		}

		return result;
	}

} // namespace sgre::scenario
