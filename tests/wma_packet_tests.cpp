#include "sgre/audio/wma_packet.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static void TestMsbBitOrder() {
	const std::array<std::uint8_t, 5> bytes = {0xb2, 0x69, 0x87, 0x65, 0x43};
	sgre::audio::WmaBitReader reader(bytes);
	Require(reader.PeekBits(3) == 5 && reader.PositionBits() == 0, "MSB peek changed stream position");
	Require(reader.ReadBits(3) == 5 && reader.ReadBits(7) == 73 && reader.ReadBits(6) == 41, "cross-byte MSB bit order differs from independent binary vector");
	Require(reader.ReadBits(0) == 0 && reader.RemainingBits() == 24, "zero-width read changed position");
	reader.SkipBits(4);
	Require(reader.ReadBits(20) == 0x76543 && reader.RemainingBits() == 0, "unaligned twenty-bit value differs");

	const std::array<std::uint8_t, 4> full = {0xfe, 0xdc, 0xba, 0x98};
	sgre::audio::WmaBitReader wide(full);
	Require(wide.ReadBits(32) == 0xfedcba98, "32-bit unsigned read differs");
}

static void TestBitFailuresDoNotConsume() {
	const std::array<std::uint8_t, 1> bytes = {0x80};
	sgre::audio::WmaBitReader reader(bytes);
	reader.SkipBits(7);
	for (const auto width : {2, 33}) {
		bool rejected = false;
		try {
			static_cast<void>(reader.ReadBits(static_cast<std::uint8_t>(width)));
		} catch (const std::runtime_error&) {
			rejected = true;
		}

		Require(rejected && reader.PositionBits() == 7, "failed bit read consumed input or accepted invalid width");
	}

	bool rejected = false;
	try {
		reader.SkipBits(2);
	} catch (const std::runtime_error&) {
		rejected = true;
	}

	Require(rejected && reader.PositionBits() == 7, "failed skip consumed input");
}

static sgre::resource::WaveFormat Format(std::uint16_t channels = 1) {
	sgre::resource::WaveFormat format;
	format.Encoding = sgre::resource::WaveEncoding::WmaStandard;
	format.Channels = channels;
	format.BitsPerSample = 16;
	format.BlockAlignment = 2;

	return format;
}

static void TestPacketLedger() {
	const std::array<std::uint8_t, 4> bytes = {0x03, 0xaa, 0x14, 0xbb};
	const std::array<std::uint32_t, 2> cumulative = {512, 1280};
	const auto headers = sgre::audio::ReadWmaPacketHeaders(Format(), bytes, cumulative);
	Require(headers.size() == 2 && headers[0].Sequence == 0 && headers[1].Sequence == 1 && headers[0].FrameCountField == 3 && headers[1].FrameCountField == 4, "synthetic packet sequence/frame prefixes differ");
	Require(headers[0].DecodedSampleCount == 256 && headers[1].DecodedSampleCount == 384, "decoded-byte ledger was mistaken for frame count");
	const auto stereo = sgre::audio::ReadWmaPacketHeaders(Format(2), bytes, cumulative);
	Require(stereo[0].DecodedSampleCount == 128 && stereo[1].DecodedSampleCount == 192, "channel count was omitted from the decoded-byte conversion");
}

static void TestPacketBounds() {
	const std::array<std::uint8_t, 4> bytes = {0x01, 0, 0x11, 0};
	for (const auto& cumulative : {std::vector<std::uint32_t>{512}, std::vector<std::uint32_t>{512, 510}, std::vector<std::uint32_t>{512, 513}}) {
		bool rejected = false;
		try {
			static_cast<void>(sgre::audio::ReadWmaPacketHeaders(Format(), bytes, cumulative));
		} catch (const std::runtime_error&) {
			rejected = true;
		}

		Require(rejected, "invalid packet ledger was accepted");
	}

	bool rejected = false;
	try {
		static_cast<void>(sgre::audio::ReadWmaPacketHeaders(Format(), bytes, std::array<std::uint32_t, 2>{512, 1024}, 1));
	} catch (const std::runtime_error&) {
		rejected = true;
	}

	Require(rejected, "packet allocation bound was ignored");
	rejected = false;
	try {
		static_cast<void>(sgre::audio::ReadWmaPacketHeaders(Format(), std::array<std::uint8_t, 4>{0x01, 0, 0x21, 0}, std::array<std::uint32_t, 2>{512, 1024}));
	} catch (const std::runtime_error&) {
		rejected = true;
	}

	Require(rejected, "packet sequence discontinuity was accepted");
	const std::array<std::uint8_t, 2> zero_frame_field = {0x00, 0};
	Require(sgre::audio::ReadWmaPacketHeaders(Format(), zero_frame_field, std::array<std::uint32_t, 1>{0}).front().FrameCountField == 0, "zero frame-count field was rejected");
}

