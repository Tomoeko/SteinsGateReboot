#include "sgre/media/video.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <utility>

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static void TestIndependentBlackFrame() {
	// FFmpeg 8.1/libvpx independently encoded a 64x64 limited-range black frame.
	static constexpr std::array<std::uint8_t, 31> kPacket{0x82, 0x49, 0x83, 0x42, 0x00, 0x03, 0xf0, 0x03, 0xf6, 0x00, 0x38, 0x24, 0x1c, 0x18, 0x4a, 0x00, 0x00, 0x30, 0x60, 0x00, 0x00, 0x67, 0x3f, 0xff, 0xff, 0x56, 0x45, 0x64, 0xee, 0xcc, 0x00};
	sgre::media::Vp9Description description;
	description.Width = 64;
	description.Height = 64;
	sgre::media::Vp9VideoDecoder decoder(description, 6144);
	const auto frames = decoder.Decode(kPacket, 123456789);
	Require(frames.size() == 1, "synthetic VP9 frame was not decoded");
	const auto& frame = frames.front();
	Require(frame.Width == 64 && frame.Height == 64 && frame.TimestampNanoseconds == 123456789, "VP9 dimensions/timestamp differ");
	Require(frame.Luma.size() == 4096 && frame.Chroma.size() == 2048 && !frame.FullRange, "NV12 plane sizes/range differ");
	Require(std::all_of(frame.Luma.begin(), frame.Luma.end(), [](std::uint8_t value) {
		return value == 16;
	}),
	    "black-frame luma differs from independent vector");
	Require(std::all_of(frame.Chroma.begin(), frame.Chroma.end(), [](std::uint8_t value) {
		return value == 128;
	}),
	    "black-frame chroma differs from independent vector");
	Require(decoder.Drain().empty(), "decoder invented delayed output");

	auto moved = std::move(decoder);
	bool rejected = false;
	try {
		static_cast<void>(decoder.Drain());
	} catch (const std::logic_error&) {
		rejected = true;
	}

	Require(rejected && moved.Drain().empty(), "decoder move lost its ownership invariant");
}

static void TestBounds() {
	sgre::media::Vp9Description description;
	description.Width = 64;
	description.Height = 64;
	bool rejected = false;
	try {
		const sgre::media::Vp9VideoDecoder invalid(description, 6143);
	} catch (const std::runtime_error&) {
		rejected = true;
	}

	Require(rejected, "decoder accepted an insufficient frame bound");
	sgre::media::Vp9VideoDecoder decoder(description);
	rejected = false;
	try {
		static_cast<void>(decoder.Decode({}, 0));
	} catch (const std::runtime_error&) {
		rejected = true;
	}

	Require(rejected, "decoder accepted an empty VP9 packet");
}

int main() {
	try {
		TestIndependentBlackFrame();
		TestBounds();
		std::cout << "macOS VP9 tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
