#include "sgre/resource/resource_cipher.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static void TestStreamOffsets() {
	const sgre::resource::ResourceCipher cipher("folder/TEST.MZS", "synthetic-seed", 131);
	std::vector<std::uint8_t> full(1024, 0);
	cipher.Transform(full);
	const std::vector<std::uint8_t> prefix = {0x18, 0xc1, 0xd7, 0x4b, 0xae, 0xc8, 0xd9, 0xb5, 0x99, 0xb4, 0xf7, 0xce, 0x76, 0x2c, 0xbe, 0x9b};
	Require(std::equal(prefix.begin(), prefix.end(), full.begin()), "independent stream vector differs");

	std::vector<std::uint8_t> pieces(1024, 0);
	for (std::size_t offset = 0; offset < pieces.size(); offset += 37) {
		const auto length = std::min<std::size_t>(37, pieces.size() - offset);
		cipher.Transform(std::span<std::uint8_t>(pieces).subspan(offset, length), offset);
	}

	Require(full == pieces, "chunked transform changed stream phase");
	cipher.Transform(pieces);
	Require(std::all_of(pieces.begin(), pieces.end(), [](std::uint8_t value) {
		return value == 0;
	}),
	    "cipher was not reversible");

	std::vector<std::uint8_t> high_offset(3, 0);
	cipher.Transform(high_offset, std::numeric_limits<std::uint64_t>::max());
	const auto start = static_cast<std::size_t>(std::numeric_limits<std::uint64_t>::max() % 131);
	for (std::size_t index = 0; index < high_offset.size(); ++index) {
		Require(high_offset[index] == full[(start + index) % 131], "large stream offset narrowed or overflowed");
	}
}

static void TestMtTwistBoundary() {
	const sgre::resource::ResourceCipher cipher("test.mzs", "synthetic-seed", 4096);
	std::vector<std::uint8_t> stream(4096, 0);
	cipher.Transform(stream);
	// Independently generated with NumPy RandomState's init_by_array and hashlib MD5.
	const std::vector<std::uint8_t> boundary{0x79, 0x2f, 0xeb, 0x55, 0xa9, 0x1a, 0x46, 0x8b, 0xce, 0x10, 0x8c, 0xe5, 0x65, 0xd5, 0x9c, 0x31};
	const std::vector<std::uint8_t> tail{0xb2, 0x79, 0x46, 0xe5, 0x36, 0x2f, 0x64, 0x6d, 0xae, 0xb5, 0xdf, 0xfb, 0x16, 0xc5, 0xcf, 0xb5};
	Require(std::equal(boundary.begin(), boundary.end(), stream.begin() + 2492), "MT twist boundary differs from independent vector");
	Require(std::equal(tail.begin(), tail.end(), stream.begin() + 4080), "4096-byte key tail differs from independent vector");
}

static void TestInvalidPeriods() {
	for (const std::size_t period : {std::size_t{0}, std::size_t{1024 * 1024 + 1}}) {
		bool rejected = false;
		try {
			const sgre::resource::ResourceCipher invalid("test", "seed", period);
		} catch (const std::runtime_error&) {
			rejected = true;
		}

		Require(rejected, "invalid period was accepted");
	}
}

int main() {
	try {
		TestStreamOffsets();
		TestInvalidPeriods();
		TestMtTwistBoundary();
		std::cout << "resource cipher tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
