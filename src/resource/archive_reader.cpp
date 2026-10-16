#include "sgre/resource/archive_reader.hpp"

#include "sgre/resource/mzs_shell.hpp"
#include "sgre/resource/psb.hpp"
#include "sgre/resource/zstd_decoder.hpp"

#include <span>
#include <stdexcept>
#include <utility>

namespace sgre::resource {

	static std::vector<std::uint8_t> DecodeWrapped(std::span<const std::uint8_t> bytes, std::string_view filename, std::string_view seed, const ArchiveLimits& limits, std::size_t maximum) {
		const auto shell = DecodeMzsShell(bytes, filename, seed, limits.ShellKeyPeriod, maximum);

		return DecodeZstd(shell.CompressedBytes, shell.DecodedSize, maximum);
	}

	static PsbDocument ValidatePsb(std::span<const std::uint8_t> bytes, std::size_t metadata_limit) {
		const auto header = ReadPsbHeader(bytes);
		if (header.DataOffset > bytes.size() || header.DataOffset > metadata_limit) {
			throw std::runtime_error("archive PSB metadata exceeds its checked range");
		}

		const auto document = ReadPsbMetadata(bytes.first(header.DataOffset), bytes.size());
		if (document.SerializedSize != bytes.size()) {
			throw std::runtime_error("archive PSB extent differs from its entry");
		}

		return document;
	}

	ArchiveReader::ArchiveReader(const std::filesystem::path& index_path, const std::filesystem::path& body_path, std::string seed, ArchiveLimits limits) : m_Body(body_path), m_Seed(std::move(seed)), m_Limits(limits) {
		if (m_Seed.empty() || limits.MaxMetadataBytes == 0 || limits.MaxPackedEntryBytes == 0 || limits.MaxDecodedEntryBytes == 0 || limits.ShellKeyPeriod == 0) {
			throw std::runtime_error("archive reader requires a seed and positive limits");
		}

		io::FileReader input(index_path);
		if (input.Size() > limits.MaxMetadataBytes) {
			throw std::runtime_error("archive index exceeds its allocation limit");
		}

		const auto packed = input.Read(0, static_cast<std::size_t>(input.Size()));
		const auto decoded = DecodeWrapped(packed, index_path.filename().string(), m_Seed, limits, limits.MaxMetadataBytes);
		m_Index = ReadArchiveIndex(ValidatePsb(decoded, limits.MaxMetadataBytes), m_Body.Size());
		for (std::size_t index = 0; index < m_Index.Entries.size(); ++index) {
			m_Lookup.emplace(m_Index.Entries[index].Name, index);
		}
	}

	const ArchiveIndex& ArchiveReader::Index() const noexcept {
		return m_Index;
	}

	bool ArchiveReader::Contains(std::string_view index_key) const {
		return m_Lookup.contains(index_key);
	}

	std::vector<std::uint8_t> ArchiveReader::Read(std::string_view index_key, std::string_view original_filename) {
		const auto entry_index = m_Lookup.find(index_key);
		if (entry_index == m_Lookup.end()) {
			throw std::runtime_error("archive entry was not found: " + std::string(index_key));
		}

		if (original_filename.empty()) {
			throw std::runtime_error("archive read requires the original resource filename");
		}

		const auto& entry = m_Index.Entries[entry_index->second];
		if (entry.Size > m_Limits.MaxPackedEntryBytes) {
			throw std::runtime_error("archive packed entry exceeds its allocation limit: " + entry.Name);
		}

		try {
			auto bytes = m_Body.Read(entry.Offset, static_cast<std::size_t>(entry.Size));
			if (bytes.size() >= 4 && bytes[0] == 'm' && bytes[1] == 'z' && bytes[2] == 's' && bytes[3] == 0) {
				return DecodeWrapped(bytes, original_filename, m_Seed, m_Limits, m_Limits.MaxDecodedEntryBytes);
			}

			if (bytes.size() >= 4 && bytes[0] == 'P' && bytes[1] == 'S' && bytes[2] == 'B' && bytes[3] == 0) {
				if (bytes.size() > m_Limits.MaxDecodedEntryBytes) {
					throw std::runtime_error("unwrapped archive entry exceeds its decoded allocation limit");
				}

				static_cast<void>(ValidatePsb(bytes, m_Limits.MaxMetadataBytes));

				return bytes;
			}

			throw std::runtime_error("unsupported archive entry shell/signature");
		} catch (const std::exception& error) {
			throw std::runtime_error("archive entry " + entry.Name + " at body offset " + std::to_string(entry.Offset) + ": " + error.what());
		}
	}

} // namespace sgre::resource
