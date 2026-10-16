#include "sgre/audio/wma_coefficient.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static sgre::audio::WmaCoefficientEscapeWidths EscapeWidths() {
	sgre::audio::WmaCoefficientEscapeWidths widths;
	widths.MagnitudeBits = 9;
	widths.ZeroRunBits = 11;

	return widths;
}

struct NativeFixture {
	sgre::audio::WmaCoefficientBook Book;
	std::array<std::uint8_t, 8> Escape;
	std::size_t EscapeBits = 0;
	std::array<std::uint8_t, 2> End;
	std::size_t EndBits = 0;
	std::uint8_t PositiveOne = 0;
	std::uint8_t NegativeOne = 0;
};

static void TestNativeCoefficientVectors() {
	// Independent native helper vectors; no game asset bytes are used. See docs/recovery/wma.md.
	const std::array<NativeFixture, 6> fixtures = {
	    NativeFixture{sgre::audio::WmaCoefficientBook::SetOneMain, {0x4b, 0x10, 0x1a, 0xc7, 0, 0, 0, 0}, 32, {0xf4, 0}, 6, 0x20, 0x00},
	    NativeFixture{sgre::audio::WmaCoefficientBook::SetOneAlternate, {0x8a, 0xc0, 0x6b, 0x1c, 0, 0, 0, 0}, 30, {0x10, 0}, 5, 0x60, 0x40},
	    NativeFixture{sgre::audio::WmaCoefficientBook::SetTwoMain, {0x7c, 0xd0, 0x1a, 0xc7, 0, 0, 0, 0}, 32, {0x7b, 0}, 9, 0x20, 0x00},
	    NativeFixture{sgre::audio::WmaCoefficientBook::SetTwoAlternate, {0xd9, 0x40, 0x6b, 0x1c, 0, 0, 0, 0}, 30, {0xd2, 0}, 7, 0x20, 0x00},
	    NativeFixture{sgre::audio::WmaCoefficientBook::SetThreeMain, {0xf0, 0x18, 0x0d, 0x63, 0x80, 0, 0, 0}, 33, {0x78, 0}, 6, 0x20, 0x00},
	    NativeFixture{sgre::audio::WmaCoefficientBook::SetThreeAlternate, {0xd1, 0xe0, 0x35, 0x8e, 0, 0, 0, 0}, 31, {0xb0, 0}, 4, 0x60, 0x40}};
	for (const auto& fixture : fixtures) {
		const sgre::audio::WmaCoefficientDecoder decoder(fixture.Book);
		sgre::audio::WmaBitReader escape(fixture.Escape);
		const auto value = decoder.Decode(escape, EscapeWidths());
		Require(value.Kind == sgre::audio::WmaCoefficientKind::Value && value.ZeroRun == 1379 && value.Magnitude == 257 && !value.IsNegative && escape.PositionBits() == fixture.EscapeBits, "native coefficient escape fields or bit extent differ");
		sgre::audio::WmaBitReader end(fixture.End);
		const auto finished = decoder.Decode(end, EscapeWidths());
		Require(finished.Kind == sgre::audio::WmaCoefficientKind::EndOfBlock && end.PositionBits() == fixture.EndBits, "end-of-block consumed a sign bit or differed");
		const std::array<std::uint8_t, 1> positive_bytes = {fixture.PositiveOne};
		const std::array<std::uint8_t, 1> negative_bytes = {fixture.NegativeOne};
		sgre::audio::WmaBitReader positive(positive_bytes);
		sgre::audio::WmaBitReader negative(negative_bytes);
		const auto positive_value = decoder.Decode(positive, EscapeWidths());
		const auto negative_value = decoder.Decode(negative, EscapeWidths());
		Require(positive_value.ZeroRun == 0 && positive_value.Magnitude == 1 && !positive_value.IsNegative && positive.PositionBits() == 3, "native positive coefficient sign differs");
		Require(negative_value.ZeroRun == 0 && negative_value.Magnitude == 1 && negative_value.IsNegative && negative.PositionBits() == 3, "native negative coefficient sign differs");
	}
}

