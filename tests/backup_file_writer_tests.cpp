#include "sgre/save/backup_files.hpp"

#include "backup_file_fixture.hpp"
#include "sgre/io/file_reader.hpp"
#include "sgre/resource/psb.hpp"

#if defined(__APPLE__)
#include "sgre/platform/backup_crypto.hpp"
#endif

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

static void Require(bool value, std::string_view message) {
	if (!value) {
		throw std::runtime_error(std::string(message));
	}
}

static std::vector<std::uint8_t> ReadOutput(const std::filesystem::path& path) {
	sgre::io::FileReader reader(path);
	Require(reader.Size() <= 65536, "authored backup writer fixture output exceeds its bounded observation");
	return reader.Read(0, static_cast<std::size_t>(reader.Size()));
}

static std::int64_t MetadataInteger(const sgre::resource::PsbDocument& document, std::string_view name) {
	const auto* value = document.Root.Find(name);
	Require(value != nullptr, "actual writer omitted a native metadata member");
	const auto* integer = std::get_if<std::int64_t>(&value->Data);
	Require(integer != nullptr, "native metadata integer did not survive actual PSB decoding");
	return *integer;
}

static void TestActualRawMetadataWriter() {
	BackupFileFixture files;
	sgre::save::BackupFileWriteRequest request;
	request.Bytes = {'a', 'b', 'c'};
	request.EnableMetaData = true;
	request.Timestamp = 0x102030405060708;
	const auto result = sgre::save::WriteAdventureBackupFiles(files.Directory(), request, {});
	Require(result.Success && !result.NoSpace && result.Error.empty(), "actual raw data+metadata writer failed");
	Require(ReadOutput(files.Directory() / "data_000_0000.bin") == request.Bytes, "native data file must contain the exact raw Struct snapshot");
	const auto bytes = ReadOutput(files.Directory() / "meta_000_0000.bin");
	const auto document = sgre::resource::ReadPsbMetadata(bytes, bytes.size());
	Require(bytes.size() < 4096 && document.Header.Version == 3 && document.Names.size() == 6 && document.Strings.empty() && document.Chunks.size() == 1, "first-party compact metadata must decode as the real six-field PSB3 schema");
	Require(MetadataInteger(document, "FileVersion") == 0x10200 && MetadataInteger(document, "TimeStamp") == request.Timestamp && MetadataInteger(document, "FileSize") == 3 && MetadataInteger(document, "OriginalSize") == 3 && MetadataInteger(document, "CryptMagic") == 0, "native integer fields, including the full64-bit timestamp, changed during encoding");
	const auto* digest = std::get_if<sgre::resource::PsbChunkReference>(&document.Root.Find("Digest")->Data);
	Require(digest != nullptr && !digest->Extra && digest->Index == 0, "native metadata Digest must reference its actual binary chunk");
	const auto range = document.Chunks[0];
	const std::array<std::uint8_t, 16> expected{0x90, 0x01, 0x50, 0x98, 0x3c, 0xd2, 0x4f, 0xb0, 0xd6, 0x96, 0x3f, 0x7d, 0x28, 0xe1, 0x7f, 0x72};
	Require(range.Size == expected.size() && range.Offset <= bytes.size() && range.Size <= bytes.size() - range.Offset && std::equal(expected.begin(), expected.end(), bytes.begin() + static_cast<std::ptrdiff_t>(range.Offset)), "metadata Digest differs from the independent RFC1321 abc vector");
	sgre::save::BackupFileReadRequest read;
	read.EnableMetaData = true;
	read.RootBytes = request.Bytes.size();
	const auto loaded = sgre::save::ReadAdventureBackupFiles(files.Directory(), read, {});
	Require(loaded.Success && loaded.Bytes == request.Bytes, "actual independent reader must accept the new raw metadata writer output");

	request.Bytes = {9, 8};
	request.EnableMetaData = false;
	const auto no_meta = sgre::save::WriteAdventureBackupFiles(files.Directory(), request, {});
	Require(no_meta.Success && ReadOutput(files.Directory() / "data_000_0000.bin") == request.Bytes && ReadOutput(files.Directory() / "meta_000_0000.bin") == bytes, "native metadata-disabled write must leave the prior metadata file intact while directly replacing data");
}

