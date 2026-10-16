#pragma once

#include "sgre/media/movie_playback.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

namespace sgre::scene {

	enum class MoviePlaybackStatus : std::uint32_t {
		Stopped = 0,
		Playing = 1,
		Paused = 2
	};

	struct MovieLayerState {
		MoviePlaybackStatus Status = MoviePlaybackStatus::Stopped;
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
		std::array<float, 2> Position{};
		std::array<float, 2> Extent{};
		std::uint32_t Opacity = 255;
		std::uint32_t FilteringX = 0;
		std::uint32_t FilteringY = 0;
		float Volume = 1;
		double PositionSeconds = 0;
		bool UseAlpha = false;
		bool Closed = false;
	};

	class MovieLayerTask {
	public:
		// Services are the actual stream/clock/frame receiver and enclose this deferred First task.
		explicit MovieLayerTask(media::MoviePlaybackServices services);
		~MovieLayerTask();
		MovieLayerTask(const MovieLayerTask&) = delete;
		MovieLayerTask& operator=(const MovieLayerTask&) = delete;
		[[nodiscard]] MovieLayerState State() const noexcept;
		void Play(std::string_view storage);
		void Stop();
		void Pause();
		void Unpause();
		void SetOpacity(std::uint32_t opacity);
		void SetFiltering(std::uint32_t horizontal, std::uint32_t vertical);
		void SetVolume(float volume);
		void SetUseAlpha(bool enabled);
		// Native Update consumes only the asynchronous stop request; Draw advances the media clock.
		void Update();
		void Draw();
		[[nodiscard]] bool CanClose() const;
		void Close();

	private:
		void RequireIdle() const;
		void StopStream() noexcept;
		void ProcessVideo(std::int64_t position100ns);
		void DrawCurrent();
		media::MoviePlaybackServices m_Services;
		MovieLayerState m_State;
		std::unique_ptr<media::MoviePlaybackStream> m_Stream;
		media::MovieStreamDescription m_Description;
		std::optional<media::MovieVideoSample> m_Current;
		std::optional<std::int64_t> m_ClockBase100ns;
		std::uint64_t m_FrameSerial = 0;
		std::uint32_t m_AudioAssembledBlocks = 0;
		std::uint32_t m_AudioAcknowledgedBlocks = 0;
		double m_DurationSeconds = 0;
		bool m_VideoInputEnded = false;
		bool m_VideoFinished = false;
		bool m_AudioFinished = false;
		bool m_AlternateDraw = false;
		bool m_Operating = false;
	};

} // namespace sgre::scene
