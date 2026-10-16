#pragma once

#include "sgre/media/movie_playback.hpp"

#include <cstddef>

namespace sgre::media {

	// Call on the decoder worker. This macOS service retains matrix/range and prepares native bottom-up RGB32.
	[[nodiscard]] MovieRgb32Frame ConvertMovieRgb32Frame(const Nv12VideoFrame& frame, std::size_t maximum_frame_bytes = 64 * 1024 * 1024);

} // namespace sgre::media
