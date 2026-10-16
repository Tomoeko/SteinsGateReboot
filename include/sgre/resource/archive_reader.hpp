#pragma once

#include "sgre/io/file_reader.hpp"
#include "sgre/resource/archive_index.hpp"

#include <cstddef>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace sgre::resource {

	struct ArchiveLimits {
		std::size_t MaxMetadataBytes = 64 * 1024 * 1024;
		std::size_t MaxPackedEntryBytes = 160 * 1024 * 1024;
		std::size_t MaxDecodedEntryBytes = 160 * 1024 * 1024;
		std::size_t ShellKeyPeriod = 131;
	};

	class ArchiveReader {
	public:
		ArchiveReader(const std::filesystem::path& index_path, const std::filesystem::path& body_path, std::string seed, ArchiveLimits limits = {});
		ArchiveReader(const ArchiveReader&) = delete;
		ArchiveReader& operator=(const ArchiveReader&) = delete;
		ArchiveReader(ArchiveReader&&) = delete;
		ArchiveReader& operator=(ArchiveReader&&) = delete;

		[[nodiscard]] const ArchiveIndex& Index() const noexcept;
		[[nodiscard]] bool Contains(std::string_view index_key) const;
		// Index keys and shell filenames are distinct original identities; do not infer suffixes here.
		[[nodiscard]] std::vector<std::uint8_t> Read(std::string_view index_key, std::string_view original_filename);

	private:
		io::FileReader m_Body;
		ArchiveIndex m_Index;
		// Views borrow immutable m_Index entry names. This owner cannot move or copy.
		std::map<std::string_view, std::size_t, std::less<>> m_Lookup;
		std::string m_Seed;
		ArchiveLimits m_Limits;
	};

} // namespace sgre::resource
