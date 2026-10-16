#pragma once

#include "sgre/core/md5.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

static void AppendBackupFixtureWord(std::vector<std::uint8_t>& bytes, std::uint32_t word) {
	for (std::uint32_t shift = 0; shift < 32; shift += 8) {
		bytes.push_back(static_cast<std::uint8_t>(word >> shift));
	}
}

static void SetBackupFixtureWord(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t word) {
	for (std::uint32_t shift = 0; shift < 32; shift += 8) {
		bytes.at(offset + shift / 8) = static_cast<std::uint8_t>(word >> shift);
	}
}

static void AppendBackupFixtureArray(std::vector<std::uint8_t>& bytes, std::span<const std::uint32_t> values) {
	bytes.push_back(16);
	AppendBackupFixtureWord(bytes, static_cast<std::uint32_t>(values.size()));
	bytes.push_back(16);
	for (const auto value : values) {
		AppendBackupFixtureWord(bytes, value);
	}
}

static std::vector<std::uint8_t> BackupMetadataFixture(std::span<const std::uint8_t> payload, std::uint32_t root_bytes, std::uint32_t magic, std::uint32_t version = 0x10200) {
	// Authored PSB3 tables encode the real native field names; no game metadata bytes are copied.
	const std::array<std::string_view, 6> names{"FileVersion", "TimeStamp", "FileSize", "OriginalSize", "CryptMagic", "Digest"};
	std::vector<std::uint32_t> bases(256);
	std::vector<std::uint32_t> parents(256);
	std::vector<std::uint32_t> leaves;
	std::map<std::pair<std::uint32_t, char>, std::uint32_t> edges;
	std::uint32_t next_block = 256;
	for (const auto name : names) {
		std::uint32_t parent = 0;
		for (const auto letter : name) {
			const auto key = std::make_pair(parent, letter);
			const auto found = edges.find(key);
			if (found != edges.end()) {
				parent = found->second;
				continue;
			}
			const auto node = bases[parent] + static_cast<std::uint8_t>(letter);
			edges.emplace(key, node);
			parents[node] = parent;
			bases[node] = next_block;
			next_block += 256;
			bases.resize(next_block);
			parents.resize(next_block);
			parent = node;
		}
		const auto leaf = bases[parent];
		parents[leaf] = parent;
		leaves.push_back(leaf);
	}

	std::vector<std::uint8_t> bytes(44);
	bytes[0] = 'P';
	bytes[1] = 'S';
	bytes[2] = 'B';
	bytes[4] = 3;
	SetBackupFixtureWord(bytes, 8, 44);
	SetBackupFixtureWord(bytes, 12, static_cast<std::uint32_t>(bytes.size()));
	AppendBackupFixtureArray(bytes, bases);
	AppendBackupFixtureArray(bytes, parents);
	AppendBackupFixtureArray(bytes, leaves);
	SetBackupFixtureWord(bytes, 36, static_cast<std::uint32_t>(bytes.size()));
	bytes.push_back(33);
	AppendBackupFixtureArray(bytes, std::array<std::uint32_t, 6>{0, 1, 2, 3, 4, 5});
	AppendBackupFixtureArray(bytes, std::array<std::uint32_t, 6>{0, 5, 10, 15, 20, 25});
	for (const auto value : std::array<std::uint32_t, 5>{version, 0, static_cast<std::uint32_t>(payload.size()), root_bytes, magic}) {
		bytes.push_back(8);
		AppendBackupFixtureWord(bytes, value);
	}
	bytes.push_back(25);
	bytes.push_back(0);
	SetBackupFixtureWord(bytes, 16, static_cast<std::uint32_t>(bytes.size()));
	AppendBackupFixtureArray(bytes, {});
	SetBackupFixtureWord(bytes, 20, static_cast<std::uint32_t>(bytes.size()));
	SetBackupFixtureWord(bytes, 24, static_cast<std::uint32_t>(bytes.size()));
	AppendBackupFixtureArray(bytes, std::array<std::uint32_t, 1>{0});
	SetBackupFixtureWord(bytes, 28, static_cast<std::uint32_t>(bytes.size()));
	AppendBackupFixtureArray(bytes, std::array<std::uint32_t, 1>{16});
	SetBackupFixtureWord(bytes, 32, static_cast<std::uint32_t>(bytes.size()));
	std::uint32_t first = 1;
	std::uint32_t second = 0;
	for (std::size_t index = 8; index < 40; ++index) {
		first += bytes[index];
		second += first;
	}
	SetBackupFixtureWord(bytes, 40, ((second % 65521) << 16) | (first % 65521));
	sgre::core::Md5 digest;
	digest.Update(payload);
	const auto hash = digest.Finalize();
	bytes.insert(bytes.end(), hash.begin(), hash.end());

	return bytes;
}

class BackupFileFixture {
public:
	BackupFileFixture() {
		const auto parent = std::filesystem::path("Files/user_state/rewrite/backup-load-tests");
		std::filesystem::create_directories(parent);
		m_Directory = parent / std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
		if (!std::filesystem::create_directory(m_Directory)) {
			throw std::runtime_error("backup fixture must create its own unique ignored state directory");
		}
		m_Owned = true;
	}

	~BackupFileFixture() noexcept {
		if (m_Owned) {
			std::error_code error;
			std::filesystem::remove_all(m_Directory, error);
		}
	}

	BackupFileFixture(const BackupFileFixture&) = delete;
	BackupFileFixture& operator=(const BackupFileFixture&) = delete;
	BackupFileFixture(BackupFileFixture&&) = delete;
	BackupFileFixture& operator=(BackupFileFixture&&) = delete;

	const std::filesystem::path& Directory() const noexcept {
		return m_Directory;
	}

	void Write(std::string_view name, std::span<const std::uint8_t> bytes) {
		if (name.find_first_of("/\\") != std::string_view::npos) {
			throw std::runtime_error("backup fixture file must be inside its newly owned directory");
		}
		std::ofstream stream(m_Directory / name, std::ios::binary | std::ios::trunc);
		for (const auto byte : bytes) {
			stream.put(static_cast<char>(byte));
		}
		stream.close();
		if (!stream) {
			throw std::runtime_error("backup fixture could not write its authored native-format input");
		}
	}

private:
	std::filesystem::path m_Directory;
	bool m_Owned = false;
};
