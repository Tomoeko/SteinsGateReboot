#pragma once

#include <string_view>

namespace sgre::audio {

	enum class SoundVolumeUnit {
		Setting,
		Linear,
		Loudness,
		Decibels,
	};

	// Setting follows the recovered engine flag; explicit V/L/DB units bypass that choice.
	[[nodiscard]] float SoundVolumeToLinear(float value, SoundVolumeUnit unit = SoundVolumeUnit::Setting, bool loudness_enabled = true);
	// Native381390: CRT numeric prefix, then case-insensitive whole V/L/DB suffix; unknown suffix means Setting.
	[[nodiscard]] float ParseSoundVolume(std::string_view text, bool loudness_enabled = true);
	[[nodiscard]] float SoundVolumeFromLinear(float value, bool loudness_enabled = true);

} // namespace sgre::audio