static void TestHuffmanPrefixes() {
	const std::array<sgre::audio::WmaHuffmanCode, 4> codes = {
	    sgre::audio::WmaHuffmanCode{10, 0, 1},
	    sgre::audio::WmaHuffmanCode{20, 2, 2},
	    sgre::audio::WmaHuffmanCode{30, 6, 3},
	    sgre::audio::WmaHuffmanCode{40, 7, 3}};
	const sgre::audio::WmaHuffmanDecoder decoder(codes);
	const std::array<std::uint8_t, 2> bytes = {0x5b, 0x80};
	sgre::audio::WmaBitReader reader(bytes);
	for (const auto expected : {10, 20, 30, 40}) {
		Require(decoder.DecodeSymbol(reader) == static_cast<std::uint32_t>(expected), "independent Huffman prefix symbol differs");
	}

	Require(reader.PositionBits() == 9, "Huffman decoder consumed padding or table lookahead");
	const std::array<std::uint8_t, 1> truncated = {0xff};
	sgre::audio::WmaBitReader partial(truncated);
	partial.SkipBits(6);
	bool rejected = false;
	try {
		static_cast<void>(decoder.DecodeSymbol(partial));
	} catch (const std::runtime_error&) {
		rejected = true;
	}

	Require(rejected && partial.PositionBits() == 6, "truncated Huffman symbol consumed input");
	const sgre::audio::WmaHuffmanDecoder incomplete(std::array<sgre::audio::WmaHuffmanCode, 1>{sgre::audio::WmaHuffmanCode{10, 0, 1}});
	sgre::audio::WmaBitReader missing(truncated);
	rejected = false;
	try {
		static_cast<void>(incomplete.DecodeSymbol(missing));
	} catch (const std::runtime_error&) {
		rejected = true;
	}

	Require(rejected && missing.PositionBits() == 0, "missing Huffman branch consumed input");
}

static void TestHuffmanValidation() {
	const std::vector<std::vector<sgre::audio::WmaHuffmanCode>> invalid = {
	    {},
	    {sgre::audio::WmaHuffmanCode{1, 0, 0}},
	    {sgre::audio::WmaHuffmanCode{1, 0, 33}},
	    {sgre::audio::WmaHuffmanCode{1, 2, 1}},
	    {sgre::audio::WmaHuffmanCode{1, 0, 1}, sgre::audio::WmaHuffmanCode{2, 0, 1}},
	    {sgre::audio::WmaHuffmanCode{1, 0, 1}, sgre::audio::WmaHuffmanCode{2, 0, 2}},
	    {sgre::audio::WmaHuffmanCode{1, 0, 2}, sgre::audio::WmaHuffmanCode{2, 0, 1}}};
	for (const auto& codes : invalid) {
		bool rejected = false;
		try {
			const sgre::audio::WmaHuffmanDecoder decoder(codes);
		} catch (const std::runtime_error&) {
			rejected = true;
		}

		Require(rejected, "invalid Huffman alphabet or prefix collision was accepted");
	}

	bool rejected = false;
	try {
		const sgre::audio::WmaHuffmanDecoder decoder(std::array<sgre::audio::WmaHuffmanCode, 1>{sgre::audio::WmaHuffmanCode{1, 0, 32}}, 32);
	} catch (const std::runtime_error&) {
		rejected = true;
	}

	Require(rejected, "Huffman node allocation limit was ignored");
	const sgre::audio::WmaHuffmanDecoder wide(std::array<sgre::audio::WmaHuffmanCode, 1>{sgre::audio::WmaHuffmanCode{1234, 0x89abcdef, 32}});
	const std::array<std::uint8_t, 4> bytes = {0x89, 0xab, 0xcd, 0xef};
	sgre::audio::WmaBitReader reader(bytes);
	Require(wide.DecodeSymbol(reader) == 1234 && reader.PositionBits() == 32, "32-bit Huffman symbol differs");
}

static void TestExponentDeltas() {
	const sgre::audio::WmaHuffmanDecoder decoder(sgre::audio::WmaExponentCodebook());
	const std::array<std::uint8_t, 5> bytes = {0, 0x3f, 0xfe, 0x87, 0xff};
	sgre::audio::WmaBitReader reader(bytes);
	Require(sgre::audio::DecodeWmaExponentDelta(reader, decoder) == 0 && reader.PositionBits() == 1, "one-bit zero exponent delta differs");
	const std::array<std::uint8_t, 3> minimum = {0xff, 0xfa, 0};
	sgre::audio::WmaBitReader low(minimum);
	Require(sgre::audio::DecodeWmaExponentDelta(low, decoder) == -60 && low.PositionBits() == 18, "minimum exponent delta differs from native bit vector");
	const std::array<std::uint8_t, 3> maximum = {0xff, 0xfe, 0x60};
	sgre::audio::WmaBitReader high(maximum);
	Require(sgre::audio::DecodeWmaExponentDelta(high, decoder) == 60 && high.PositionBits() == 19, "maximum exponent delta differs from native bit vector");
	const sgre::audio::WmaHuffmanDecoder wrong(std::array<sgre::audio::WmaHuffmanCode, 1>{sgre::audio::WmaHuffmanCode{121, 0, 1}});
	bool rejected = false;
	try {
		static_cast<void>(sgre::audio::DecodeWmaExponentDelta(reader, wrong));
	} catch (const std::runtime_error&) {
		rejected = true;
	}

	Require(rejected && reader.PositionBits() == 1, "invalid exponent alphabet consumed input");
}

int main() {
	try {
		TestMsbBitOrder();
		TestBitFailuresDoNotConsume();
		TestPacketLedger();
		TestPacketBounds();
		TestHuffmanPrefixes();
		TestHuffmanValidation();
		TestExponentDeltas();
		std::cout << "WMA packet tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
