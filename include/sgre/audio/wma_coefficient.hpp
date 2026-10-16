#pragma once

#include "sgre/audio/wma_packet.hpp"

#include <cstdint>

namespace sgre::audio {

	enum class WmaCoefficientBook {
		SetOneMain,
		SetOneAlternate,
		SetTwoMain,
		SetTwoAlternate,
		SetThreeMain,
		SetThreeAlternate
	};

	struct WmaGainLimits {
		static constexpr std::uint16_t kMaxGroupCount = 64;
		std::uint32_t MaxGain = 4096;
		std::uint16_t MaxGroups = kMaxGroupCount;
	};

	struct WmaCodingHeader {
		bool MidSideStereo = false;
		std::array<bool, 2> ChannelCoded = {false, false};
		std::uint32_t TotalGain = 1;
	};

	[[nodiscard]] WmaCodingHeader ReadWmaCodingHeader(WmaBitReader& reader, std::uint16_t channels, WmaGainLimits limits = {});
	[[nodiscard]] std::uint8_t WmaEscapeMagnitudeBits(std::uint32_t total_gain) noexcept;
	[[nodiscard]] WmaCoefficientBook SelectWmaCoefficientBook(const resource::WaveFormat& format, bool mid_side_stereo, std::uint16_t channel_index);

	struct WmaCoefficientEscapeWidths {
		static constexpr std::uint8_t kMaxNativeFieldBits = 24;
		std::uint8_t MagnitudeBits = 0;
		std::uint8_t ZeroRunBits = 0;
	};

	enum class WmaCoefficientKind {
		Value,
		EndOfBlock
	};

	struct WmaCoefficient {
		WmaCoefficientKind Kind = WmaCoefficientKind::Value;
		std::uint32_t ZeroRun = 0;
		std::uint32_t Magnitude = 0;
		bool IsNegative = false;
	};

	// Construct once for a selected native book; decoding performs no allocation and commits complete values only.
	class WmaCoefficientDecoder {
	public:
		explicit WmaCoefficientDecoder(WmaCoefficientBook book);
		[[nodiscard]] WmaCoefficient Decode(WmaBitReader& reader, WmaCoefficientEscapeWidths escape_widths) const;

	private:
		WmaCoefficientBook m_Book;
		WmaHuffmanDecoder m_Huffman;
	};

} // namespace sgre::audio
