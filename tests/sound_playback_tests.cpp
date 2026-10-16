#include "sgre/audio/sound_playback.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static void TestSubmission() {
	sgre::resource::SoundChannel channel;
	channel.EncodedData.Size = 1933884;
	channel.Format.SamplesPerSecond = 44100;
	Require(sgre::audio::PlanSoundSubmission(channel, 0).LoopRepeats == 0, "unlooped zero count differs");
	Require(sgre::audio::PlanSoundSubmission(channel, 1).LoopRepeats == 0, "single playback unexpectedly repeats");
	Require(sgre::audio::PlanSoundSubmission(channel, 3).LoopRepeats == 2, "finite playback count differs");
	Require(sgre::audio::PlanSoundSubmission(channel, -1).LoopRepeats == 255, "unlooped negative count is not infinite");
	channel.Loop = sgre::resource::SampleLoop{};
	channel.Loop->Begin = 18097;
	channel.Loop->Length = 948845;
	const auto pcm = sgre::audio::PlanSoundSubmission(channel, 0);
	Require(pcm.LoopBegin == 18097 && pcm.LoopLength == 948845 && pcm.LoopRepeats == 255 && pcm.EncodedBytes == 1933884, "captured infinite PCM submission differs");
	channel.Format.Encoding = sgre::resource::WaveEncoding::MicrosoftAdpcm;
	channel.Format.Extra = {0xf4, 1};
	const auto adpcm = sgre::audio::PlanSoundSubmission(channel, 3);
	Require(adpcm.LoopBegin == 18000 && adpcm.LoopLength == 948500 && adpcm.LoopRepeats == 2, "ADPCM loops were not rounded down to complete 500-sample blocks");
	channel.DisableSampleRateConversion = true;
	Require(!sgre::audio::PlanSoundSubmission(channel, 1).DisableRateConversion, "nosrc changed a non-48000 source");
	channel.Format.SamplesPerSecond = 48000;
	Require(sgre::audio::PlanSoundSubmission(channel, 1).DisableRateConversion, "48000 nosrc was ignored");
	channel.Format.Encoding = sgre::resource::WaveEncoding::WmaStandard;
	channel.DecodedPacketCumulativeBytes.resize(13);
	Require(sgre::audio::PlanSoundSubmission(channel, 1).WmaPackets == 13, "WMA packet table count differs");
	channel.Format.Encoding = sgre::resource::WaveEncoding::MicrosoftAdpcm;
	channel.Format.Extra = {0, 0};
	bool rejected = false;
	try {
		static_cast<void>(sgre::audio::PlanSoundSubmission(channel, 1));
	} catch (const std::runtime_error&) {
		rejected = true;
	}

	Require(rejected, "zero ADPCM samples-per-block was accepted");
}

static void TestMatrixAndAccumulation() {
	sgre::audio::SoundMixParameters parameters;
	const auto stereo = sgre::audio::EvaluateSoundMix(parameters, 2, 2, false);
	Require(stereo.Matrix[0] == 1 && stereo.Matrix[1] == 0 && stereo.Matrix[2] == 0 && stereo.Matrix[3] == 1 && stereo.FrequencyRatio == 1, "default stereo routing differs");
	parameters.UseSourcePan = true;
	parameters.VoiceVolume = 0.5f;
	parameters.ResourceVolume = 2.0f;
	parameters.GroupVolume = 0.25f;
	parameters.ArchiveVolume = 2.0f;
	parameters.MasterVolume = 0.5f;
	parameters.VoicePitchOctaves = 0.75f;
	parameters.ResourcePitchOctaves = -0.25f;
	parameters.ResourcePan = {2, 3, 4, 1, 1, 1, 1, 1};
	parameters.SourcePan[0].Destinations = {1, -2, 3, 0, 0, 0, 0, 0};
	parameters.SourcePan[1].Destinations = {-4, 5, -6, 0, 0, 0, 0, 0};
	const auto mix = sgre::audio::EvaluateSoundMix(parameters, 2, 3, false);
	const std::array<float, 6> expected = {0.5f, -2.0f, -1.5f, 3.75f, 3.0f, -6.0f};
	for (std::size_t index = 0; index < expected.size(); ++index) {
		Require(mix.Matrix[index] == expected[index], "non-symmetric source/destination matrix differs");
	}

	Require(mix.FrequencyRatio && std::abs(*mix.FrequencyRatio - std::sqrt(2.0f)) < 1e-6f, "pitch is not measured in octaves");
	Require(!sgre::audio::EvaluateSoundMix(parameters, 2, 3, true).FrequencyRatio, "nosrc still requested a pitch operation");
	const std::array<float, 4> input = {2, -1, -4, 3};
	std::array<float, 6> output = {1, 1, 1, 1, 1, 1};
	sgre::audio::AccumulateSoundMix(mix, input, output);
	Require(output == std::array<float, 6>{4.0f, -5.75f, 13.0f, -7.0f, 18.25f, -29.0f}, "interleaved matrix accumulation clipped or transposed samples");
	const auto mono = sgre::audio::EvaluateSoundMix(parameters, 1, 2, false);
	Require(mono.Matrix[0] == -1.5f && mono.Matrix[1] == 2.25f, "mono source-pan folding differs");
	parameters.FoldStereoPanForMono = false;
	const auto unfolded = sgre::audio::EvaluateSoundMix(parameters, 1, 2, false);
	Require(unfolded.Matrix[0] == 0.5f && unfolded.Matrix[1] == -1.5f, "mono source-pan row selection differs");
	bool rejected = false;
	try {
		sgre::audio::AccumulateSoundMix(mix, input, std::span(output).first(3));
	} catch (const std::runtime_error&) {
		rejected = true;
	}

	Require(rejected, "unequal frame counts were accepted");
}

