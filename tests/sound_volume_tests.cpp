#include "sgre/audio/sound_volume.hpp"

#include "sgre/audio/sound_playback.hpp"

#include <bit>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static void TestVolumeUnits() {
	using sgre::audio::SoundVolumeToLinear;
	using sgre::audio::SoundVolumeUnit;
	Require(SoundVolumeToLinear(2.0f) == 3.0f && SoundVolumeToLinear(-2.0f) == -3.0f, "sound loudness units or phase differ");
	Require(sgre::audio::SoundVolumeFromLinear(3.0f) == 2.0f && sgre::audio::SoundVolumeFromLinear(-3.0f) == -2.0f, "inverse sound loudness units differ");
	Require(SoundVolumeToLinear(-1.5f, SoundVolumeUnit::Linear) == -1.5f && SoundVolumeToLinear(2.0f, SoundVolumeUnit::Setting, false) == 2.0f, "linear sound gain was converted or clipped");
	Require(SoundVolumeToLinear(2.0f, SoundVolumeUnit::Loudness, false) == 3.0f, "explicit loudness unit followed the setting flag");
	Require(sgre::audio::SoundVolumeFromLinear(-1.5f, false) == -1.5f, "inverse linear setting changed the value");
	Require(SoundVolumeToLinear(7.0f, SoundVolumeUnit::Decibels) == 1.0f && SoundVolumeToLinear(0.0f, SoundVolumeUnit::Decibels) == 1.0f, "nonnegative decibels exceeded native unity");
	Require(SoundVolumeToLinear(-96.0f, SoundVolumeUnit::Decibels) == 0.0f && SoundVolumeToLinear(-120.0f, SoundVolumeUnit::Decibels) == 0.0f, "decibel silence threshold differs");
	Require(SoundVolumeToLinear(-20.0f, SoundVolumeUnit::Decibels) == 0.1f, "decibel amplitude units differ");
	Require(std::bit_cast<std::uint32_t>(SoundVolumeToLinear(-0.0f)) == 0x80000000 && std::bit_cast<std::uint32_t>(sgre::audio::SoundVolumeFromLinear(-0.0f)) == 0x80000000, "native signed zero was normalized");
	const auto infinity = std::numeric_limits<float>::infinity();
	Require(SoundVolumeToLinear(infinity, SoundVolumeUnit::Decibels) == 1.0f && SoundVolumeToLinear(-infinity, SoundVolumeUnit::Decibels) == 0.0f, "infinite decibel bounds differ");
	Require(std::isnan(SoundVolumeToLinear(std::numeric_limits<float>::quiet_NaN(), SoundVolumeUnit::Decibels)), "unordered decibels were silently clamped");
	bool rejected = false;
	try {
		static_cast<void>(SoundVolumeToLinear(1.0f, static_cast<SoundVolumeUnit>(99)));
	} catch (const std::runtime_error&) {
		rejected = true;
	}

	Require(rejected, "invalid volume unit was accepted");
}

static std::uint32_t ReadWord(std::string_view line, std::string_view name) {
	const auto begin = line.find(name);
	Require(begin != std::string_view::npos, "native sound volume vector is missing a word");
	std::uint32_t value = 0;
	const auto first = line.data() + begin + name.size();
	const auto result = std::from_chars(first, line.data() + line.size(), value);
	Require(result.ec == std::errc{} && result.ptr != first, "native sound volume word is invalid");

	return value;
}

static sgre::audio::SoundVolumeUnit ReadUnit(std::string_view line) {
	using sgre::audio::SoundVolumeUnit;
	if (line.find("\"unit\":\"Setting\"") != std::string_view::npos) {
		return SoundVolumeUnit::Setting;
	}

	if (line.find("\"unit\":\"Linear\"") != std::string_view::npos) {
		return SoundVolumeUnit::Linear;
	}

	if (line.find("\"unit\":\"Loudness\"") != std::string_view::npos) {
		return SoundVolumeUnit::Loudness;
	}

	if (line.find("\"unit\":\"Decibels\"") != std::string_view::npos) {
		return SoundVolumeUnit::Decibels;
	}

	throw std::runtime_error("native sound volume unit is invalid");
}

static void CompareNative(const char* path) {
	std::ifstream input(path, std::ios::ate);
	Require(input && input.tellg() >= 0 && input.tellg() <= 512 * 1024, "could not open bounded native sound volume vectors");
	input.seekg(0);
	std::size_t count = 0;
	std::string line;
	while (std::getline(input, line)) {
		Require(line.size() < 1024 && count < 1024, "native sound volume vector count or line exceeds its bound");
		const auto value = std::bit_cast<float>(ReadWord(line, "\"input\":"));
		float actual = 0.0f;
		if (line.find("\"unit\"") != std::string_view::npos) {
			const bool enabled = line.find("\"loudness_enabled\":true") != std::string_view::npos;
			actual = sgre::audio::SoundVolumeToLinear(value, ReadUnit(line), enabled);
		} else if (line.find("\"kind\":\"linear\"") != std::string_view::npos) {
			actual = sgre::audio::SoundVolumeFromLinear(value);
		} else if (line.find("\"kind\":\"loudness\"") != std::string_view::npos) {
			actual = sgre::audio::SoundVolumeToLinear(value);
		} else if (line.find("\"kind\":\"pitch\"") != std::string_view::npos) {
			sgre::audio::SoundMixParameters parameters;
			parameters.VoicePitchOctaves = value;
			actual = sgre::audio::EvaluateSoundMix(parameters, 2, 2, false).FrequencyRatio.value();
		} else {
			throw std::runtime_error("native sound volume operation is invalid");
		}

		Require(std::bit_cast<std::uint32_t>(actual) == ReadWord(line, "\"output\":"), "sound volume result differs from original baseline instructions");
		++count;
	}

	Require(input.eof() && count != 0, "native sound volume vector read failed");
	std::cout << "Original sound volume/pitch cases: " << count << '\n';
}

int main(int argc, char** argv) {
	try {
		TestVolumeUnits();
		if (argc == 3) {
			CompareNative(argv[1]);
			CompareNative(argv[2]);
		} else {
			Require(argc == 1, "expected no arguments or two native vector files");
		}

		std::cout << "Sound volume tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
