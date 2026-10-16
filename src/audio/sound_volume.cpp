#include "sgre/audio/sound_volume.hpp"

#include <cmath>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string>

namespace sgre::audio {

	static float ConvertLoudness(float value, float base, float logarithm_base) {
		if (value == 0.0f || value == 1.0f || value == -1.0f) {
			return value;
		}

		// The original branches on ordered nonnegative input and preserves phase for negative gain.
		if (value >= 0.0f) {
			return std::pow(base, std::log(value) / std::log(logarithm_base));
		}

		return -std::pow(base, std::log(-value) / std::log(logarithm_base));
	}

	static float ConvertDecibels(float value) {
		if (value >= 0.0f) {
			return 1.0f;
		}

		if (value <= -96.0f) {
			return 0.0f;
		}

		return std::pow(10.0f, value / 20.0f);
	}

	float SoundVolumeToLinear(float value, SoundVolumeUnit unit, bool loudness_enabled) {
		switch (unit) {
		case SoundVolumeUnit::Setting:
			return loudness_enabled ? ConvertLoudness(value, 3.0f, 2.0f) : value;
		case SoundVolumeUnit::Linear:
			return value;
		case SoundVolumeUnit::Loudness:
			return ConvertLoudness(value, 3.0f, 2.0f);
		case SoundVolumeUnit::Decibels:
			return ConvertDecibels(value);
		}

		throw std::runtime_error("invalid sound volume unit");
	}

	static bool VolumeUnitEquals(std::string_view suffix, std::string_view expected) {
		if (suffix.size() != expected.size()) {
			return false;
		}

		for (std::size_t index = 0; index < suffix.size(); ++index) {
			const auto character = static_cast<unsigned char>(suffix[index]);
			const auto uppercase = character >= 'a' && character <= 'z' ? character - ('a' - 'A') : character;
			if (uppercase != static_cast<unsigned char>(expected[index])) {
				return false;
			}
		}

		return true;
	}

	float ParseSoundVolume(std::string_view text, bool loudness_enabled) {
		text = text.substr(0, text.find('\0'));
		if (text.size() > 65536) {
			throw std::runtime_error("sound volume text exceeds its bounded numeric/unit input");
		}

		// The program retains its initial C numeric locale. Native580e44 is the corresponding CRT strtod.
		const std::string terminated(text);
		char* end = nullptr;
		const auto parsed = std::strtod(terminated.c_str(), &end);
		const auto value = std::abs(parsed) > std::numeric_limits<float>::max() ? std::copysign(std::numeric_limits<float>::infinity(), parsed) : static_cast<float>(parsed);
		while (*end == ' ') {
			++end;
		}
		const std::string_view suffix(end);
		if (VolumeUnitEquals(suffix, "V")) {
			return static_cast<float>(value);
		}
		if (VolumeUnitEquals(suffix, "L")) {
			return SoundVolumeToLinear(static_cast<float>(value), SoundVolumeUnit::Loudness, loudness_enabled);
		}
		if (VolumeUnitEquals(suffix, "DB")) {
			return SoundVolumeToLinear(static_cast<float>(value), SoundVolumeUnit::Decibels, loudness_enabled);
		}

		return SoundVolumeToLinear(static_cast<float>(value), SoundVolumeUnit::Setting, loudness_enabled);
	}

	float SoundVolumeFromLinear(float value, bool loudness_enabled) {
		return loudness_enabled ? ConvertLoudness(value, 2.0f, 3.0f) : value;
	}

} // namespace sgre::audio
