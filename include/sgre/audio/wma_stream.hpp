#pragma once

#include "sgre/resource/sound.hpp"

#include <cstdint>
#include <span>

namespace sgre::audio {

	struct WmaStandardParameters {
		std::uint16_t EncodeOptions = 0;
		std::uint16_t PacketBytes = 0;
		std::uint32_t FrameCoefficientCount = 0;
		std::uint32_t MinimumBlockCoefficients = 0;
		std::uint16_t MaximumBlockCount = 0;
		std::uint8_t FrameCoefficientBits = 0;
		std::uint8_t ByteOffsetBits = 0;
		std::uint8_t ContinuationBits = 0;
		std::uint8_t PacketHeaderBits = 0;
		std::uint8_t BlockCountBits = 0;
		std::uint8_t BlockLengthIndexBits = 0;
		bool HasReservoir = false;
		bool HasVariableBlocks = false;
		bool HasVlcExponents = false;
	};

	// Encode options are explicit: original empty xWMA extensions do not themselves establish decoder defaults.
	[[nodiscard]] WmaStandardParameters DescribeWmaStandard(const resource::WaveFormat& format, std::uint16_t encode_options);

	struct WmaReservoirHeader {
		std::uint8_t Sequence = 0;
		std::uint8_t FrameCountField = 0;
		std::uint32_t ContinuationBitCount = 0;
		std::uint8_t HeaderBitCount = 0;
	};

	[[nodiscard]] WmaReservoirHeader ReadWmaReservoirHeader(std::span<const std::uint8_t> packet, const WmaStandardParameters& parameters);

} // namespace sgre::audio
