#include "sgre/audio/user_audio_settings.hpp"

#include <stdexcept>

namespace sgre::audio {

	void ValidateUserAudioSettings(const UserAudioSettings& settings) {
		if (settings.Master > 100 || settings.Music > 100 || settings.Voice > 100 || settings.Effects > 100) {
			throw std::runtime_error("user audio volume must be an integer percentage from 0 to 100");
		}
	}

	float UserAudioGroupGain(const UserAudioSettings& settings, std::string_view original_group) {
		ValidateUserAudioSettings(settings);
		auto percentage = settings.Effects;
		if (original_group == "bgm") {
			percentage = settings.Music;
		} else if (original_group == "voice") {
			percentage = settings.Voice;
		}

		return static_cast<float>(percentage) / 100.0f;
	}

} // namespace sgre::audio