static void TestNativeAlignmentAndFailureOrder() {
	BackupFileFixture files;
	for (const std::size_t size : {0u, 1u, 15u, 16u, 17u}) {
		sgre::save::BackupFileWriteRequest request;
		request.SegmentId = 3;
		request.FileId = -1;
		request.Bytes.resize(size, 0x7b);
		request.EnableMetaData = true;
		request.SecureFileId = "actual encryption service input";
		request.Timestamp = -1;
		std::size_t calls = 0;
		const auto result = sgre::save::WriteAdventureBackupFiles(files.Directory(), request, [&](auto aligned, auto key) {
			++calls;
			const auto count = (size + 15) & ~std::size_t{15};
			Require(key == request.SecureFileId && aligned.size() == count && std::equal(request.Bytes.begin(), request.Bytes.end(), aligned.begin()) && std::all_of(aligned.begin() + static_cast<std::ptrdiff_t>(size), aligned.end(), [](auto byte) {
				return byte == 0;
			}),
			    "native worker must zero-align actual snapshot bytes before crossing the platform encrypt boundary");
			// Explicit service-observation vector only; this callback makes no AES fidelity claim.
			return std::vector<std::uint8_t>(count + 16, 0xa5);
		});
		Require(result.Success && calls == 1, "actual aligned encryption service must be called once per worker request");
		const auto bytes = ReadOutput(files.Directory() / "meta_003_-001.bin");
		const auto document = sgre::resource::ReadPsbMetadata(bytes, bytes.size());
		Require(MetadataInteger(document, "OriginalSize") == static_cast<std::int64_t>(size) && MetadataInteger(document, "FileSize") == static_cast<std::int64_t>(((size + 15) & ~std::size_t{15}) + 16) && MetadataInteger(document, "TimeStamp") == -1 && static_cast<std::uint32_t>(MetadataInteger(document, "CryptMagic")) == 0xca2b7fb4, "cipher extent/original extent/key-presence CryptMagic/timestamp must remain independent native fields");
	}

	sgre::save::BackupFileWriteRequest missing;
	missing.Bytes = {1, 2};
	const auto failed_open = sgre::save::WriteAdventureBackupFiles(files.Directory() / "not-created", missing, {});
	Require(!failed_open.Success && failed_open.NoSpace, "native classifies every actual data open failure as NoSpace");
	const auto meta_path = files.Directory() / "meta_000_0000.bin";
	Require(std::filesystem::create_directory(meta_path), "metadata-open failure fixture must own the real conflicting directory");
	missing.EnableMetaData = true;
	const auto failed_meta = sgre::save::WriteAdventureBackupFiles(files.Directory(), missing, {});
	Require(!failed_meta.Success && failed_meta.NoSpace && !std::filesystem::exists(files.Directory() / "data_000_0000.bin") && std::filesystem::is_directory(meta_path), "failed metadata open must delete the newly written data but preserve the unopened conflicting metadata path");
}

#if defined(__APPLE__)
static void TestActualMacEncryptionWriter() {
	BackupFileFixture files;
	sgre::save::BackupFileWriteRequest request;
	request.EnableMetaData = true;
	request.SecureFileId = "short";
	for (std::uint32_t index = 0; index != 16; ++index) {
		request.Bytes.push_back(static_cast<std::uint8_t>(3 + 17 * index));
	}
	const auto result = sgre::save::WriteAdventureBackupFiles(files.Directory(), request, sgre::platform::EncryptBackupBytes);
	const std::vector<std::uint8_t> expected{0x45, 0xd0, 0xb9, 0x59, 0x03, 0x3a, 0xd3, 0x51, 0xae, 0xb6, 0x3d, 0x1a, 0xd2, 0xa7, 0xb7, 0xe0, 0x2a, 0x42, 0x26, 0xee, 0xba, 0xeb, 0xfa, 0x7c, 0x52, 0xe3, 0xf5, 0x4f, 0x50, 0x59, 0x57, 0xbd};
	Require(result.Success && ReadOutput(files.Directory() / "data_000_0000.bin") == expected, "actual macOS writer differs from the retained independent OpenSSL short-key aligned16 CBC+Final vector");
	sgre::save::BackupFileReadRequest read;
	read.EnableMetaData = true;
	read.RootBytes = request.Bytes.size();
	read.SecureFileId = request.SecureFileId;
	const auto result_read = sgre::save::ReadAdventureBackupFiles(files.Directory(), read, sgre::platform::DecryptBackupBytes);
	Require(result_read.Success && result_read.Bytes == request.Bytes, "actual platform encryption/metadata output must pass the independently recovered native load path");
}
#endif

int main() {
	try {
		TestActualRawMetadataWriter();
		TestNativeAlignmentAndFailureOrder();
#if defined(__APPLE__)
		TestActualMacEncryptionWriter();
#endif
		std::cout << "Backup PSB3 writer, native data/meta order, alignment and actual filesystem results PASS\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
