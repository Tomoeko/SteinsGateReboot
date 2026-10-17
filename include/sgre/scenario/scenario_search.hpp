#pragma once

#include "sgre/resource/psb.hpp"
#include "sgre/scenario/scenario_navigation.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sgre::scenario {

	struct ScenarioSearchLocation {
		std::filesystem::path Installation;
		std::string ArchiveSeed;
	};

	// Reads existing config and the verified seed without creating or repairing any files.
	[[nodiscard]] ScenarioSearchLocation ResolveScenarioSearchLocation(const std::filesystem::path& workspace, std::optional<std::filesystem::path> installation = {});

	struct ScenarioSearchLimits {
		std::size_t MaxEntries = 4096;
		std::size_t MaxScenes = 65536;
		std::size_t MaxRecords = 2000000;
		std::size_t MaxDecodedEntryBytes = 64 * 1024 * 1024;
		std::size_t MaxDecodedTotalBytes = 512 * 1024 * 1024;
		std::size_t MaxTextBytes = 1024 * 1024;
		std::size_t MaxQueryBytes = 4096;
		std::size_t MaxResults = 10000;
		std::size_t MaxResultBytes = 16 * 1024 * 1024;
	};

	struct ScenarioSearchRequest {
		// Case-sensitive UTF-8 substring in original text, selected translation, character or scene identity.
		// Empty queries browse the same real records with the same result/work limits.
		std::string Query;
		std::string Language = "en";
		std::size_t Limit = 50;
		// Called synchronously between entries/records, never from an engine or OS callback.
		std::function<bool()> Cancelled;
	};

	struct ScenarioJumpPoint {
		// Validation preserves the supplied launch spelling; canonical metadata is reported separately.
		std::string Storage;
		std::string CanonicalStorage;
		std::string SceneLabel;
		std::int32_t SourceLine = 0;
		std::int32_t SnapshotLine = 0;
		std::size_t SnapshotIndex = 0;
		std::int32_t Point = 0;
	};

	struct ScenarioSearchMatch {
		std::string Storage;
		std::string SceneLabel;
		std::int32_t SourceLine = 0;
		std::optional<std::int32_t> TranslatedSourceLine;
		std::string Character;
		std::string OriginalText;
		std::string Translation;
		std::string Language;
		std::size_t TextId = 0;
		std::int32_t JumpLine = 0;
		std::int32_t SnapshotLine = 0;
		std::string JumpTarget;
	};

	struct ScenarioSearchResults {
		std::vector<ScenarioSearchMatch> Matches;
		bool Truncated = false;
		bool Cancelled = false;
		std::size_t EntriesRead = 0;
		std::size_t TextRowsRead = 0;
	};

	struct ScenarioArchiveEntry {
		std::string Name;
		std::uint64_t Offset = 0;
		std::uint64_t PackedBytes = 0;
	};

	// These operations also serve callers already holding actual decoded scenario metadata.
	[[nodiscard]] ScenarioSearchResults SearchScenarioDocument(const resource::PsbDocument& document, const ScenarioSearchRequest& request, ScenarioSearchLimits limits = {});
	[[nodiscard]] ScenarioJumpPoint ValidateScenarioJump(const resource::PsbDocument& document, std::string_view storage, std::int32_t target_line, ScenarioSearchLimits limits = {});

	// Serialized inspector owner: reads one bounded archive entry at a time and never writes assets/config.
	// Returned values own their strings and survive the catalog or decoded document's retirement.
	class ScenarioSearchCatalog {
	public:
		explicit ScenarioSearchCatalog(ScenarioSearchLocation location, ScenarioSearchLimits limits = {});
		~ScenarioSearchCatalog();
		ScenarioSearchCatalog(const ScenarioSearchCatalog&) = delete;
		ScenarioSearchCatalog& operator=(const ScenarioSearchCatalog&) = delete;
		ScenarioSearchCatalog(ScenarioSearchCatalog&&) = delete;
		ScenarioSearchCatalog& operator=(ScenarioSearchCatalog&&) = delete;
		[[nodiscard]] std::span<const ScenarioArchiveEntry> Entries() const noexcept;
		[[nodiscard]] ScenarioSearchResults Search(const ScenarioSearchRequest& request);
		[[nodiscard]] ScenarioJumpPoint ValidateJump(std::string_view storage, std::int32_t target_line);
		[[nodiscard]] ScenarioNavigation ReadNavigation();

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::scenario
