#include "sgre/audio/wave_decoder.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <stdexcept>

namespace sgre::audio {

	static std::uint16_t ReadWord(std::span<const std::uint8_t> bytes, std::size_t offset) {
		return static_cast<std::uint16_t>(bytes[offset] | (static_cast<std::uint16_t>(bytes[offset + 1]) << 8));
	}

	static std::int16_t ReadSignedWord(std::span<const std::uint8_t> bytes, std::size_t offset) {
		return std::bit_cast<std::int16_t>(ReadWord(bytes, offset));
	}

	static std::size_t CheckedSampleCount(std::size_t frames, std::size_t channels, std::size_t limit) {
		if (channels == 0 || frames > limit / sizeof(float) / channels) {
			throw std::runtime_error("decoded audio exceeds its allocation limit");
		}

		return frames * channels;
	}

	static std::vector<float> DecodePcm(const resource::WaveFormat& format, std::span<const std::uint8_t> bytes, std::size_t limit) {
		if ((format.BitsPerSample != 8 && format.BitsPerSample != 16 && format.BitsPerSample != 24 && format.BitsPerSample != 32) || format.BlockAlignment != format.Channels * (format.BitsPerSample / 8) || bytes.size() % format.BlockAlignment != 0) {
			throw std::runtime_error("PCM sample width, block alignment, or payload length is invalid");
		}

		const auto count = CheckedSampleCount(bytes.size() / format.BlockAlignment, format.Channels, limit);
		const auto width = static_cast<std::size_t>(format.BitsPerSample / 8);
		std::vector<float> samples;
		samples.reserve(count);
		for (std::size_t offset = 0; offset < bytes.size(); offset += width) {
			if (width == 1) {
				samples.push_back(static_cast<float>(static_cast<std::int32_t>(bytes[offset]) - 128) / 128);
				continue;
			}

			std::uint32_t bits = 0;
			for (std::size_t index = 0; index < width; ++index) {
				bits |= static_cast<std::uint32_t>(bytes[offset + index]) << (8 * index);
			}

			if (width < 4 && (bytes[offset + width - 1] & 0x80) != 0) {
				bits |= std::numeric_limits<std::uint32_t>::max() << (width * 8);
			}

			const auto value = std::bit_cast<std::int32_t>(bits);
			const float divisor = width == 2 ? 32768.0F : width == 3 ? 8388608.0F
			                                                         : 2147483648.0F;
			samples.push_back(static_cast<float>(value) / divisor);
		}

		return samples;
	}

	struct AdpcmState {
		std::int32_t Sample1 = 0;
		std::int32_t Sample2 = 0;
		std::int32_t Coefficient1 = 0;
		std::int32_t Coefficient2 = 0;
		std::int64_t Delta = 0;
	};

	static std::int32_t DecodeNibble(AdpcmState& state, std::uint8_t nibble) {
		static constexpr std::array<std::int32_t, 16> kAdaptation{230, 230, 230, 230, 307, 409, 512, 614, 768, 614, 512, 409, 307, 230, 230, 230};
		const auto prediction = (static_cast<std::int64_t>(state.Sample1) * state.Coefficient1 + static_cast<std::int64_t>(state.Sample2) * state.Coefficient2) / 256;
		const auto signed_nibble = static_cast<std::int32_t>(nibble) - ((nibble & 8) != 0 ? 16 : 0);
		const auto sample = static_cast<std::int32_t>(std::clamp<std::int64_t>(prediction + signed_nibble * state.Delta, -32768, 32767));
		state.Sample2 = state.Sample1;
		state.Sample1 = sample;
		state.Delta = std::max<std::int64_t>(16, state.Delta * kAdaptation[nibble] / 256);
		// A corrupt stream must not grow its predictor scale beyond checked arithmetic.
		if (state.Delta > std::numeric_limits<std::int32_t>::max()) {
			throw std::runtime_error("ADPCM adaptation scale exceeds its safe range");
		}

		return sample;
	}

