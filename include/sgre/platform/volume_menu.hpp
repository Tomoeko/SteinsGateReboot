#pragma once

#include "sgre/audio/user_audio_settings.hpp"

#include <functional>
#include <memory>
#include <optional>

namespace sgre::render {
	class SpritePassPresentation;
}

namespace sgre::platform {

	class VolumeMenu {
	public:
		VolumeMenu(render::SpritePassPresentation& presentation, audio::UserAudioSettings settings, std::function<void()> on_open);
		~VolumeMenu();
		VolumeMenu(const VolumeMenu&) = delete;
		VolumeMenu& operator=(const VolumeMenu&) = delete;
		void Toggle();
		void Update();
		[[nodiscard]] bool CapturesInput() const noexcept;
		[[nodiscard]] audio::UserAudioSettings Settings() const noexcept;
		[[nodiscard]] std::optional<audio::UserAudioSettings> TakeChanges();

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::platform
