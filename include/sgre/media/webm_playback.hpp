#pragma once

#include "sgre/media/movie_playback.hpp"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string_view>

namespace sgre::media {

	class MovieSilentAudioRegistry;

	struct WebmMoviePlaybackLimits {
		std::size_t QueuedVideoFrames = 3;
		std::size_t QueuedAudioBlocks = 8;
		std::size_t MaximumFrameBytes = 64 * 1024 * 1024;
	};

	// Opens the actual encrypted file/seed; demux, VP9 decoding and RGB32 conversion run on its owned worker.
	// This overload retains the explicit audio-service gate.
	[[nodiscard]] std::unique_ptr<MoviePlaybackStream> OpenWebmMoviePlayback(const std::filesystem::path& path, std::string_view seed, WebmMoviePlaybackLimits limits = {});

	[[nodiscard]] std::unique_ptr<MoviePlaybackStream> OpenWebmMoviePlayback(const std::filesystem::path& path, std::string_view seed, MovieSilentAudioRegistry& actual_audio, WebmMoviePlaybackLimits limits = {});

} // namespace sgre::media