static std::vector<std::uint8_t> ReadFile(const char* path) {
	std::ifstream stream(path, std::ios::binary | std::ios::ate);
	const auto length = stream.tellg();
	if (!stream || length < 0 || length > 2 * 1024 * 1024) {
		throw std::runtime_error("could not open a bounded audio policy vector file");
	}

	std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
	stream.seekg(0);
	stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	if (!stream) {
		throw std::runtime_error("audio policy vector read failed");
	}

	return bytes;
}

static std::uint32_t Word(std::span<const std::uint8_t> bytes, std::size_t offset) {
	if (offset > bytes.size() || bytes.size() - offset < 4) {
		throw std::runtime_error("audio policy vector is truncated");
	}

	return bytes[offset] | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) | (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) | (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

static void CompareNative(const char* input_path, const char* native_path) {
	const auto inputs = ReadFile(input_path);
	const auto native = ReadFile(native_path);
	constexpr std::size_t input_size = 16 + 88 * 4;
	constexpr std::size_t output_size = 8 + 64 * 4;
	Require(inputs.size() % input_size == 0 && native.size() / output_size == inputs.size() / input_size && native.size() % output_size == 0, "native audio policy vector extents disagree");
	for (std::size_t offset = 0, native_offset = 0; offset < inputs.size(); offset += input_size, native_offset += output_size) {
		sgre::audio::SoundMixParameters parameters;
		const auto value = [&inputs, offset](std::size_t index) {
			return std::bit_cast<float>(Word(inputs, offset + 16 + index * 4));
		};
		parameters.UseSourcePan = Word(inputs, offset + 8) != 0;
		parameters.FoldStereoPanForMono = Word(inputs, offset + 12) != 0;
		parameters.VoiceVolume = value(0);
		parameters.ResourceVolume = value(1);
		parameters.GroupVolume = value(2);
		parameters.ArchiveVolume = value(3);
		parameters.MasterVolume = value(4);
		parameters.MuteVolume = value(5);
		parameters.VoicePitchOctaves = value(6);
		parameters.ResourcePitchOctaves = value(7);
		for (std::size_t destination = 0; destination < 8; ++destination) {
			parameters.Pan.Destinations[destination] = value(8 + destination);
			parameters.ResourcePan[destination] = value(80 + destination);
			for (std::size_t source = 0; source < 8; ++source) {
				parameters.SourcePan[source].Destinations[destination] = value(16 + source * 8 + destination);
			}
		}

		const auto mix = sgre::audio::EvaluateSoundMix(parameters, static_cast<std::uint8_t>(Word(inputs, offset)), static_cast<std::uint8_t>(Word(inputs, offset + 4)), true);
		for (std::size_t index = 0; index < mix.Matrix.size(); ++index) {
			if (std::bit_cast<std::uint32_t>(mix.Matrix[index]) != Word(native, native_offset + 8 + index * 4)) {
				throw std::runtime_error("sound routing native mismatch at case " + std::to_string(offset / input_size) + ", matrix index " + std::to_string(index) + ", actual bits " + std::to_string(std::bit_cast<std::uint32_t>(mix.Matrix[index])) + ", expected bits " + std::to_string(Word(native, native_offset + 8 + index * 4)));
			}
		}
	}

	std::cout << "Original sound matrix cases: " << inputs.size() / input_size << '\n';
}

int main(int argc, char** argv) {
	try {
		TestSubmission();
		TestMatrixAndAccumulation();
		if (argc == 3) {
			CompareNative(argv[1], argv[2]);
		} else {
			Require(argc == 1, "expected zero arguments or two native vector paths");
		}

		std::cout << "Sound submission and routing tests passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
