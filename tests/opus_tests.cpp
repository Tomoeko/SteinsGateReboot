#include "sgre/media/opus.hpp"

#include <iostream>
#include <stdexcept>

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static void TestHeader() {
	const std::vector<std::uint8_t> bytes{'O', 'p', 'u', 's', 'H', 'e', 'a', 'd', 1, 2, 0x38, 1, 0x80, 0xbb, 0, 0, 0, 0xff, 0};
	const auto header = sgre::media::ReadOpusHeader(bytes);
	Require(header.Channels == 2 && header.PreSkipSamples == 312 && header.OriginalSamplesPerSecond == 48000 && header.OutputGainQ8Decibels == -256, "independent Opus identification fields differ");
	for (std::size_t length = 0; length < bytes.size(); ++length) {
		bool rejected = false;
		try {
			static_cast<void>(sgre::media::ReadOpusHeader(std::span<const std::uint8_t>(bytes).first(length)));
		} catch (const std::runtime_error&) {
			rejected = true;
		}

		Require(rejected, "truncated Opus identification was accepted");
	}
}

static void TestFraming() {
	const auto single = sgre::media::ReadOpusPacket(std::vector<std::uint8_t>{0xfc, 0xff, 0xfe});
	Require(single.Stereo && single.Mode == sgre::media::OpusMode::Celt && single.Bandwidth == sgre::media::OpusBandwidth::Full && single.SamplesPerFrame == 960 && single.TotalSamples == 960 && single.Frames.size() == 1 && single.Frames.front().Offset == 1 && single.Frames.front().Size == 2, "CELT one-frame framing differs");
	const auto equal = sgre::media::ReadOpusPacket(std::vector<std::uint8_t>{0xf9, 1, 2, 3, 4});
	Require(equal.TotalSamples == 1920 && equal.Frames.size() == 2 && equal.Frames[0].Size == 2 && equal.Frames[1].Offset == 3, "two-frame CBR framing differs");
	const auto variable = sgre::media::ReadOpusPacket(std::vector<std::uint8_t>{0xfa, 1, 10, 20, 30});
	Require(variable.Frames[0].Offset == 2 && variable.Frames[0].Size == 1 && variable.Frames[1].Offset == 3 && variable.Frames[1].Size == 2, "two-frame VBR framing differs");
	const auto multiple = sgre::media::ReadOpusPacket(std::vector<std::uint8_t>{0xfb, 0xc3, 2, 1, 2, 10, 20, 21, 30, 0, 0});
	Require(multiple.TotalSamples == 2880 && multiple.Frames.size() == 3 && multiple.Frames[0].Offset == 5 && multiple.Frames[1].Size == 2 && multiple.Frames[2].Size == 1, "padded multi-frame VBR framing differs");
	const auto silk = sgre::media::ReadOpusPacket(std::vector<std::uint8_t>{0x18, 1});
	Require(silk.Mode == sgre::media::OpusMode::Silk && silk.TotalSamples == 2880, "SILK duration differs");
	const auto hybrid = sgre::media::ReadOpusPacket(std::vector<std::uint8_t>{0x68, 1});
	Require(hybrid.Mode == sgre::media::OpusMode::Hybrid && hybrid.Bandwidth == sgre::media::OpusBandwidth::SuperWide && hybrid.TotalSamples == 960, "hybrid duration/bandwidth differs");
}

static void TestMalformed() {
	for (const auto& bytes : std::vector<std::vector<std::uint8_t>>{{}, {0xf9, 1}, {0xfa}, {0xfb}, {0xfb, 0}, {0xfb, 7}, {0xfb, 0x41, 255}, {0xfa, 252}, {0xfa, 100, 1}}) {
		bool rejected = false;
		try {
			static_cast<void>(sgre::media::ReadOpusPacket(bytes));
		} catch (const std::runtime_error&) {
			rejected = true;
		}

		Require(rejected, "malformed Opus framing was accepted");
	}
}

int main() {
	try {
		TestHeader();
		TestFraming();
		TestMalformed();
		std::cout << "Opus packet tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
