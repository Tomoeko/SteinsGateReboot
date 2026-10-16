#pragma once

#include "sgre/script/resource_file_service.hpp"

#include "archive_psb_fixture.hpp"
#include "sgre/resource/resource_cipher.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using ByteVector = std::vector<std::uint8_t>;
using sgre::core::BackgroundCallLane;
using sgre::core::BackgroundCallManager;
using sgre::core::BackgroundCallTask;
using sgre::io::FileReadCompletion;
using sgre::io::FileReadProcessor;
using sgre::resource::ArchiveMounts;
using sgre::resource::ResourceCache;
using sgre::script::ScriptResourceFileService;
using sgre::script::ScriptResourceRequestMode;

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

template <typename Operation>
static void RequireFailure(Operation operation) {
	try {
		operation();
	} catch (const std::runtime_error&) {
		return;
	}

	throw std::runtime_error("actual resource-file fixture expected supported-domain rejection");
}

static void WriteWord(ByteVector& bytes, std::size_t offset, std::uint32_t value) {
	for (std::uint32_t index = 0; index < 4; ++index) {
		bytes.at(offset + index) = static_cast<std::uint8_t>(value >> (index * 8));
	}
}

static void RefreshChecksum(ByteVector& bytes) {
	std::uint32_t first = 1;
	std::uint32_t second = 0;
	for (std::size_t index = 8; index < 40; ++index) {
		first += bytes.at(index);
		second += first;
	}

	WriteWord(bytes, 40, (second << 16) | first);
}

static ByteVector PayloadPsb() {
	// Independent PSB3 with empty names/strings, root array [true], one four-byte unreferenced chunk.
	ByteVector bytes(44, 0);
	bytes[0] = 'P';
	bytes[1] = 'S';
	bytes[2] = 'B';
	bytes[4] = 3;
	WriteWord(bytes, 8, 44);
	WriteWord(bytes, 12, 44);
	const ByteVector metadata{13, 1, 13, 0, 13, 1, 13, 0, 13, 0, 13, 32, 13, 1, 13, 0, 2, 13, 0, 13, 13, 1, 13, 0, 13, 1, 13, 4};
	bytes.insert(bytes.end(), metadata.begin(), metadata.end());
	WriteWord(bytes, 36, 55);
	WriteWord(bytes, 16, 61);
	WriteWord(bytes, 20, 64);
	WriteWord(bytes, 24, 64);
	WriteWord(bytes, 28, 68);
	WriteWord(bytes, 32, 72);
	RefreshChecksum(bytes);
	bytes.insert(bytes.end(), {10, 20, 30, 40});

	return bytes;
}

static ByteVector IndexPsb(std::size_t body_size) {
	Require(body_size < 256, "synthetic body size exceeds this explicit one-byte entry fixture");
	ByteVector bytes(kArchivePsbFixture.begin(), kArchivePsbFixture.end());
	// Explicit schema changes: entry length, expire list [string1], string offsets [0,8], strings archive/.m.
	bytes[0x174] = static_cast<std::uint8_t>(body_size);
	bytes[0x15a] = 11;
	bytes.erase(bytes.begin() + 0x15f, bytes.begin() + 0x163);
	const ByteVector expiry{32, 13, 1, 13, 0, 21, 1};
	bytes.insert(bytes.begin() + 0x15f, expiry.begin(), expiry.end());
	bytes[0x179] = 2;
	bytes.insert(bytes.begin() + 0x17c, 8);
	bytes.insert(bytes.begin() + 0x185, {'.', 'm', 0});
	WriteWord(bytes, 16, 0x178);
	WriteWord(bytes, 20, 0x17d);
	WriteWord(bytes, 24, 0x188);
	WriteWord(bytes, 28, 0x18b);
	WriteWord(bytes, 32, 0x18e);
	RefreshChecksum(bytes);
	const auto document = sgre::resource::ReadPsbMetadata(bytes, bytes.size());
	const auto index = sgre::resource::ReadArchiveIndex(document, body_size);
	Require(index.Entries.size() == 1 && index.Entries[0].Name == "item" && index.Entries[0].Size == body_size && index.ExpireSuffixes == std::vector<std::string>{".m"}, "independent serialized index fixture differs from its stated schema");

	return bytes;
}

static ByteVector Wrap(ByteVector decoded, std::string filename) {
	Require(decoded.size() < 65536, "synthetic MZS input exceeds its explicit RFC8878 raw-block fixture");
	ByteVector frame{0x28, 0xb5, 0x2f, 0xfd};
	if (decoded.size() < 256) {
		frame.insert(frame.end(), {0x20, static_cast<std::uint8_t>(decoded.size())});
	} else {
		const auto count = decoded.size() - 256;
		frame.insert(frame.end(), {0x60, static_cast<std::uint8_t>(count), static_cast<std::uint8_t>(count >> 8)});
	}

	const auto block = static_cast<std::uint32_t>((decoded.size() << 3) | 1);
	frame.insert(frame.end(), {static_cast<std::uint8_t>(block), static_cast<std::uint8_t>(block >> 8), static_cast<std::uint8_t>(block >> 16)});
	frame.insert(frame.end(), decoded.begin(), decoded.end());
	sgre::resource::ResourceCipher cipher(filename, "synthetic-seed", 131);
	cipher.Transform(frame);
	ByteVector bytes{'m', 'z', 's', 0, 0, 0, 0, 0};
	WriteWord(bytes, 4, static_cast<std::uint32_t>(decoded.size()));
	bytes.insert(bytes.end(), frame.begin(), frame.end());

	return bytes;
}

static void WriteFile(const std::filesystem::path& path, const ByteVector& bytes) {
	std::ofstream file(path, std::ios::binary | std::ios::trunc);
	for (const auto byte : bytes) {
		file.put(static_cast<char>(byte));
	}

	Require(static_cast<bool>(file), "synthetic resource file write failed");
}

static void DrainLane(BackgroundCallManager& background) {
	std::promise<void> barrier;
	auto done = barrier.get_future();
	BackgroundCallTask after(background, BackgroundCallLane::BelowNormal, [&]() {
		barrier.set_value();
	});
	after.Update();
	background.PostLoop();
	done.get();
	after.Close();
}

static void MountActual(FileReadProcessor& processor, ArchiveMounts& mounts, BackgroundCallManager& background, const std::filesystem::path& index_path, const std::filesystem::path& body_path, const ByteVector& body) {
	WriteFile(body_path, body);
	WriteFile(index_path, Wrap(IndexPsb(body.size()), "index_info.psb.m"));
	sgre::resource::ArchiveReadMountInput input;
	input.Prefix = "asset/";
	input.IndexIdentity = "index_info.psb.m";
	input.BodyIdentity = "actual-body";
	input.BodyFilename = body_path;
	input.BodyBytes = body.size();
	input.IndexRead.Region.Filename = index_path;
	input.IndexRead.Region.MaxBytes = 4096;
	input.IndexRead.OriginalFilename = "index_info.psb.m";
	input.IndexRead.Seed = "synthetic-seed";
	input.IndexRead.MaxDecodedBytes = 4096;
	input.IndexRead.MaxMetadataBytes = 4096;
	processor.QueueReadMount(std::move(input));
	processor.Update();
	DrainLane(background);
	processor.Update();
	processor.Update();
	processor.Update();
	Require(mounts.MountCount() == 1 && mounts.RetainedPsbObjectCount() == 1 && !mounts.HasInFlightReadMount(), "actual metadata receiving graph did not finish its three distinct retirement/publication gates");
}