static void TestFailedValuesDoNotConsume() {
	const sgre::audio::WmaCoefficientDecoder decoder(sgre::audio::WmaCoefficientBook::SetThreeMain);
	const std::array<std::uint8_t, 4> incomplete_escape = {0xf0, 0x18, 0x0d, 0x63};
	sgre::audio::WmaBitReader escape(incomplete_escape);
	bool rejected = false;
	try {
		static_cast<void>(decoder.Decode(escape, EscapeWidths()));
	} catch (const std::runtime_error&) {
		rejected = true;
	}

	Require(rejected && escape.PositionBits() == 0, "truncated escape sign consumed coefficient fields");
	const std::array<std::uint8_t, 1> byte = {0};
	sgre::audio::WmaBitReader ordinary(byte);
	ordinary.SkipBits(6);
	rejected = false;
	try {
		static_cast<void>(decoder.Decode(ordinary, EscapeWidths()));
	} catch (const std::runtime_error&) {
		rejected = true;
	}

	Require(rejected && ordinary.PositionBits() == 6, "truncated ordinary sign consumed coefficient prefix");
	for (const auto count : {0, 32}) {
		auto invalid = EscapeWidths();
		invalid.ZeroRunBits = static_cast<std::uint8_t>(count);
		rejected = false;
		try {
			static_cast<void>(decoder.Decode(escape, invalid));
		} catch (const std::runtime_error&) {
			rejected = true;
		}

		Require(rejected && escape.PositionBits() == 0, "invalid escape width consumed input");
	}

	rejected = false;
	try {
		const sgre::audio::WmaCoefficientDecoder invalid(static_cast<sgre::audio::WmaCoefficientBook>(255));
	} catch (const std::runtime_error&) {
		rejected = true;
	}

	Require(rejected, "unknown coefficient book was accepted");
}

static void TestCodingHeaders() {
	const std::array<std::uint8_t, 1> mono = {0x80};
	sgre::audio::WmaBitReader mono_reader(mono);
	const auto mono_header = sgre::audio::ReadWmaCodingHeader(mono_reader, 1);
	Require(!mono_header.MidSideStereo && mono_header.ChannelCoded[0] && !mono_header.ChannelCoded[1] && mono_header.TotalGain == 1 && mono_reader.PositionBits() == 8, "mono coding header differs");
	const std::array<std::uint8_t, 2> stereo = {0xc1, 0x40};
	sgre::audio::WmaBitReader stereo_reader(stereo);
	const auto stereo_header = sgre::audio::ReadWmaCodingHeader(stereo_reader, 2);
	Require(stereo_header.MidSideStereo && stereo_header.ChannelCoded[0] && !stereo_header.ChannelCoded[1] && stereo_header.TotalGain == 6 && stereo_reader.PositionBits() == 10, "stereo coding/gain bit order differs");
	sgre::audio::WmaBitReader silent_reader(mono);
	const auto silent_header = sgre::audio::ReadWmaCodingHeader(silent_reader, 2);
	Require(silent_header.MidSideStereo && !silent_header.ChannelCoded[0] && !silent_header.ChannelCoded[1] && silent_header.TotalGain == 1 && silent_reader.PositionBits() == 3, "uncoded stereo block consumed gain bits");
	const std::array<std::uint8_t, 3> extended = {0xff, 0xfe, 0x08};
	sgre::audio::WmaBitReader extended_reader(extended);
	Require(sgre::audio::ReadWmaCodingHeader(extended_reader, 1).TotalGain == 257 && extended_reader.PositionBits() == 22, "gain continuation accumulation differs");
	const std::array<std::uint8_t, 1> truncated = {0xff};
	for (const auto channels : {0, 1, 3}) {
		sgre::audio::WmaBitReader incomplete(truncated);
		bool rejected = false;
		try {
			static_cast<void>(sgre::audio::ReadWmaCodingHeader(incomplete, static_cast<std::uint16_t>(channels)));
		} catch (const std::runtime_error&) {
			rejected = true;
		}

		Require(rejected && incomplete.PositionBits() == 0, "invalid/truncated coding header consumed bits");
	}

	for (const auto group_limit : {0, 1, 65}) {
		sgre::audio::WmaGainLimits limits;
		limits.MaxGroups = static_cast<std::uint16_t>(group_limit);
		sgre::audio::WmaBitReader incomplete(truncated);
		bool rejected = false;
		try {
			static_cast<void>(sgre::audio::ReadWmaCodingHeader(incomplete, 1, limits));
		} catch (const std::runtime_error&) {
			rejected = true;
		}

		Require(rejected && incomplete.PositionBits() == 0, "gain group limit failed to bound parsing");
	}

	sgre::audio::WmaGainLimits limits;
	limits.MaxGain = 127;
	sgre::audio::WmaBitReader bounded(extended);
	bool rejected = false;
	try {
		static_cast<void>(sgre::audio::ReadWmaCodingHeader(bounded, 1, limits));
	} catch (const std::runtime_error&) {
		rejected = true;
	}

	Require(rejected && bounded.PositionBits() == 0, "gain value limit consumed bits");
}

