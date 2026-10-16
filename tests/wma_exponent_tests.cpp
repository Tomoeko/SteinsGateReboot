#include "sgre/audio/wma_exponent.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static void TestNativeBandVectors() {
	const std::vector<std::uint32_t> full_44100 = {
	    0,
	    8,
	    20,
	    28,
	    36,
	    48,
	    60,
	    72,
	    84,
	    100,
	    116,
	    136,
	    160,
	    184,
	    216,
	    252,
	    292,
	    344,
	    408,
	    492,
	    596,
	    716,
	    884,
	    1116,
	    1440,
	    2048};
	const std::vector<std::uint32_t> full_48000 = {
	    0,
	    8,
	    16,
	    24,
	    36,
	    44,
	    52,
	    64,
	    80,
	    92,
	    108,
	    128,
	    148,
	    172,
	    196,
	    232,
	    268,
	    316,
	    376,
	    452,
	    548,
	    656,
	    812,
	    1024,
	    1324,
	    2048};
	const std::vector<std::uint32_t> short_512 = {
	    0,
	    4,
	    12,
	    20,
	    24,
	    36,
	    48,
	    56,
	    64,
	    88,
	    104,
	    124,
	    148,
	    180,
	    220,
	    280,
	    360,
	    512};
	Require(sgre::audio::WmaExponentBandEnds(44100, 2048) == full_44100, "native 44.1 kHz full-block exponent boundaries differ");
	Require(sgre::audio::WmaExponentBandEnds(48000, 2048) == full_48000, "native 48 kHz full-block exponent boundaries differ");
	Require(sgre::audio::WmaExponentBandEnds(44100, 512) == short_512 && sgre::audio::WmaExponentBandEnds(48000, 512) == short_512, "native Standard short-block boundaries differ");
	for (const auto rate : std::array<std::uint32_t, 2>{44100, 48000}) {
		for (const auto size : std::array<std::uint32_t, 5>{128, 256, 512, 1024, 2048}) {
			const auto ends = sgre::audio::WmaExponentBandEnds(rate, size);
			Require(ends.front() == 0 && ends.back() == static_cast<std::uint32_t>(size), "exponent bands do not cover exactly one coefficient block");
			for (std::size_t index = 1; index < ends.size(); ++index) {
				Require(ends[index] > ends[index - 1] && ends[index] % 4 == 0, "exponent bands are not increasing multiples of four");
			}
		}
	}
}

static void TestExponentValues() {
	const sgre::audio::WmaHuffmanDecoder decoder(sgre::audio::WmaExponentCodebook());
	// Native symbols 60, 61, 59, 60 have prefixes 0, 1010, 100, 0 respectively.
	const std::array<std::uint8_t, 2> bytes = {0x54, 0x00};
	sgre::audio::WmaBitReader reader(bytes);
	const auto values = sgre::audio::DecodeWmaExponentValues(reader, decoder, 4);
	Require(values.Bands == std::vector<std::int32_t>{36, 37, 36, 36} && values.MaximumExponent == 37 && reader.PositionBits() == 9, "native exponent starting value or accumulated deltas differ");
	const std::array<std::uint8_t, 1> truncated = {0x57};
	sgre::audio::WmaBitReader incomplete(truncated);
	bool rejected = false;
	try {
		static_cast<void>(sgre::audio::DecodeWmaExponentValues(incomplete, decoder, 4));
	} catch (const std::runtime_error&) {
		rejected = true;
	}

	Require(rejected && incomplete.PositionBits() == 0, "incomplete exponent block consumed a partial prefix");
	const std::array<std::uint8_t, 4> zeros = {};
	sgre::audio::WmaBitReader silent(zeros);
	const auto constant = sgre::audio::DecodeWmaExponentValues(silent, decoder, 25);
	Require(constant.Bands.size() == 25 && constant.MaximumExponent == 36 && silent.PositionBits() == 25, "maximum-band constant exponent vector differs");
}

static void TestBounds() {
	for (const auto rate : std::array<std::uint32_t, 4>{0, 32000, 44099, 48001}) {
		bool rejected = false;
		try {
			static_cast<void>(sgre::audio::WmaExponentBandEnds(rate, 2048));
		} catch (const std::runtime_error&) {
			rejected = true;
		}

		Require(rejected, "unrecovered exponent sample-rate profile was accepted");
	}

	for (const auto size : std::array<std::uint32_t, 5>{0, 64, 129, 768, 4096}) {
		bool rejected = false;
		try {
			static_cast<void>(sgre::audio::WmaExponentBandEnds(44100, size));
		} catch (const std::runtime_error&) {
			rejected = true;
		}

		Require(rejected, "invalid exponent block coefficient extent was accepted");
	}

	const sgre::audio::WmaHuffmanDecoder decoder(sgre::audio::WmaExponentCodebook());
	for (const auto count : std::array<std::size_t, 2>{0, 26}) {
		const std::array<std::uint8_t, 4> bytes = {};
		sgre::audio::WmaBitReader reader(bytes);
		bool rejected = false;
		try {
			static_cast<void>(sgre::audio::DecodeWmaExponentValues(reader, decoder, count));
		} catch (const std::runtime_error&) {
			rejected = true;
		}

		Require(rejected && reader.PositionBits() == 0, "invalid exponent band count was accepted or consumed input");
	}
}

int main() {
	try {
		TestNativeBandVectors();
		TestExponentValues();
		TestBounds();
		std::cout << "WMA exponent tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
