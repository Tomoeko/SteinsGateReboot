#include "sgre/audio/wma_block.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static sgre::audio::WmaStandardParameters Parameters(std::uint16_t options = 31) {
	sgre::resource::WaveFormat format;
	format.Encoding = sgre::resource::WaveEncoding::WmaStandard;
	format.Channels = 1;
	format.SamplesPerSecond = 44100;
	format.AverageBytesPerSecond = 4000;
	format.BlockAlignment = 1487;
	format.BitsPerSample = 16;

	return sgre::audio::DescribeWmaStandard(format, options);
}

static std::array<std::uint8_t, 8> EscapeVector(std::uint32_t zero_run) {
	const std::uint64_t packed = (std::uint64_t{0x3e6} << 53) | (std::uint64_t{7} << 44) | (static_cast<std::uint64_t>(zero_run) << 37) | (std::uint64_t{1} << 36) | (std::uint64_t{0xf6} << 27);
	std::array<std::uint8_t, 8> bytes = {};
	for (std::size_t index = 0; index < bytes.size(); ++index) {
		bytes[index] = static_cast<std::uint8_t>(packed >> ((7 - index) * 8));
	}

	return bytes;
}

static void TestBlockHistory() {
	const std::array<std::uint8_t, 2> bytes = {0x2e, 0x00};
	sgre::audio::WmaBitReader reader(bytes);
	const auto parameters = Parameters();
	const auto history = sgre::audio::ReadWmaBlockHistory(reader, parameters);
	Require(history.PreviousCoefficients == 1024 && history.CurrentCoefficients == 256 && reader.PositionBits() == 6, "native previous/current block lengths differ");
	Require(sgre::audio::ReadWmaNextBlockSize(reader, parameters) == 128 && reader.PositionBits() == 9, "native next-block length differs");
	const std::array<std::uint8_t, 0> empty = {};
	sgre::audio::WmaBitReader fixed_reader(empty);
	const auto fixed = sgre::audio::ReadWmaBlockHistory(fixed_reader, Parameters(1));
	Require(fixed.PreviousCoefficients == 2048 && fixed.CurrentCoefficients == 2048 && fixed_reader.PositionBits() == 0, "fixed WMA history consumed variable-length fields");
	for (const auto index : std::array<std::uint8_t, 3>{5, 6, 7}) {
		const std::array<std::uint8_t, 1> invalid = {static_cast<std::uint8_t>(index << 2)};
		sgre::audio::WmaBitReader rejected_reader(invalid);
		bool rejected = false;
		try {
			static_cast<void>(sgre::audio::ReadWmaBlockHistory(rejected_reader, parameters));
		} catch (const std::runtime_error&) {
			rejected = true;
		}

		Require(rejected && rejected_reader.PositionBits() == 0, "invalid WMA current-block index consumed history input");
	}
}

static void TestSparseValues() {
	const sgre::audio::WmaCoefficientDecoder decoder(sgre::audio::WmaCoefficientBook::SetTwoMain);
	sgre::audio::WmaCoefficientEscapeWidths widths;
	widths.MagnitudeBits = 9;
	widths.ZeroRunBits = 7;
	// Native ordinary prefixes/signs: 001, 0100, 01101; end marker 011110110.
	const std::array<std::uint8_t, 3> bytes = {0x28, 0xd7, 0xb0};
	sgre::audio::WmaBitReader reader(bytes);
	const auto values = sgre::audio::DecodeWmaCoefficientValues(reader, decoder, widths, 128);
	Require(values.size() == 3 && values[0].Position == 0 && values[1].Position == 2 && values[2].Position == 5 && reader.PositionBits() == 21, "native sparse run placement or end-marker extent differs");
	Require(values[0].Value.Magnitude == 1 && !values[0].Value.IsNegative && values[1].Value.Magnitude == 1 && values[1].Value.IsNegative && values[2].Value.Magnitude == 1 && !values[2].Value.IsNegative, "native sparse coefficient values or signs differ");
	for (const auto position : std::array<std::uint32_t, 5>{0, 1, 31, 126, 127}) {
		const auto encoded = EscapeVector(position);
		sgre::audio::WmaBitReader escaped(encoded);
		const auto placed = sgre::audio::DecodeWmaCoefficientValues(escaped, decoder, widths, 128);
		Require(placed.size() == 1 && placed.front().Position == position && placed.front().Value.Magnitude == 7 && !placed.front().Value.IsNegative, "native escape run/magnitude placement differs");
		Require(escaped.PositionBits() == (position == 127 ? 28 : 37), "native fully populated block consumed an unnecessary end marker");
	}

	const std::array<std::uint8_t, 1> truncated = {0x20};
	sgre::audio::WmaBitReader incomplete(truncated);
	bool rejected = false;
	try {
		static_cast<void>(sgre::audio::DecodeWmaCoefficientValues(incomplete, decoder, widths, 128));
	} catch (const std::runtime_error&) {
		rejected = true;
	}

	Require(rejected && incomplete.PositionBits() == 0, "truncated sparse coefficient block consumed input");
	const auto outside = EscapeVector(127);
	sgre::audio::WmaBitReader excessive(outside);
	rejected = false;
	try {
		static_cast<void>(sgre::audio::DecodeWmaCoefficientValues(excessive, decoder, widths, 127));
	} catch (const std::runtime_error&) {
		rejected = true;
	}

	Require(rejected && excessive.PositionBits() == 0, "run beyond the coded coefficient count was accepted");
}

int main() {
	try {
		TestBlockHistory();
		TestSparseValues();
		std::cout << "WMA block tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
