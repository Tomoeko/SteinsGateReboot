#pragma once

#include <cstdint>
#include <string_view>

namespace sgre::audio {

	// Authored listening balance, independent of recovered script volume and fades.
	struct UserAudioSettings {
		std::uint32_t Master = 100;
		std::uint32_t Music = 55;
		std::uint32_t Voice = 100;
		std::uint32_t Effects = 80;
		bool operator==(const UserAudioSettings&) const = default;
	};

	void ValidateUserAudioSettings(const UserAudioSettings& settings);
	[[nodiscard]] float UserAudioGroupGain(const UserAudioSettings& settings, std::string_view original_group);

} // namespace sgre::audio
