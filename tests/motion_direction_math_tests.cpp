#include "sgre/scene/motion_direction_math.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static void TestIndependentDirections() {
	Require(std::bit_cast<std::uint32_t>(sgre::scene::EvaluateMotionDirectionRadians(0, 1)) == 0 && std::bit_cast<std::uint32_t>(sgre::scene::EvaluateMotionDirectionRadians(-0.0f, 1)) == 0x80000000, "atan2 positive horizontal axis retains signed zero");
	Require(std::bit_cast<std::uint32_t>(sgre::scene::EvaluateMotionDirectionRadians(0, -1)) == 0x40490fdb && std::bit_cast<std::uint32_t>(sgre::scene::EvaluateMotionDirectionRadians(-0.0f, -1)) == 0xc0490fdb, "atan2 negative horizontal axis follows the vertical signed zero");
	Require(std::bit_cast<std::uint32_t>(sgre::scene::EvaluateMotionDirectionRadians(1, 0)) == 0x3fc90fdb && std::bit_cast<std::uint32_t>(sgre::scene::EvaluateMotionDirectionRadians(-1, 0)) == 0xbfc90fdb, "atan2 vertical axes use actual rounded native half pi");
	for (const auto vertical : std::array<float, 5>{-4, -1, 0.0005f, 1, 4}) {
		for (const auto horizontal : std::array<float, 5>{-4, -1, 0.0005f, 1, 4}) {
			const auto actual = sgre::scene::EvaluateMotionDirectionRadians(vertical, horizontal);
			const auto independent = static_cast<float>(std::atan2(static_cast<double>(vertical), static_cast<double>(horizontal)));
			Require(std::abs(actual - independent) < 0.000001f, "atan2 quadrant/reduction differs from independent geometry");
		}
	}

	bool rejected = false;
	try {
		static_cast<void>(sgre::scene::EvaluateMotionDirectionRadians(std::numeric_limits<float>::infinity(), 1));
	} catch (const std::runtime_error& error) {
		rejected = std::string_view(error.what()).find("finite native float inputs") != std::string_view::npos;
	}

	Require(rejected, "nonfinite direction math remains explicitly unsupported");
}

static std::uint32_t ReadWord(std::span<const std::uint8_t> bytes, std::size_t offset) {
	if (offset > bytes.size() || 4 > bytes.size() - offset) {
		throw std::runtime_error("truncated native direction word");
	}

	std::uint32_t result = 0;
	for (std::size_t index = 0; index < 4; ++index) {
		result |= static_cast<std::uint32_t>(bytes[offset + index]) << (index * 8);
	}

	return result;
}

template <std::size_t Size>
static std::array<std::uint8_t, Size> ReadRecord(std::ifstream& input) {
	std::array<char, Size> raw{};
	input.read(raw.data(), static_cast<std::streamsize>(raw.size()));
	Require(input.gcount() == static_cast<std::streamsize>(raw.size()), "truncated native direction record");
	std::array<std::uint8_t, Size> result{};
	for (std::size_t index = 0; index < Size; ++index) {
		result[index] = static_cast<std::uint8_t>(static_cast<unsigned char>(raw[index]));
	}

	return result;
}

static void ReplayNative(const char* path) {
	std::ifstream input(path, std::ios::binary);
	Require(input.is_open(), "cannot open native direction math fixture");
	const auto header = ReadRecord<8>(input);
	Require(header[0] == 'M' && header[1] == 'D' && header[2] == 'A' && header[3] == 'T', "invalid native direction math signature");
	const auto count = ReadWord(header, 4);
	Require(count > 0 && count <= 8192, "native direction work exceeds its bound");
	for (std::uint32_t index = 0; index < count; ++index) {
		const auto record = ReadRecord<12>(input);
		const auto actual = sgre::scene::EvaluateMotionDirectionRadians(std::bit_cast<float>(ReadWord(record, 0)), std::bit_cast<float>(ReadWord(record, 4)));
		Require(std::bit_cast<std::uint32_t>(actual) == ReadWord(record, 8), "recovered scalar/SSE atan2 word differs from original native");
	}

	Require(input.peek() == std::char_traits<char>::eof(), "native direction fixture has trailing data");
}

int main(int argc, char** argv) {
	try {
		Require(argc == 1 || argc == 2, "usage: motion_direction_math_tests [native-direction-math-fixture]");
		TestIndependentDirections();
		if (argc == 2) {
			ReplayNative(argv[1]);
		}

		std::cout << "motion direction math tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