static void TestGainWidthsAndBookSelection() {
	const std::array<std::array<std::uint32_t, 2>, 11> gains = {std::array<std::uint32_t, 2>{0, 13}, {14, 13}, {15, 12}, {31, 12}, {32, 11}, {39, 11}, {40, 10}, {44, 10}, {45, 9}, {255, 9}, {0xffffffff, 13}};
	for (const auto& gain : gains) {
		Require(sgre::audio::WmaEscapeMagnitudeBits(gain[0]) == gain[1], "native gain-to-escape threshold differs");
	}

	sgre::resource::WaveFormat format;
	format.Encoding = sgre::resource::WaveEncoding::WmaStandard;
	format.Channels = 1;
	format.SamplesPerSecond = 44100;
	format.AverageBytesPerSecond = 4000;
	Require(sgre::audio::SelectWmaCoefficientBook(format, false, 0) == sgre::audio::WmaCoefficientBook::SetTwoMain, "original mono voice selected the wrong coefficient book");
	format.SamplesPerSecond = 48000;
	format.AverageBytesPerSecond = 4319;
	Require(sgre::audio::SelectWmaCoefficientBook(format, false, 0) == sgre::audio::WmaCoefficientBook::SetOneMain, "below .72 coefficient boundary differs");
	format.AverageBytesPerSecond = 4320;
	Require(sgre::audio::SelectWmaCoefficientBook(format, false, 0) == sgre::audio::WmaCoefficientBook::SetTwoMain, "equal .72 coefficient boundary differs");
	format.SamplesPerSecond = 40000;
	format.AverageBytesPerSecond = 5799;
	Require(sgre::audio::SelectWmaCoefficientBook(format, false, 0) == sgre::audio::WmaCoefficientBook::SetTwoMain, "below 1.16 coefficient boundary differs");
	format.AverageBytesPerSecond = 5800;
	Require(sgre::audio::SelectWmaCoefficientBook(format, false, 0) == sgre::audio::WmaCoefficientBook::SetThreeMain, "equal 1.16 coefficient boundary differs");
	format.Channels = 2;
	format.SamplesPerSecond = 44100;
	format.AverageBytesPerSecond = 7000;
	Require(sgre::audio::SelectWmaCoefficientBook(format, true, 0) == sgre::audio::WmaCoefficientBook::SetTwoMain && sgre::audio::SelectWmaCoefficientBook(format, true, 1) == sgre::audio::WmaCoefficientBook::SetTwoAlternate && sgre::audio::SelectWmaCoefficientBook(format, false, 1) == sgre::audio::WmaCoefficientBook::SetTwoMain, "stereo normalization or alternate-channel selection differs");
	format.Channels = 1;
	format.SamplesPerSecond = 22050;
	format.AverageBytesPerSecond = 1;
	Require(sgre::audio::SelectWmaCoefficientBook(format, false, 0) == sgre::audio::WmaCoefficientBook::SetThreeMain, "low-rate native default coefficient book differs");
	bool rejected = false;
	try {
		static_cast<void>(sgre::audio::SelectWmaCoefficientBook(format, true, 0));
	} catch (const std::runtime_error&) {
		rejected = true;
	}

	Require(rejected, "mono mid/side coefficient selection was accepted");
}

int main() {
	try {
		TestNativeCoefficientVectors();
		TestFailedValuesDoNotConsume();
		TestCodingHeaders();
		TestGainWidthsAndBookSelection();
		std::cout << "WMA coefficient tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
