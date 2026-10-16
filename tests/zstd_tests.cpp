#include "sgre/resource/zstd_decoder.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace sgre::resource::test {

	static constexpr std::size_t kTestAllocationLimit = 8 * 1024 * 1024;

	static std::vector<std::uint8_t> FromHex(std::string_view hex) {
		if (hex.size() % 2 != 0) {
			throw std::runtime_error("test fixture has an odd hex length");
		}

		std::vector<std::uint8_t> bytes;
		for (std::size_t index = 0; index < hex.size(); index += 2) {
			const auto value = std::stoul(std::string(hex.substr(index, 2)), nullptr, 16);
			bytes.push_back(static_cast<std::uint8_t>(value));
		}

		return bytes;
	}

	static std::vector<std::uint8_t> RepeatedPhrase(std::size_t count) {
		const std::string_view phrase = "Portable resource decoding. ";
		std::vector<std::uint8_t> bytes;
		for (std::size_t index = 0; index < count; ++index) {
			bytes.insert(bytes.end(), phrase.begin(), phrase.end());
		}

		return bytes;
	}

	static void CheckDecode(std::string_view name, std::span<const std::uint8_t> encoded, const std::vector<std::uint8_t>& expected) {
		const auto actual = DecodeZstd(encoded, expected.size(), kTestAllocationLimit);
		if (actual != expected) {
			throw std::runtime_error(std::string(name) + " decoded bytes differ");
		}
	}

	static void CheckReject(std::string_view name, std::span<const std::uint8_t> encoded, std::size_t expected_size, std::size_t allocation_limit = kTestAllocationLimit) {
		try {
			static_cast<void>(DecodeZstd(encoded, expected_size, allocation_limit));
		} catch (const std::runtime_error&) {
			return;
		}

		throw std::runtime_error(std::string(name) + " malformed input was accepted");
	}

	static void TestBlockForms() {
		const auto raw = FromHex("28b52ffd200529000068656c6c6f");
		CheckDecode("raw block", raw, std::vector<std::uint8_t>{'h', 'e', 'l', 'l', 'o'});
		CheckDecode("RLE block", FromHex("28b52ffd20052b000041"), std::vector<std::uint8_t>(5, 'A'));
		CheckDecode("empty frame", FromHex("28b52ffd2000010000"), std::vector<std::uint8_t>{});

		// Hand-encoded weights 4,3,2,0,1 and inferred 1 describe six symbols.
		CheckDecode("direct Huffman weights", FromHex("28b52ffd000055000042800184432010100d00"), std::vector<std::uint8_t>{0, 1, 5, 4});

		auto concatenated = raw;
		concatenated.insert(concatenated.end(), raw.begin(), raw.end());
		CheckDecode("concatenated frames", concatenated, std::vector<std::uint8_t>{'h', 'e', 'l', 'l', 'o', 'h', 'e', 'l', 'l', 'o'});

		auto skippable = FromHex("502a4d1803000000616263");
		skippable.insert(skippable.end(), raw.begin(), raw.end());
		CheckDecode("skippable frame", skippable, std::vector<std::uint8_t>{'h', 'e', 'l', 'l', 'o'});
	}

	static void TestIndependentCompressionVectors() {
		// These synthetic vectors were encoded by a local zstd CLI; no game bytes are included.
		const auto phrase = FromHex("28b52ffd0458250100e0506f727461626c65207265736f75726365206465636f64696e672e200100c4f9ff740214b9fdbd");
		CheckDecode("predefined FSE sequences and checksum", phrase, RepeatedPhrase(60));

		const auto blocks = FromHex("28b52ffd0458240100e0506f727461626c65207265736f75726365206465636f64696e672e20010084ff7f3e9d440000000100fdff9f4f204500000001007d209f4f2023e2efc0");
		CheckDecode("multiple blocks and previous history", blocks, RepeatedPhrase(12000));

		const auto weights = FromHex("28b52ffd045815050082081f2290d5d8752d0138e070b303cc003003c0e8ffffffffbf2572d3a64d9b2b7b73e54e0113a96815e3f677c4c42468c0c9a4ca42d6821978c06dfae212d8880839500087711e8c620d51fed1b8935270fbc7827b70571378304a8dbf54b48a715b5ece9314e37e91384f52abb9071953d1aee69eff41d12ec6ed2f050118a810103830d6fe1ba033f4102086a20fffbd3b11e9fb565831aac22d623308b26753c24e290b");
		std::vector<std::uint8_t> expected_weights;
		for (std::size_t index = 0; index < 320; ++index) {
			const auto value = index % 11 != 0 ? (index * 17 + index / 7) % 19 : 128 + index % 71;
			expected_weights.push_back(static_cast<std::uint8_t>(value));
		}

		CheckDecode("FSE-compressed Huffman weights and four streams", weights, expected_weights);
		CheckDecode("zero run sequence", FromHex("28b52ffd045845000008000100e4a99c105a074479"), std::vector<std::uint8_t>(1000, 0));

		auto wrong_checksum = phrase;
		wrong_checksum.back() ^= 1;
		CheckReject("checksum mismatch", wrong_checksum, 1680);
	}

	static void TestInvalidInputs() {
		const auto raw = FromHex("28b52ffd200529000068656c6c6f");
		for (std::size_t length = 0; length < raw.size(); ++length) {
			CheckReject("truncated frame", std::span<const std::uint8_t>(raw).first(length), 5);
		}

		CheckReject("output limit", raw, 5, 4);
		CheckReject("expected size mismatch", raw, 6);
		CheckReject("smaller expected size", raw, 4);
		CheckReject("reserved descriptor", FromHex("28b52ffd2800010000"), 0);
		CheckReject("reserved block", FromHex("28b52ffd2000070000"), 0);
		CheckReject("unsupported dictionary", FromHex("28b52ffd21010529000068656c6c6f"), 5);
		CheckReject("oversized frame window", FromHex("28b52ffd00f8010000"), 0);
		CheckReject("invalid entropy marker", FromHex("28b52ffd00003500000001000000"), 3);
		CheckReject("treeless Huffman without table", FromHex("28b52ffd000025000003000000"), 0);
		CheckReject("truncated skippable frame", FromHex("502a4d180500000061"), 0);
	}

	void RunZstdTests() {
		TestBlockForms();
		TestIndependentCompressionVectors();
		TestInvalidInputs();
	}

} // namespace sgre::resource::test

int main() {
	try {
		sgre::resource::test::RunZstdTests();
		std::cout << "Zstandard tests passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}

	return 0;
}
