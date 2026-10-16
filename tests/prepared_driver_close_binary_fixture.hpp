#pragma once

#include "sgre/io/script_buffer_reader.hpp"
#include "sgre/resource/resource_cache.hpp"
#include "sgre/resource/resource_cipher.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <stdexcept>
#include <string>
#include <vector>

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

	throw std::runtime_error("script buffer fixture expected a supported-domain rejection");
}

static void AppendWord(std::vector<std::uint8_t>& bytes, std::uint32_t word) {
	for (std::size_t index = 0; index < 4; ++index) {
		bytes.push_back(static_cast<std::uint8_t>(word >> (8 * index)));
	}
}

static void AppendString(std::vector<std::uint8_t>& bytes, std::string_view text) {
	AppendWord(bytes, 0x08000010);
	AppendWord(bytes, static_cast<std::uint32_t>(text.size()));
	bytes.insert(bytes.end(), text.begin(), text.end());
}

static std::vector<std::uint8_t> CompiledFixture() {
	std::vector<std::uint8_t> bytes{0xfa, 0xfa, 0x52, 0x49, 0x51, 0x53, 1, 0, 0, 0};
	const auto part = [&]() {
		AppendWord(bytes, 0x50415254);
	};
	part();
	AppendString(bytes, "independent-buffer.nut");
	AppendString(bytes, "main");
	part();
	for (const auto count : std::array<std::uint32_t, 8>{0, 1, 0, 0, 0, 0, 2, 0}) {
		AppendWord(bytes, count);
	}
	part();
	part();
	AppendString(bytes, "this");
	for (std::size_t section = 0; section < 5; ++section) {
		part();
	}
	AppendWord(bytes, 42);
	bytes.insert(bytes.end(), {2, 1, 0, 0});
	AppendWord(bytes, 1);
	bytes.insert(bytes.end(), {19, 1, 0, 0});
	part();
	AppendWord(bytes, 2);
	bytes.insert(bytes.end(), {0, 0});
	AppendWord(bytes, 0x5441494c);

	return bytes;
}

static std::vector<std::uint8_t> WrappedFixture(std::span<const std::uint8_t> decoded, std::string_view filename) {
	Require(decoded.size() < 256, "independent script wrapper must fit the single-byte content size");
	std::vector<std::uint8_t> compressed{0x28, 0xb5, 0x2f, 0xfd, 0x20, static_cast<std::uint8_t>(decoded.size())};
	const auto block = static_cast<std::uint32_t>(decoded.size() << 3) | 1;
	compressed.push_back(static_cast<std::uint8_t>(block));
	compressed.push_back(static_cast<std::uint8_t>(block >> 8));
	compressed.push_back(static_cast<std::uint8_t>(block >> 16));
	compressed.insert(compressed.end(), decoded.begin(), decoded.end());
	sgre::resource::ResourceCipher cipher(filename, "synthetic-seed", 131);
	cipher.Transform(compressed);
	std::vector<std::uint8_t> wrapped{'m', 'z', 's', 0};
	AppendWord(wrapped, static_cast<std::uint32_t>(decoded.size()));
	wrapped.insert(wrapped.end(), compressed.begin(), compressed.end());

	return wrapped;
}

static void WriteFixture(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
	std::ofstream stream(path, std::ios::binary);
	for (const auto byte : bytes) {
		stream.put(static_cast<char>(byte));
	}
	if (!stream) {
		throw std::runtime_error("script buffer fixture file write failed");
	}
}

static void DrainLane(sgre::core::BackgroundCallManager& background) {
	std::promise<void> barrier;
	auto done = barrier.get_future();
	sgre::core::BackgroundCallTask after(background, sgre::core::BackgroundCallLane::BelowNormal, [&]() {
		barrier.set_value();
	});
	after.Update();
	background.PostLoop();
	done.get();
	after.Close();
}
