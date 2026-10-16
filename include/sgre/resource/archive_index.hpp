#pragma once

#include "sgre/resource/psb.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace sgre::resource {

	struct ArchiveEntry {
		std::string Name;
		std::uint64_t Offset = 0;
		std::uint64_t Size = 0;
	};

	struct ArchiveIndex {
		std::vector<ArchiveEntry> Entries;
		std::vector<std::string> ExpireSuffixes;
		// Maximum body-file entry end; gaps do not imply corrupt metadata.
		std::uint64_t SerializedExtent = 0;
	};

	[[nodiscard]] ArchiveIndex ReadArchiveIndex(const PsbDocument& document, std::uint64_t body_size);

} // namespace sgre::resource
