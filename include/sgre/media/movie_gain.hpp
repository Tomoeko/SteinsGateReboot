#pragma once

namespace sgre::media {

	struct MovieSoundVolumeState {
		float MasterLinear = 1;
		bool LoudnessEnabled = true;
		bool MasterBypassed = false;
	};

	// Native3903b0 converts the retained master in the opposite direction from the movie setting.
	[[nodiscard]] float CalculateMovieSourceGain(float movie_setting, const MovieSoundVolumeState& actual_sound);

} // namespace sgre::media
