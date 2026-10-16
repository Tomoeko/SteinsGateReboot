#pragma once

#include "sgre/resource/sound.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace sgre::audio {

	// The encoded storage owner must outlive this reader. Bit offsets use MSB-first order within each byte.
	class WmaBitReader {
	public:
		explicit WmaBitReader(std::span<const std::uint8_t> bytes);
		[[nodiscard]] std::uint32_t ReadBits(std::uint8_t count);
		[[nodiscard]] std::uint32_t PeekBits(std::uint8_t count) const;
		void SkipBits(std::size_t count);
		[[nodiscard]] std::size_t PositionBits() const noexcept;
		[[nodiscard]] std::size_t RemainingBits() const noexcept;

	private:
		std::span<const std::uint8_t> m_Bytes;
		std::size_t m_PositionBits = 0;
	};

	struct WmaHuffmanCode {
		std::uint32_t Symbol = 0;
		std::uint32_t CodeBits = 0;
		std::uint8_t BitCount = 0;
	};

	class WmaHuffmanDecoder {
	public:
		static constexpr std::size_t kMaxCodeCount = 4096;
		static constexpr std::size_t kMaxNodeCount = kMaxCodeCount * 32 + 1;

		explicit WmaHuffmanDecoder(std::span<const WmaHuffmanCode> codes, std::size_t max_nodes = kMaxNodeCount);
		[[nodiscard]] std::uint32_t DecodeSymbol(WmaBitReader& reader) const;

	private:
		struct Node {
			std::array<std::size_t, 2> Children = {static_cast<std::size_t>(-1), static_cast<std::size_t>(-1)};
			std::optional<std::uint32_t> Symbol;
		};

		std::vector<Node> m_Nodes;
		std::uint8_t m_MaxBitCount = 0;
	};

	[[nodiscard]] std::span<const WmaHuffmanCode> WmaExponentCodebook() noexcept;
	// Standard exponent symbols 0–120 encode signed differences centered at 60.
	[[nodiscard]] std::int32_t DecodeWmaExponentDelta(WmaBitReader& reader, const WmaHuffmanDecoder& decoder);

	struct WmaPacketHeader {
		std::uint8_t Sequence = 0;
		// Preserve the encoded nibble; frame completion and reservoir accounting remain unresolved.
		std::uint8_t FrameCountField = 0;
		std::uint32_t DecodedSampleCount = 0;
	};

	// Reads only the established packet prefix and decoded-byte ledger. This does not decode audio samples.
	[[nodiscard]] std::vector<WmaPacketHeader> ReadWmaPacketHeaders(const resource::WaveFormat& format, std::span<const std::uint8_t> encoded, std::span<const std::uint32_t> decoded_packet_cumulative_bytes, std::size_t max_packets = 262144);

} // namespace sgre::audio
