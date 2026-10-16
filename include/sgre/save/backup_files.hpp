#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sgre::save {

	using BackupAesEncrypt = std::function<std::vector<std::uint8_t>(std::span<const std::uint8_t>, std::string_view)>;

	using BackupAesDecrypt = std::function<std::vector<std::uint8_t>(std::span<const std::uint8_t>, std::string_view)>;

	struct BackupFileServices {
		std::filesystem::path Directory;
		BackupAesDecrypt Decrypt;
		BackupAesEncrypt Encrypt;
		std::function<std::int64_t()> ReadTimestamp;
	};

	struct BackupFileSelection {
		std::uint32_t SegmentId = 0;
		std::int32_t FileId = -1;
	};

	struct BackupFileReadRequest {
		std::uint32_t SegmentId = 0;
		std::int32_t FileId = 0;
		std::size_t RootBytes = 0;
		bool EnableMetaData = false;
		std::string SecureFileId;
	};

	struct BackupFileReadResult {
		bool Success = false;
		bool Broken = false;
		bool NoFile = false;
		bool Crypted = false;
		// Exact accepted plaintext prefix; actual Struct publication remains on its serialized owner thread.
		std::vector<std::uint8_t> Bytes;
		std::string Error;
	};

	struct BackupFileWriteRequest {
		std::uint32_t SegmentId = 0;
		std::int32_t FileId = 0;
		bool EnableMetaData = false;
		std::string SecureFileId;
		// Owned snapshot; the queued worker never borrows or mutates the live Struct storage.
		std::vector<std::uint8_t> Bytes;
		std::int64_t Timestamp = 0;
	};

	struct BackupFileWriteResult {
		bool Success = false;
		// Native077630/0777e0 set this for every write/open/close failure, not only disk exhaustion.
		bool NoSpace = false;
		std::string Error;
	};

	// Native075a40 zero-aligns plaintext to16 bytes before CBC+PKCS7; metadata hashes ciphertext.
	// Native075d50 writes data first, then optional PSB metadata; failed metadata removes the data.
	[[nodiscard]] BackupFileWriteResult WriteAdventureBackupFiles(const std::filesystem::path& directory, const BackupFileWriteRequest& request, const BackupAesEncrypt& encrypt);

	// Directory is the real application-owned rewrite state location, never an original save directory.
	// This operation reads existing files only. Missing metadata/data reports the native NoFile result.
	// The checked AES service implements native raw first32 key bytes, zero-IV CBC and PKCS7 Final padding.
	[[nodiscard]] BackupFileReadResult ReadAdventureBackupFiles(const std::filesystem::path& directory, const BackupFileReadRequest& request, const BackupAesDecrypt& decrypt);

} // namespace sgre::save
