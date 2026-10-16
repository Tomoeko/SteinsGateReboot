#include "sgre/audio/wave_decoder.hpp"

#include <iostream>
#include <stdexcept>

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static sgre::resource::WaveFormat Pcm(std::uint16_t bits) {
	sgre::resource::WaveFormat format;
	format.Encoding = sgre::resource::WaveEncoding::Pcm;
	format.Channels = 1;
	format.SamplesPerSecond = 44100;
	format.BitsPerSample = bits;
	format.BlockAlignment = bits / 8;

	return format;
}

static void TestPcm() {
	const auto eight = sgre::audio::DecodeWaveAudio(Pcm(8), std::vector<std::uint8_t>{0, 128, 255, 64});
	Require(eight.Samples == std::vector<float>{-1, 0, 127.0F / 128, -0.5F}, "unsigned PCM8 vector differs");
	const auto sixteen = sgre::audio::DecodeWaveAudio(Pcm(16), std::vector<std::uint8_t>{0, 0x80, 0, 0, 0xff, 0x7f});
	Require(sixteen.Samples == std::vector<float>{-1, 0, 32767.0F / 32768}, "signed PCM16 vector differs");
	const auto twenty_four = sgre::audio::DecodeWaveAudio(Pcm(24), std::vector<std::uint8_t>{0, 0, 0x80, 0xff, 0xff, 0xff});
	Require(twenty_four.Samples == std::vector<float>{-1, -1.0F / 8388608}, "signed PCM24 vector differs");
	const auto thirty_two = sgre::audio::DecodeWaveAudio(Pcm(32), std::vector<std::uint8_t>{0, 0, 0, 0x80, 0, 0, 0, 0});
	Require(thirty_two.Samples == std::vector<float>{-1, 0}, "signed PCM32 vector differs");
}

static void TestAdpcm() {
	sgre::resource::WaveFormat format;
	format.Encoding = sgre::resource::WaveEncoding::MicrosoftAdpcm;
	format.Channels = 1;
	format.SamplesPerSecond = 48000;
	format.BlockAlignment = 8;
	format.BitsPerSample = 4;
	format.Extra = {4, 0, 1, 0, 0, 1, 0, 0};
	const auto mono = sgre::audio::DecodeWaveAudio(format, std::vector<std::uint8_t>{0, 16, 0, 100, 0, 0, 0, 0x1f});
	Require(mono.Samples == std::vector<float>{0, 100.0F / 32768, 116.0F / 32768, 100.0F / 32768}, "mono ADPCM predictor/nibble order differs");
	format.Channels = 2;
	format.BlockAlignment = 15;
	format.Extra[0] = 3;
	const auto stereo = sgre::audio::DecodeWaveAudio(format, std::vector<std::uint8_t>{0, 0, 16, 0, 32, 0, 100, 0, 0x9c, 0xff, 0, 0, 200, 0, 0x1f});
	Require(stereo.Samples == std::vector<float>{0, 200.0F / 32768, 100.0F / 32768, -100.0F / 32768, 116.0F / 32768, -132.0F / 32768}, "stereo ADPCM channel/header order differs");
}

static void TestBounds() {
	bool rejected = false;
	try {
		static_cast<void>(sgre::audio::DecodeWaveAudio(Pcm(16), std::vector<std::uint8_t>{1}));
	} catch (const std::runtime_error&) {
		rejected = true;
	}

	Require(rejected, "partial PCM sample was accepted");
	rejected = false;
	try {
		static_cast<void>(sgre::audio::DecodeWaveAudio(Pcm(16), std::vector<std::uint8_t>{0, 0}, 3));
	} catch (const std::runtime_error&) {
		rejected = true;
	}

	Require(rejected, "decoded allocation bound was ignored");
}

int main() {
	try {
		TestPcm();
		TestAdpcm();
		TestBounds();
		std::cout << "wave decoder tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
