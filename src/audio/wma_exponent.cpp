#include "sgre/audio/wma_exponent.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <stdexcept>

namespace sgre::audio {

	static constexpr std::array<std::uint32_t, 25> kBandFrequencies = {
	    100,
	    200,
	    300,
	    400,
	    510,
	    630,
	    770,
	    920,
	    1080,
	    1270,
	    1480,
	    1720,
	    2000,
	    2320,
	    2700,
	    3150,
	    3700,
	    4400,
	    5300,
	    6400,
	    7700,
	    9500,
	    12000,
	    15500,
	    24500};

	static constexpr std::array<std::uint32_t, 18> kBandEnds512 = {
	    0,
	    4,
	    12,
	    20,
	    24,
	    36,
	    48,
	    56,
	    64,
	    88,
	    104,
	    124,
	    148,
	    180,
	    220,
	    280,
	    360,
	    512};

	static constexpr std::array<std::uint32_t, 16> kBandEnds256 = {
	    0,
	    4,
	    12,
	    16,
	    24,
	    32,
	    36,
	    44,
	    52,
	    64,
	    76,
	    88,
	    112,
	    140,
	    180,
	    256};

	static constexpr std::array<std::uint32_t, 13> kBandEnds128 = {
	    0,
	    4,
	    8,
	    12,
	    16,
	    20,
	    28,
	    36,
	    44,
	    56,
	    72,
	    92,
	    128};

	std::vector<std::uint32_t> WmaExponentBandEnds(std::uint32_t sample_rate, std::uint32_t block_coefficients) {
		if (sample_rate < 44100 || sample_rate > 48000 || block_coefficients < 128 || block_coefficients > 2048 || !std::has_single_bit(block_coefficients)) {
			throw std::runtime_error("WMA exponent bands require a recovered 44.1–48 kHz block profile");
		}

		if (block_coefficients == 128) {
			return {kBandEnds128.begin(), kBandEnds128.end()};
		}

		if (block_coefficients == 256) {
			return {kBandEnds256.begin(), kBandEnds256.end()};
		}

		if (block_coefficients == 512) {
			return {kBandEnds512.begin(), kBandEnds512.end()};
		}

		std::vector<std::uint32_t> ends;
		ends.reserve(kBandFrequencies.size() + 1);
		ends.push_back(0);
		// Native 0x180003e84 preserves these separate single-precision operations before rounding to multiples of four.
		const float reciprocal_rate = 1.0f / static_cast<float>(sample_rate);
		const float transform_per_rate = static_cast<float>(block_coefficients * 2) * reciprocal_rate;
		for (const auto frequency : kBandFrequencies) {
			const float frequency_position = static_cast<float>(frequency) * transform_per_rate;
			const float rounded_position = (frequency_position + 2.0f) * 0.25f;
			const auto end = static_cast<std::uint32_t>(rounded_position) * 4;
			if (end > ends.back()) {
				ends.push_back(end);
			}

			if (end >= block_coefficients) {
				break;
			}
		}

		if (ends.size() < 2) {
			throw std::runtime_error("WMA exponent band table did not cover its block");
		}

		ends.back() = block_coefficients;

		return ends;
	}

	WmaExponentValues DecodeWmaExponentValues(WmaBitReader& reader, const WmaHuffmanDecoder& decoder, std::size_t band_count) {
		constexpr std::size_t kMaximumBands = 25;
		if (band_count == 0 || band_count > kMaximumBands) {
			throw std::runtime_error("WMA exponent band count exceeds the recovered Standard profile");
		}

		auto staged_reader = reader;
		WmaExponentValues values;
		values.Bands.reserve(band_count);
		std::int32_t exponent = 36;
		for (std::size_t band = 0; band < band_count; ++band) {
			exponent += DecodeWmaExponentDelta(staged_reader, decoder);
			values.Bands.push_back(exponent);
		}

		values.MaximumExponent = *std::max_element(values.Bands.begin(), values.Bands.end());
		reader = staged_reader;

		return values;
	}

} // namespace sgre::audio
