#include "sgre/audio/wma_stream.hpp"

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

static sgre::resource::WaveFormat VoiceFormat() {
	sgre::resource::WaveFormat format;
	format.Encoding = sgre::resource::WaveEncoding::WmaStandard;
	format.Channels = 1;
	format.SamplesPerSecond = 44100;
	format.AverageBytesPerSecond = 4000;
	format.BlockAlignment = 1487;
	format.BitsPerSample = 16;

	return format;
}

static void TestNativeParameterSnapshot() {
	const auto parameters = sgre::audio::DescribeWmaStandard(VoiceFormat(), 31);
	Require(parameters.FrameCoefficientCount == 2048 && parameters.FrameCoefficientBits == 11 && parameters.ByteOffsetBits == 9, "native Standard frame/byte-offset snapshot differs");
	Require(parameters.HasReservoir && parameters.HasVariableBlocks && parameters.HasVlcExponents && parameters.MaximumBlockCount == 16 && parameters.MinimumBlockCoefficients == 128 && parameters.BlockCountBits == 5 && parameters.BlockLengthIndexBits == 3, "native Standard option/block snapshot differs");
	Require(parameters.PacketHeaderBits == 20 && parameters.ContinuationBits == 12 && parameters.PacketBytes == 1487, "native Standard packet widths differ");
	const auto fixed = sgre::audio::DescribeWmaStandard(VoiceFormat(), 1);
	Require(!fixed.HasReservoir && !fixed.HasVariableBlocks && fixed.HasVlcExponents && fixed.MaximumBlockCount == 1 && fixed.MinimumBlockCoefficients == 2048 && fixed.PacketHeaderBits == 0, "fixed Standard block options differ");
	auto low = VoiceFormat();
	low.SamplesPerSecond = 16000;
	const auto short_frame = sgre::audio::DescribeWmaStandard(low, 31);
	Require(short_frame.FrameCoefficientCount == 512 && short_frame.MaximumBlockCount == 4 && short_frame.MinimumBlockCoefficients == 128, "low-rate native frame or block clamp differs");
	low.SamplesPerSecond = 22050;
	Require(sgre::audio::DescribeWmaStandard(low, 31).FrameCoefficientCount == 1024, "22.05 kHz native frame differs");
	low.SamplesPerSecond = 32000;
	low.AverageBytesPerSecond = 1992;
	Require(sgre::audio::DescribeWmaStandard(low, 31).ByteOffsetBits == 8, "below-rounding reservoir boundary differs");
	low.AverageBytesPerSecond = 1993;
	Require(sgre::audio::DescribeWmaStandard(low, 31).ByteOffsetBits == 9, "above-rounding reservoir boundary differs");
}

static void TestReservoirFields() {
	const auto parameters = sgre::audio::DescribeWmaStandard(VoiceFormat(), 31);
	std::vector<std::uint8_t> packet(parameters.PacketBytes);
	packet[0] = 0x14;
	packet[1] = 0x2d;
	packet[2] = 0x8f;
	const auto header = sgre::audio::ReadWmaReservoirHeader(packet, parameters);
	Require(header.Sequence == 1 && header.FrameCountField == 4 && header.ContinuationBitCount == 728 && header.HeaderBitCount == 20, "native reservoir packet one differs or consumed following payload bits");
	packet[0] = 0;
	packet[1] = 0;
	packet[2] = 0;
	const auto empty_count = sgre::audio::ReadWmaReservoirHeader(packet, parameters);
	Require(empty_count.Sequence == 0 && empty_count.FrameCountField == 0 && empty_count.ContinuationBitCount == 0, "zero-count reservoir packet was rejected");
	packet[0] = 0xbF;
	packet[1] = 0x0c;
	packet[2] = 0x70;
	const auto tail = sgre::audio::ReadWmaReservoirHeader(packet, parameters);
	Require(tail.Sequence == 11 && tail.FrameCountField == 15 && tail.ContinuationBitCount == 199, "native final packet prefix differs");
}

static void TestBounds() {
	auto format = VoiceFormat();
	for (const auto options : {32, 65535}) {
		bool rejected = false;
		try {
			static_cast<void>(sgre::audio::DescribeWmaStandard(format, static_cast<std::uint16_t>(options)));
		} catch (const std::runtime_error&) {
			rejected = true;
		}

		Require(rejected, "unrecovered Standard option bits were accepted");
	}

	format.BlockAlignment = 2;
	bool rejected = false;
	try {
		static_cast<void>(sgre::audio::DescribeWmaStandard(format, 31));
	} catch (const std::runtime_error&) {
		rejected = true;
	}

	Require(rejected, "reservoir header exceeding the declared packet was accepted");
	format = VoiceFormat();
	format.BlockAlignment = 3;
	const auto small = sgre::audio::DescribeWmaStandard(format, 31);
	const std::array<std::uint8_t, 3> excessive_continuation = {0x01, 0xff, 0xf0};
	rejected = false;
	try {
		static_cast<void>(sgre::audio::ReadWmaReservoirHeader(excessive_continuation, small));
	} catch (const std::runtime_error&) {
		rejected = true;
	}

	Require(rejected, "continuation exceeding this packet payload was accepted");
	rejected = false;
	try {
		static_cast<void>(sgre::audio::ReadWmaReservoirHeader(std::array<std::uint8_t, 2>{0, 0}, small));
	} catch (const std::runtime_error&) {
		rejected = true;
	}

	Require(rejected, "packet length mismatch was accepted");
	format = VoiceFormat();
	format.AverageBytesPerSecond = 1;
	rejected = false;
	try {
		static_cast<void>(sgre::audio::DescribeWmaStandard(format, 31));
	} catch (const std::runtime_error&) {
		rejected = true;
	}

	Require(rejected, "zero rounded reservoir-offset frame size was accepted");
}

int main() {
	try {
		TestNativeParameterSnapshot();
		TestReservoirFields();
		TestBounds();
		std::cout << "WMA stream tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
