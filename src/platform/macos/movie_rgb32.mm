#include "sgre/media/movie_rgb32.hpp"

#import <Accelerate/Accelerate.h>

#include <array>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace sgre::media {

	static const vImage_YpCbCrToARGBMatrix* MovieColorMatrix(VideoColorMatrix matrix) {
		switch (matrix) {
		case VideoColorMatrix::Bt601:
			return kvImage_YpCbCrToARGBMatrix_ITU_R_601_4;
		case VideoColorMatrix::Bt709:
			return kvImage_YpCbCrToARGBMatrix_ITU_R_709_2;
		default:
			throw std::runtime_error("movie RGB32 conversion requires a verified BT.601 or BT.709 matrix");
		}
	}

	MovieRgb32Frame ConvertMovieRgb32Frame(const Nv12VideoFrame& frame, std::size_t maximum_frame_bytes) {
		if (frame.Width == 0 || frame.Height == 0 || frame.Width > 16384 || frame.Height > 16384 || frame.Width % 2 != 0 || frame.Height % 2 != 0) {
			throw std::runtime_error("movie RGB32 conversion requires bounded even NV12 dimensions");
		}
		const auto luma_bytes = static_cast<std::size_t>(frame.Width) * frame.Height;
		const auto padded_width = (frame.Width + 15u) & ~15u;
		const auto padded_height = (frame.Height + 15u) & ~15u;
		const auto row_bytes = static_cast<std::size_t>(padded_width) * 4;
		if (frame.Luma.size() != luma_bytes || frame.Chroma.size() != luma_bytes / 2 || row_bytes > maximum_frame_bytes / padded_height) {
			throw std::runtime_error("movie RGB32 conversion has truncated planes or exceeds its frame byte limit");
		}
		const auto* matrix = MovieColorMatrix(frame.Matrix);
		vImage_YpCbCrPixelRange range{};
		range.Yp_bias = frame.FullRange ? 0 : 16;
		range.CbCr_bias = 128;
		range.YpRangeMax = frame.FullRange ? 255 : 235;
		range.CbCrRangeMax = frame.FullRange ? 255 : 240;
		range.YpMax = 255;
		range.YpMin = 0;
		range.CbCrMax = 255;
		range.CbCrMin = 0;
		vImage_YpCbCrToARGB conversion{};
		const auto prepared = vImageConvert_YpCbCrToARGB_GenerateConversion(matrix, &range, &conversion, kvImage420Yp8_CbCr8, kvImageARGB8888, kvImageNoFlags);
		if (prepared != kvImageNoError) {
			throw std::runtime_error("movie RGB32 conversion setup failed: " + std::to_string(prepared));
		}

		std::vector<std::uint8_t> top_down(luma_bytes * 4);
		// vImage's foreign buffer descriptor stores void* even for these const source buffers.
		const vImage_Buffer luma{const_cast<std::uint8_t*>(frame.Luma.data()), frame.Height, frame.Width, frame.Width};
		const vImage_Buffer chroma{const_cast<std::uint8_t*>(frame.Chroma.data()), frame.Height / 2u, frame.Width / 2u, frame.Width};
		const vImage_Buffer destination{top_down.data(), frame.Height, frame.Width, static_cast<std::size_t>(frame.Width) * 4};
		const std::array<std::uint8_t, 4> bgra{3, 2, 1, 0};
		const auto converted = vImageConvert_420Yp8_CbCr8ToARGB8888(&luma, &chroma, &destination, &conversion, bgra.data(), 255, kvImageNoFlags);
		if (converted != kvImageNoError) {
			throw std::runtime_error("movie RGB32 conversion failed: " + std::to_string(converted));
		}

		MovieRgb32Frame result;
		result.Width = frame.Width;
		result.Height = frame.Height;
		result.PaddedWidth = padded_width;
		result.PaddedHeight = padded_height;
		result.RowBytes = row_bytes;
		result.Bytes.resize(row_bytes * padded_height);
		const auto visible_row_bytes = static_cast<std::size_t>(frame.Width) * 4;
		for (std::size_t row = 0; row < frame.Height; ++row) {
			std::memcpy(result.Bytes.data() + (padded_height - 1 - row) * row_bytes, top_down.data() + row * visible_row_bytes, visible_row_bytes);
		}

		return result;
	}

} // namespace sgre::media