	static std::vector<float> DecodeAdpcm(const resource::WaveFormat& format, std::span<const std::uint8_t> bytes, std::size_t limit) {
		if (format.Channels == 0 || format.Channels > 2 || format.BitsPerSample != 4 || format.Extra.size() < 4 || format.BlockAlignment < 7 * format.Channels) {
			throw std::runtime_error("Microsoft ADPCM format header is invalid");
		}

		const auto samples_per_block = ReadWord(format.Extra, 0);
		const auto coefficient_count = ReadWord(format.Extra, 2);
		if (coefficient_count == 0 || coefficient_count > 256 || format.Extra.size() != 4 + static_cast<std::size_t>(coefficient_count) * 4 || samples_per_block != ((format.BlockAlignment - 7 * format.Channels) * 2 / format.Channels) + 2) {
			throw std::runtime_error("Microsoft ADPCM block/coefficient counts are inconsistent");
		}

		if (bytes.size() % format.BlockAlignment != 0) {
			throw std::runtime_error("Microsoft ADPCM payload has a partial block");
		}

		const auto blocks = bytes.size() / format.BlockAlignment;
		if (blocks > limit / sizeof(float) / format.Channels / samples_per_block) {
			throw std::runtime_error("ADPCM decoded audio exceeds its allocation limit");
		}

		std::vector<float> samples;
		samples.reserve(CheckedSampleCount(blocks * samples_per_block, format.Channels, limit));
		for (std::size_t offset = 0; offset < bytes.size(); offset += format.BlockAlignment) {
			const auto block = bytes.subspan(offset, format.BlockAlignment);
			std::array<AdpcmState, 2> states{};
			for (std::size_t channel = 0; channel < format.Channels; ++channel) {
				const auto predictor = block[channel];
				if (predictor >= coefficient_count) {
					throw std::runtime_error("Microsoft ADPCM predictor exceeds coefficient table");
				}

				auto& state = states[channel];
				state.Coefficient1 = ReadSignedWord(format.Extra, 4 + predictor * 4);
				state.Coefficient2 = ReadSignedWord(format.Extra, 6 + predictor * 4);
				state.Delta = ReadWord(block, format.Channels + channel * 2);
				state.Sample1 = ReadSignedWord(block, format.Channels * 3 + channel * 2);
				state.Sample2 = ReadSignedWord(block, format.Channels * 5 + channel * 2);
			}

			for (std::size_t channel = 0; channel < format.Channels; ++channel) {
				samples.push_back(static_cast<float>(states[channel].Sample2) / 32768);
			}

			for (std::size_t channel = 0; channel < format.Channels; ++channel) {
				samples.push_back(static_cast<float>(states[channel].Sample1) / 32768);
			}

			for (std::size_t position = 7 * format.Channels; position < block.size(); ++position) {
				samples.push_back(static_cast<float>(DecodeNibble(states[0], static_cast<std::uint8_t>(block[position] >> 4))) / 32768);
				samples.push_back(static_cast<float>(DecodeNibble(states[format.Channels - 1], static_cast<std::uint8_t>(block[position] & 15))) / 32768);
			}
		}

		return samples;
	}

	DecodedAudio DecodeWaveAudio(const resource::WaveFormat& format, std::span<const std::uint8_t> bytes, std::size_t max_decoded_bytes) {
		if (format.Channels == 0 || format.Channels > 8 || format.SamplesPerSecond == 0 || format.BlockAlignment == 0) {
			throw std::runtime_error("audio format requires positive channels, rate, and block alignment");
		}

		DecodedAudio audio;
		audio.SamplesPerSecond = format.SamplesPerSecond;
		audio.Channels = format.Channels;
		if (format.Encoding == resource::WaveEncoding::Pcm) {
			audio.Samples = DecodePcm(format, bytes, max_decoded_bytes);
		} else if (format.Encoding == resource::WaveEncoding::MicrosoftAdpcm) {
			audio.Samples = DecodeAdpcm(format, bytes, max_decoded_bytes);
		} else {
			throw std::runtime_error("first-party WMA sample decoding is not recovered yet");
		}

		return audio;
	}

} // namespace sgre::audio
