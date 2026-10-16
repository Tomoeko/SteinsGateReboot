#include "sgre/audio/wma_stream.hpp"

#include "sgre/audio/wma_packet.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>

namespace sgre::audio {

	static std::uint32_t FrameCoefficientCount(std::uint32_t sample_rate) noexcept {
		if (sample_rate <= 16000) {
			return 512;
		}

		if (sample_rate <= 22050) {
			return 1024;
		}

		return 2048;
	}

	static std::uint8_t ByteOffsetBits(const resource::WaveFormat& format, std::uint32_t frame_coefficients) {
		constexpr std::uint32_t kMaxMeanFrameBytes = (1 << 20) - 1;
		const auto denominator = static_cast<std::uint32_t>(format.Channels) * format.SamplesPerSecond;
		const float bits_per_sample = static_cast<float>(format.AverageBytesPerSecond) * 8.0f / static_cast<float>(denominator);
		const float frame_bytes = static_cast<float>(frame_coefficients) * bits_per_sample * 0.125f;
		const float rounded = frame_bytes + 0.5f;
		if (!(rounded >= 1.0f && rounded < static_cast<float>(kMaxMeanFrameBytes + 1))) {
			throw std::runtime_error("WMA mean encoded frame size cannot produce a bounded reservoir offset");
		}

		const auto byte_count = static_cast<std::uint32_t>(rounded);

		return static_cast<std::uint8_t>(std::bit_width(byte_count) + 1);
	}

	WmaStandardParameters DescribeWmaStandard(const resource::WaveFormat& format, std::uint16_t encode_options) {
		constexpr std::uint16_t kKnownOptions = 0x1f;
		constexpr std::uint16_t kVlcExponentOption = 1;
		constexpr std::uint16_t kReservoirOption = 2;
		constexpr std::uint16_t kVariableBlocksOption = 4;
		constexpr std::uint32_t kMinimumCoefficients = 128;
		if (format.Encoding != resource::WaveEncoding::WmaStandard || format.Channels == 0 || format.Channels > 2 || format.BitsPerSample != 16 || format.SamplesPerSecond == 0 || format.SamplesPerSecond > 48000 || format.AverageBytesPerSecond == 0 || format.AverageBytesPerSecond > std::numeric_limits<std::int32_t>::max() || format.BlockAlignment == 0) {
			throw std::runtime_error("WMA Standard parameters require a valid mono/stereo format");
		}

		if ((encode_options & ~kKnownOptions) != 0) {
			throw std::runtime_error("WMA Standard encode option variant is not recovered");
		}

		WmaStandardParameters parameters;
		parameters.EncodeOptions = encode_options;
		parameters.PacketBytes = format.BlockAlignment;
		parameters.FrameCoefficientCount = FrameCoefficientCount(format.SamplesPerSecond);
		parameters.FrameCoefficientBits = static_cast<std::uint8_t>(std::bit_width(parameters.FrameCoefficientCount) - 1);
		parameters.HasReservoir = (encode_options & kReservoirOption) != 0;
		parameters.HasVariableBlocks = parameters.HasReservoir && (encode_options & kVariableBlocksOption) != 0;
		parameters.HasVlcExponents = (encode_options & kVlcExponentOption) != 0;
		parameters.ByteOffsetBits = ByteOffsetBits(format, parameters.FrameCoefficientCount);
		parameters.MaximumBlockCount = 1;
		if (parameters.HasVariableBlocks) {
			const std::uint32_t base_count = format.AverageBytesPerSecond / format.Channels < 4000 ? 2 : 8;
			const auto option_shift = (encode_options >> 3) & 3;
			parameters.MaximumBlockCount = static_cast<std::uint16_t>(std::min(base_count << option_shift, parameters.FrameCoefficientCount / kMinimumCoefficients));
		}

		parameters.MinimumBlockCoefficients = parameters.FrameCoefficientCount / parameters.MaximumBlockCount;
		parameters.BlockCountBits = static_cast<std::uint8_t>(std::bit_width(parameters.MaximumBlockCount));
		if (parameters.HasVariableBlocks) {
			const auto ratio_bits = static_cast<std::uint16_t>(parameters.BlockCountBits - 1);
			parameters.BlockLengthIndexBits = static_cast<std::uint8_t>(std::bit_width(ratio_bits));
		}

		if (parameters.HasReservoir) {
			parameters.ContinuationBits = parameters.ByteOffsetBits + 3;
			parameters.PacketHeaderBits = parameters.ByteOffsetBits + 11;
			if (parameters.ContinuationBits > 24 || parameters.PacketHeaderBits > 32 || static_cast<std::uint32_t>(parameters.PacketBytes) * 8 < parameters.PacketHeaderBits) {
				throw std::runtime_error("WMA reservoir header exceeds its native width or packet extent");
			}
		}

		return parameters;
	}

	WmaReservoirHeader ReadWmaReservoirHeader(std::span<const std::uint8_t> packet, const WmaStandardParameters& parameters) {
		if (!parameters.HasReservoir || parameters.ContinuationBits == 0 || parameters.ContinuationBits > 24 || parameters.PacketHeaderBits != parameters.ContinuationBits + 8 || packet.size() != parameters.PacketBytes) {
			throw std::runtime_error("WMA reservoir packet or parameter extent differs");
		}

		WmaBitReader reader(packet);
		WmaReservoirHeader header;
		header.Sequence = static_cast<std::uint8_t>(reader.ReadBits(4));
		header.FrameCountField = static_cast<std::uint8_t>(reader.ReadBits(4));
		header.ContinuationBitCount = reader.ReadBits(parameters.ContinuationBits);
		header.HeaderBitCount = parameters.PacketHeaderBits;
		if (header.ContinuationBitCount > reader.RemainingBits()) {
			throw std::runtime_error("WMA frame continuation exceeds the packet payload");
		}

		return header;
	}

} // namespace sgre::audio
