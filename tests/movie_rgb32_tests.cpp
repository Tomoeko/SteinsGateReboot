#include "sgre/media/movie_rgb32.hpp"

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <stdexcept>

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static void TestRangeAndOrientation(bool full_range, sgre::media::VideoColorMatrix matrix) {
	sgre::media::Nv12VideoFrame frame;
	frame.Width = 18;
	frame.Height = 18;
	frame.Matrix = matrix;
	frame.FullRange = full_range;
	frame.Luma.resize(18 * 18, full_range ? 0 : 16);
	frame.Chroma.resize(18 * 9, 128);
	std::fill_n(frame.Luma.begin(), 18, full_range ? 255 : 235);
	const auto converted = sgre::media::ConvertMovieRgb32Frame(frame);
	Require(converted.Width == 18 && converted.Height == 18 && converted.PaddedWidth == 32 && converted.PaddedHeight == 32 && converted.RowBytes == 128 && converted.Bytes.size() == 4096, "movie RGB32 padding/row extent differs");
	for (std::size_t y = 0; y < 32; ++y) {
		for (std::size_t x = 0; x < 32; ++x) {
			const auto start = y * converted.RowBytes + x * 4;
			const bool visible = y >= 14 && x < 18;
			for (std::size_t channel = 0; channel < 4; ++channel) {
				const auto expected = visible ? channel == 3 || y == 31 ? 255u : 0u : 0u;
				Require(converted.Bytes[start + channel] == expected, "movie RGB32 black/white range, bottom-up orientation or padding differs");
			}
		}
	}
}

static void TestRejectedInput() {
	sgre::media::Nv12VideoFrame frame;
	frame.Width = 64;
	frame.Height = 64;
	frame.Matrix = sgre::media::VideoColorMatrix::Bt709;
	frame.Luma.resize(4096, 16);
	frame.Chroma.resize(2048, 128);
	for (std::size_t attempt = 0; attempt < 4; ++attempt) {
		auto invalid = frame;
		if (attempt == 0) {
			invalid.Matrix = sgre::media::VideoColorMatrix::Unspecified;
		} else if (attempt == 1) {
			invalid.Chroma.pop_back();
		} else if (attempt == 2) {
			invalid.Width = 63;
		}
		bool rejected = false;
		try {
			static_cast<void>(sgre::media::ConvertMovieRgb32Frame(invalid, attempt == 3 ? 16383 : 65536));
		} catch (const std::runtime_error&) {
			rejected = true;
		}
		Require(rejected, "movie RGB32 converter accepted missing matrix, truncated/odd planes or insufficient bound");
	}
}

int main() {
	try {
		for (const auto matrix : {sgre::media::VideoColorMatrix::Bt601, sgre::media::VideoColorMatrix::Bt709}) {
			TestRangeAndOrientation(false, matrix);
			TestRangeAndOrientation(true, matrix);
		}
		TestRejectedInput();
		std::cout << "movie RGB32 range/orientation/bounds passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
