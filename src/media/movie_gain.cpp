#include "sgre/media/movie_gain.hpp"

#include "sgre/audio/sound_volume.hpp"

namespace sgre::media {

	float CalculateMovieSourceGain(float movie_setting, const MovieSoundVolumeState& actual_sound) {
		const auto master_linear = actual_sound.MasterBypassed ? 1.0f : actual_sound.MasterLinear;
		const auto master_setting = audio::SoundVolumeFromLinear(master_linear, actual_sound.LoudnessEnabled);
		const auto movie_linear = audio::SoundVolumeToLinear(movie_setting, audio::SoundVolumeUnit::Setting, actual_sound.LoudnessEnabled);

		return movie_linear * master_setting;
	}

} // namespace sgre::media
