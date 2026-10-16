#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace sgre::media {

	enum class OpusMode {
		Silk,
		Hybrid,
		Celt
	};

	enum class OpusBandwidth {
		Narrow,
		Medium,
		Wide,
		SuperWide,
		Full
	};

	struct OpusHeader {
		std::uint8_t Version = 0;
		std::uint8_t Channels = 0;
		std::uint16_t PreSkipSamples = 0;
		std::uint32_t OriginalSamplesPerSecond = 0;
		std::int16_t OutputGainQ8Decibels = 0;
	};

	struct OpusFrameRange {
		std::size_t Offset = 0;
		std::size_t Size = 0;
	};

	struct OpusPacketInfo {
		OpusMode Mode = OpusMode::Silk;
		OpusBandwidth Bandwidth = OpusBandwidth::Narrow;
		std::uint32_t SamplesPerFrame = 0;
		std::uint32_t TotalSamples = 0;
		bool Stereo = false;
		std::vector<OpusFrameRange> Frames;
	};

	[[nodiscard]] OpusHeader ReadOpusHeader(std::span<const std::uint8_t> bytes);
	// Frame ranges borrow the encoded packet; sample counts are always in 48 kHz units.
	[[nodiscard]] OpusPacketInfo ReadOpusPacket(std::span<const std::uint8_t> bytes);

} // namespace sgre::media
