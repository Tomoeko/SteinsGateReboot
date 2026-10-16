#include "sgre/core/md5.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static std::string Hex(const std::array<std::uint8_t, 16>& digest) {
	static constexpr std::string_view kDigits = "0123456789abcdef";
	std::string result;
	for (const auto byte : digest) {
		result.push_back(kDigits[byte >> 4]);
		result.push_back(kDigits[byte & 15]);
	}

	return result;
}

static void TestRfcVectors() {
	// Independent RFC1321 sectionA.5 digests; no reference implementation is imported.
	const std::array<std::pair<std::string_view, std::string_view>, 7> vectors{{
	    {"", "d41d8cd98f00b204e9800998ecf8427e"},
	    {"a", "0cc175b9c0f1b6a831c399e269772661"},
	    {"abc", "900150983cd24fb0d6963f7d28e17f72"},
	    {"message digest", "f96b697d7cb7938d525a2f31aaf161d0"},
	    {"abcdefghijklmnopqrstuvwxyz", "c3fcd3d76192e4007dfb496cca67e13b"},
	    {"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789", "d174ab98d277d9f5a5611c2c9f419d9f"},
	    {"12345678901234567890123456789012345678901234567890123456789012345678901234567890", "57edf4a22be3c955ac49da2e2107b67a"},
	}};
	for (const auto& [message, expected] : vectors) {
		sgre::core::Md5 digest;
		digest.Update(message);
		Require(Hex(digest.Finalize()) == expected && Hex(digest.Finalize()) == expected, "RFC1321 digest/finalization differs");
		bool rejected = false;
		try {
			digest.Update(std::string_view("more"));
		} catch (const std::runtime_error&) {
			rejected = true;
		}
		Require(rejected, "finalized MD5 accepted additional input");
	}
}

static void TestBinaryAndPaddingBoundaries() {
	// Independent Python hashlib digests for a binary sequence containing every possible byte.
	const std::array<std::pair<std::size_t, std::string_view>, 10> vectors{{
	    {0, "d41d8cd98f00b204e9800998ecf8427e"},
	    {1, "83acb6e67e50e31db6ed341dd2de1595"},
	    {55, "6710704312a69f80962a4c3f7ee8cfed"},
	    {56, "ec6927c402e0904ce18443b8fb7002ed"},
	    {63, "ec020055bab0d50d46e88f81b706ed0f"},
	    {64, "0d864e0ef14659e996fb6c5d608c444b"},
	    {65, "f82152185e1d5f3a84df535d13dfdab7"},
	    {119, "ba616108f1b38f5525679177100eff82"},
	    {120, "36d24f4ffd21a333de1eb2b4c7f9e483"},
	    {1024, "b3e11023b0e1d9930e8f857ea92a839c"},
	}};
	for (const auto& [count, expected] : vectors) {
		std::vector<std::uint8_t> bytes(count);
		for (std::size_t index = 0; index < count; ++index) {
			bytes[index] = static_cast<std::uint8_t>(index * 13 + 127);
		}
		for (const auto chunk : std::array<std::size_t, 5>{1, 7, 63, 64, 65}) {
			sgre::core::Md5 digest;
			for (std::size_t offset = 0; offset < count; offset += std::min(chunk, count - offset)) {
				digest.Update(std::span<const std::uint8_t>(bytes).subspan(offset, std::min(chunk, count - offset)));
			}
			Require(Hex(digest.Finalize()) == expected, "binary/chunk/padding MD5 differs from independent digest");
		}
	}
}

int main() {
	try {
		TestRfcVectors();
		TestBinaryAndPaddingBoundaries();
		std::cout << "MD5 RFC1321, binary/chunk/padding boundaries and finalized-state checks passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
