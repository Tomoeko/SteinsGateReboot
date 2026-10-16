#include "sgre/save/backup_files.hpp"

#include "sgre/core/md5.hpp"
#include "sgre/io/file_reader.hpp"
#include "sgre/resource/psb.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace sgre::save {

	static constexpr std::size_t kMaximumBackupMetadataBytes = 64 * 1024 * 1024;
	static constexpr std::size_t kMaximumBackupRootBytes = 160 * 1024 * 1024;
	static constexpr std::uint32_t kBackupFileVersion = 0x10200;
	static constexpr std::uint32_t kBackupCryptMagic = 0xca2b7fb4;

	static std::string DecimalBackupId(std::int32_t value, std::size_t minimum_width) {
		std::array<char, 16> text{};
		const auto converted = std::to_chars(text.data(), text.data() + text.size(), value);
		if (converted.ec != std::errc{}) {
			throw std::runtime_error("Backup numeric file identity exceeds its native decimal domain");
		}

		std::string result(text.data(), converted.ptr);
		if (result.size() < minimum_width) {
			result.insert(value < 0 ? 1 : 0, minimum_width - result.size(), '0');
		}

		return result;
	}

	static std::filesystem::path BackupFilePath(const std::filesystem::path& directory, std::string_view prefix, const BackupFileReadRequest& request) {
		return directory / (std::string(prefix) + "_" + DecimalBackupId(static_cast<std::int32_t>(request.SegmentId), 3) + "_" + DecimalBackupId(request.FileId, 4) + ".bin");
	}

	static std::optional<std::vector<std::uint8_t>> ReadBackupFile(const std::filesystem::path& path, std::size_t maximum_bytes, BackupFileReadResult& result) {
		std::unique_ptr<io::FileReader> reader;
		try {
			reader = std::make_unique<io::FileReader>(path);
		} catch (const std::runtime_error& error) {
			// Native077440/077250 classify an unsuccessful OPEN_EXISTING as NoFile.
			result.NoFile = true;
			result.Error = path.filename().string() + ": " + error.what();

			return {};
		}
		if (reader->Size() > maximum_bytes) {
			result.Broken = true;
			result.Error = path.filename().string() + ": input exceeds its actual bounded backup storage";

			return {};
		}

		try {
			return reader->Read(0, static_cast<std::size_t>(reader->Size()));
		} catch (const std::runtime_error& error) {
			result.Broken = true;
			result.Error = path.filename().string() + ": " + error.what();

			return {};
		}
	}

	static std::uint32_t BackupMetadataWord(const resource::PsbValue& root, std::string_view name) {
		const auto* field = root.Find(name);
		const auto* integer = field == nullptr ? nullptr : std::get_if<std::int64_t>(&field->Data);
		if (integer == nullptr || *integer < std::numeric_limits<std::int32_t>::min() || *integer > std::numeric_limits<std::uint32_t>::max()) {
			throw std::runtime_error("Backup metadata " + std::string(name) + " is not a native32-bit integer");
		}

		return static_cast<std::uint32_t>(*integer);
	}

	static std::array<std::uint8_t, 16> BackupMetadataDigest(const resource::PsbDocument& document, std::span<const std::uint8_t> bytes) {
		const auto* field = document.Root.Find("Digest");
		const auto* reference = field == nullptr ? nullptr : std::get_if<resource::PsbChunkReference>(&field->Data);
		if (reference == nullptr) {
			throw std::runtime_error("Backup metadata Digest has no actual16-byte binary chunk");
		}
		const auto& ranges = reference->Extra ? document.ExtraChunks : document.Chunks;
		if (reference->Index >= ranges.size()) {
			throw std::runtime_error("Backup metadata Digest references an absent chunk");
		}
		const auto& range = ranges[reference->Index];
		if (range.Size != 16 || range.Offset > bytes.size() || range.Size > bytes.size() - range.Offset) {
			throw std::runtime_error("Backup metadata Digest chunk exceeds its actual16-byte source range");
		}

		std::array<std::uint8_t, 16> digest{};
		std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(range.Offset), digest.size(), digest.begin());

		return digest;
	}

	static std::array<std::uint8_t, 16> ValidateBackupMetadata(std::span<const std::uint8_t> bytes, const BackupFileReadRequest& request, BackupFileReadResult& result) {
		const auto document = resource::ReadPsbMetadata(bytes, bytes.size());
		if (BackupMetadataWord(document.Root, "FileVersion") != kBackupFileVersion) {
			throw std::runtime_error("Backup metadata FileVersion differs from native0x10200");
		}
		const auto magic = BackupMetadataWord(document.Root, "CryptMagic");
		// Native07577e..075783 tests the whole word, then stores one bool byte before the expected-magic check.
		result.Crypted = magic != 0;
		if (magic != (request.SecureFileId.empty() ? 0 : kBackupCryptMagic)) {
			throw std::runtime_error("Backup metadata CryptMagic differs from its actual secure-key mode");
		}
		if (BackupMetadataWord(document.Root, "OriginalSize") != request.RootBytes) {
			throw std::runtime_error("Backup metadata OriginalSize differs from the actual Struct root");
		}

		// Native reads TimeStamp and stores FileSize when writing; neither controls this load acceptance.
		return BackupMetadataDigest(document, bytes);
	}

	struct BackupMetadataNameNode {
		std::map<std::uint8_t, std::size_t> Children;
		std::optional<std::size_t> Name;
	};

	struct BackupMetadataNames {
		std::vector<std::uint32_t> Bases;
		std::vector<std::uint32_t> Parents;
		std::vector<std::uint32_t> Leaves;
	};

	static BackupMetadataNames MakeBackupMetadataNames(std::span<const std::string_view> names) {
		std::vector<BackupMetadataNameNode> nodes(1);
		for (std::size_t name = 0; name != names.size(); ++name) {
			std::size_t parent = 0;
			for (std::size_t index = 0; index <= names[name].size(); ++index) {
				const auto letter = index == names[name].size() ? std::uint8_t{0} : static_cast<std::uint8_t>(names[name][index]);
				const auto found = nodes[parent].Children.find(letter);
				if (found != nodes[parent].Children.end()) {
					parent = found->second;
					continue;
				}
				const auto child = nodes.size();
				nodes[parent].Children.emplace(letter, child);
				nodes.emplace_back();
				parent = child;
			}
			nodes[parent].Name = name;
		}

		BackupMetadataNames result;
		result.Bases.resize(1);
		result.Parents.resize(1);
		result.Leaves.resize(names.size());
		std::vector<bool> occupied(1, true);
		std::vector<std::uint32_t> positions(nodes.size());
		for (std::size_t parent = 0; parent != nodes.size(); ++parent) {
			if (nodes[parent].Children.empty()) {
				continue;
			}
			// A checked double-array trie packs only actual edges; it does not reserve a256-cell block per letter.
			std::uint32_t base = 1;
			while (true) {
				bool available = true;
				for (const auto& [letter, child] : nodes[parent].Children) {
					static_cast<void>(child);
					const auto position = static_cast<std::size_t>(base) + letter;
					if (position < occupied.size() && occupied[position]) {
						available = false;
						break;
					}
				}
				if (available) {
					break;
				}
				++base;
				if (base > 4096) {
					throw std::runtime_error("Backup metadata's fixed name trie exceeds its bounded packing domain");
				}
			}
			result.Bases[positions[parent]] = base;
			for (const auto& [letter, child] : nodes[parent].Children) {
				const auto position = base + letter;
				if (position >= occupied.size()) {
					occupied.resize(static_cast<std::size_t>(position) + 1);
					result.Bases.resize(occupied.size());
					result.Parents.resize(occupied.size());
				}
				occupied[position] = true;
				positions[child] = position;
				result.Parents[position] = positions[parent];
				if (nodes[child].Name) {
					result.Leaves[*nodes[child].Name] = position;
				}
			}
		}

		return result;
	}

	static void AppendBackupInteger(std::vector<std::uint8_t>& bytes, std::uint64_t value, std::uint32_t width) {
		for (std::uint32_t shift = 0; shift < width * 8; shift += 8) {
			bytes.push_back(static_cast<std::uint8_t>(value >> shift));
		}
	}

	static void SetBackupHeaderWord(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
		if (offset > bytes.size() || 4 > bytes.size() - offset) {
			throw std::runtime_error("Backup PSB header word exceeds its owned buffer");
		}
		for (std::uint32_t index = 0; index != 4; ++index) {
			bytes[offset + index] = static_cast<std::uint8_t>(value >> (index * 8));
		}
	}

	static std::uint32_t BackupUnsignedWidth(std::uint32_t value) {
		std::uint32_t width = 1;
		while (width != 4 && value >= (std::uint32_t{1} << (width * 8))) {
			++width;
		}

		return width;
	}

	static void AppendBackupArray(std::vector<std::uint8_t>& bytes, std::span<const std::uint32_t> values) {
		if (values.size() > 65536) {
			throw std::runtime_error("Backup metadata array exceeds its fixed schema domain");
		}
		const auto count = static_cast<std::uint32_t>(values.size());
		const auto count_width = BackupUnsignedWidth(count);
		const auto maximum = values.empty() ? 0 : *std::max_element(values.begin(), values.end());
		const auto value_width = BackupUnsignedWidth(maximum);
		bytes.push_back(static_cast<std::uint8_t>(12 + count_width));
		AppendBackupInteger(bytes, count, count_width);
		bytes.push_back(static_cast<std::uint8_t>(12 + value_width));
		for (const auto value : values) {
			AppendBackupInteger(bytes, value, value_width);
		}
	}

	static std::vector<std::uint8_t> MakeBackupMetadata(std::span<const std::uint8_t> payload, std::size_t original_size, bool secure, std::int64_t timestamp) {
		// Native075d50 creates these six values. This emits their PSB3 representation, not an extracted asset.
		static constexpr std::array<std::string_view, 6> kNames{"CryptMagic", "Digest", "FileSize", "FileVersion", "OriginalSize", "TimeStamp"};
		const auto names = MakeBackupMetadataNames(kNames);
		std::vector<std::uint8_t> bytes(44);
		bytes[0] = 'P';
		bytes[1] = 'S';
		bytes[2] = 'B';
		bytes[4] = 3;
		SetBackupHeaderWord(bytes, 8, 44);
		SetBackupHeaderWord(bytes, 12, 44);
		AppendBackupArray(bytes, names.Bases);
		AppendBackupArray(bytes, names.Parents);
		AppendBackupArray(bytes, names.Leaves);
		SetBackupHeaderWord(bytes, 36, static_cast<std::uint32_t>(bytes.size()));

		const std::array<std::uint32_t, 6> words{secure ? kBackupCryptMagic : 0, 0, static_cast<std::uint32_t>(payload.size()), kBackupFileVersion, static_cast<std::uint32_t>(original_size), 0};
		std::vector<std::uint8_t> values;
		std::array<std::uint32_t, 6> offsets{};
		for (std::size_t index = 0; index != kNames.size(); ++index) {
			offsets[index] = static_cast<std::uint32_t>(values.size());
			if (index == 1) {
				values.push_back(25);
				values.push_back(0);
			} else if (index == 5) {
				values.push_back(12);
				AppendBackupInteger(values, std::bit_cast<std::uint64_t>(timestamp), 8);
			} else {

				values.push_back(8);
				AppendBackupInteger(values, words[index], 4);
			}
		}
		bytes.push_back(33);
		AppendBackupArray(bytes, std::array<std::uint32_t, 6>{0, 1, 2, 3, 4, 5});
		AppendBackupArray(bytes, offsets);
		bytes.insert(bytes.end(), values.begin(), values.end());
		SetBackupHeaderWord(bytes, 16, static_cast<std::uint32_t>(bytes.size()));
		AppendBackupArray(bytes, {});
		SetBackupHeaderWord(bytes, 20, static_cast<std::uint32_t>(bytes.size()));
		SetBackupHeaderWord(bytes, 24, static_cast<std::uint32_t>(bytes.size()));
		AppendBackupArray(bytes, std::array<std::uint32_t, 1>{0});
		SetBackupHeaderWord(bytes, 28, static_cast<std::uint32_t>(bytes.size()));
		AppendBackupArray(bytes, std::array<std::uint32_t, 1>{16});
		SetBackupHeaderWord(bytes, 32, static_cast<std::uint32_t>(bytes.size()));

		std::uint32_t first = 1;
		std::uint32_t second = 0;
		for (std::size_t index = 8; index != 40; ++index) {
			first = (first + bytes[index]) % 65521;
			second = (second + first) % 65521;
		}
		SetBackupHeaderWord(bytes, 40, (second << 16) | first);
		core::Md5 digest;
		digest.Update(payload);
		const auto hash = digest.Finalize();
		bytes.insert(bytes.end(), hash.begin(), hash.end());

		return bytes;
	}

	static bool WriteBackupFile(const std::filesystem::path& path, std::span<const std::uint8_t> bytes, BackupFileWriteResult& result, bool& opened) {
		std::ofstream stream(path, std::ios::binary | std::ios::trunc);
		opened = stream.is_open();
		if (!opened) {
			result.NoSpace = true;
			result.Error = "Backup cannot open output: " + path.filename().string();
			return false;
		}
		if (!bytes.empty()) {
			// Standard binary stream boundary, matching FileReader's explicit byte-buffer conversion.
			stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		}
		const bool complete = static_cast<bool>(stream);
		stream.close();
		if (!complete || !stream) {
			result.NoSpace = true;
			result.Error = "Backup output write or close failed: " + path.filename().string();
			return false;
		}

		return true;
	}

	static void RemoveFailedBackupFile(const std::filesystem::path& path, BackupFileWriteResult& result) {
		std::error_code error;
		std::filesystem::remove(path, error);
		if (error) {
			result.Error += "; output cleanup failed for " + path.filename().string() + ": " + error.message();
		}
	}

	BackupFileWriteResult WriteAdventureBackupFiles(const std::filesystem::path& directory, const BackupFileWriteRequest& request, const BackupAesEncrypt& encrypt) {
		if (directory.empty() || request.SegmentId > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()) || request.Bytes.size() > kMaximumBackupRootBytes || (!request.SecureFileId.empty() && !encrypt)) {
			throw std::runtime_error("Backup file writing requires its real bounded state location, Struct snapshot and native AES service");
		}
		std::vector<std::uint8_t> ciphertext;
		std::span<const std::uint8_t> payload = request.Bytes;
		if (!request.SecureFileId.empty()) {
			const auto aligned_size = (request.Bytes.size() + 15) & ~std::size_t{15};
			auto aligned = request.Bytes;
			aligned.resize(aligned_size, 0);
			ciphertext = encrypt(aligned, request.SecureFileId);
			if (ciphertext.size() != aligned_size + 16) {
				throw std::runtime_error("Backup AES output differs from native aligned-root-plus-final-block size");
			}
			payload = ciphertext;
		}
		const auto metadata = MakeBackupMetadata(payload, request.Bytes.size(), !request.SecureFileId.empty(), request.Timestamp);
		BackupFileReadRequest identity;
		identity.SegmentId = request.SegmentId;
		identity.FileId = request.FileId;
		const auto data_path = BackupFilePath(directory, "data", identity);
		const auto meta_path = BackupFilePath(directory, "meta", identity);
		BackupFileWriteResult result;
		bool opened = false;
		if (!WriteBackupFile(data_path, payload, result, opened)) {
			if (opened) {
				RemoveFailedBackupFile(data_path, result);
			}
			return result;
		}
		if (request.EnableMetaData && !WriteBackupFile(meta_path, metadata, result, opened)) {
			// Native0777e0 removes data even when the metadata file could not be opened.
			RemoveFailedBackupFile(data_path, result);
			if (opened) {
				RemoveFailedBackupFile(meta_path, result);
			}
			return result;
		}
		result.Success = true;

		return result;
	}

	BackupFileReadResult ReadAdventureBackupFiles(const std::filesystem::path& directory, const BackupFileReadRequest& request, const BackupAesDecrypt& decrypt) {
		if (directory.empty() || request.SegmentId > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()) || request.RootBytes > kMaximumBackupRootBytes || (!request.SecureFileId.empty() && !decrypt)) {
			throw std::runtime_error("Backup file loading requires its real bounded state location, Struct layout and native AES service");
		}
		const auto expected_cipher_bytes = ((request.RootBytes + 15) & ~std::size_t{15}) + 16;
		BackupFileReadResult result;
		std::optional<std::array<std::uint8_t, 16>> expected_digest;
		if (request.EnableMetaData) {
			const auto bytes = ReadBackupFile(BackupFilePath(directory, "meta", request), kMaximumBackupMetadataBytes, result);
			if (!bytes) {
				// Native075677 marks NoFile after every failed metadata read, preserving an existing Broken result.
				result.NoFile = true;

				return result;
			}
			if (bytes->empty()) {
				result.NoFile = true;
				result.Error = "Backup metadata has no actual input bytes";

				return result;
			}
			try {
				expected_digest = ValidateBackupMetadata(*bytes, request, result);
			} catch (const std::runtime_error& error) {
				result.Broken = true;
				result.Error = error.what();

				return result;
			}
		}

		const auto maximum_payload = request.SecureFileId.empty() ? request.RootBytes : expected_cipher_bytes;
		auto bytes = ReadBackupFile(BackupFilePath(directory, "data", request), maximum_payload, result);
		if (!bytes) {
			return result;
		}
		if (expected_digest) {
			core::Md5 digest;
			digest.Update(*bytes);
			if (digest.Finalize() != *expected_digest) {
				result.Broken = true;
				result.Error = "Backup payload MD5 differs from its actual metadata Digest";

				return result;
			}
		} else if (bytes->size() != request.RootBytes) {
			result.Broken = true;
			result.Error = "Backup payload size differs from its actual Struct root without metadata";

			return result;
		}

		if (!request.SecureFileId.empty()) {
			if (bytes->size() != expected_cipher_bytes) {
				result.Broken = true;
				result.Error = "Backup encrypted payload differs from its native aligned-root-plus-final-block domain";

				return result;
			}
			try {
				result.Bytes = decrypt(*bytes, request.SecureFileId);
			} catch (const std::runtime_error& error) {
				result.Broken = true;
				result.Error = std::string("Backup native AES decryption failed: ") + error.what();

				return result;
			}
			if (result.Bytes.size() < request.RootBytes) {
				result.Broken = true;
				result.Error = "Backup AES output does not cover its actual Struct root";
				result.Bytes.clear();

				return result;
			}
			result.Bytes.resize(request.RootBytes);
		} else {
			result.Bytes = std::move(*bytes);
		}
		result.Success = true;

		return result;
	}

} // namespace sgre::save
