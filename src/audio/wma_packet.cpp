#include "sgre/audio/wma_packet.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <string>

namespace sgre::audio {

	WmaBitReader::WmaBitReader(std::span<const std::uint8_t> bytes) : m_Bytes(bytes) {
		if (bytes.size() > std::numeric_limits<std::size_t>::max() / 8) {
			throw std::runtime_error("WMA bitstream extent overflows bit offsets");
		}
	}

	std::uint32_t WmaBitReader::PeekBits(std::uint8_t count) const {
		if (count > 32 || count > RemainingBits()) {
			throw std::runtime_error("WMA bit read at bit " + std::to_string(m_PositionBits) + " exceeds the available stream or 32-bit result");
		}

		std::uint32_t value = 0;
		for (std::uint8_t index = 0; index < count; ++index) {
			const std::size_t position = m_PositionBits + index;
			const std::uint32_t bit = (m_Bytes[position / 8] >> (7 - position % 8)) & 1;
			value = (value << 1) | bit;
		}

		return value;
	}

	std::uint32_t WmaBitReader::ReadBits(std::uint8_t count) {
		const auto value = PeekBits(count);
		m_PositionBits += count;

		return value;
	}

	void WmaBitReader::SkipBits(std::size_t count) {
		if (count > RemainingBits()) {
			throw std::runtime_error("WMA bit skip at bit " + std::to_string(m_PositionBits) + " exceeds the available stream");
		}

		m_PositionBits += count;
	}

	std::size_t WmaBitReader::PositionBits() const noexcept {
		return m_PositionBits;
	}

	std::size_t WmaBitReader::RemainingBits() const noexcept {
		return m_Bytes.size() * 8 - m_PositionBits;
	}

	WmaHuffmanDecoder::WmaHuffmanDecoder(std::span<const WmaHuffmanCode> codes, std::size_t max_nodes) {
		if (codes.empty() || codes.size() > kMaxCodeCount || max_nodes == 0 || max_nodes > kMaxNodeCount) {
			throw std::runtime_error("WMA Huffman code or node allocation limit is invalid");
		}

		m_Nodes.emplace_back();
		for (const auto& code : codes) {
			if (code.BitCount == 0 || code.BitCount > 32 || (code.BitCount < 32 && (code.CodeBits >> code.BitCount) != 0)) {
				throw std::runtime_error("WMA Huffman code has an invalid bit count or unused high bits");
			}

			std::size_t node_index = 0;
			for (std::uint8_t depth = 0; depth < code.BitCount; ++depth) {
				if (m_Nodes[node_index].Symbol.has_value()) {
					throw std::runtime_error("WMA Huffman code extends an existing symbol prefix");
				}

				const auto branch = (code.CodeBits >> (code.BitCount - depth - 1)) & 1;
				if (m_Nodes[node_index].Children[branch] == std::numeric_limits<std::size_t>::max()) {
					if (m_Nodes.size() >= max_nodes) {
						throw std::runtime_error("WMA Huffman tree exceeds its node allocation limit");
					}

					const auto next_index = m_Nodes.size();
					m_Nodes.emplace_back();
					m_Nodes[node_index].Children[branch] = next_index;
				}

				node_index = m_Nodes[node_index].Children[branch];
			}

			const auto& children = m_Nodes[node_index].Children;
			if (m_Nodes[node_index].Symbol.has_value() || children[0] != std::numeric_limits<std::size_t>::max() || children[1] != std::numeric_limits<std::size_t>::max()) {
				throw std::runtime_error("WMA Huffman code duplicates or prefixes an existing symbol");
			}

			m_Nodes[node_index].Symbol = code.Symbol;
			m_MaxBitCount = std::max(m_MaxBitCount, code.BitCount);
		}
	}

	std::uint32_t WmaHuffmanDecoder::DecodeSymbol(WmaBitReader& reader) const {
		auto trial_reader = reader;
		std::size_t node_index = 0;
		for (std::uint8_t depth = 0; depth < m_MaxBitCount; ++depth) {
			const auto branch = trial_reader.ReadBits(1);
			node_index = m_Nodes[node_index].Children[branch];
			if (node_index == std::numeric_limits<std::size_t>::max()) {
				throw std::runtime_error("WMA Huffman symbol at bit " + std::to_string(reader.PositionBits()) + " has no matching code");
			}

			if (m_Nodes[node_index].Symbol.has_value()) {
				reader = trial_reader;

				return *m_Nodes[node_index].Symbol;
			}
		}

		throw std::runtime_error("WMA Huffman symbol exceeds the maximum code length");
	}

	std::span<const WmaHuffmanCode> WmaExponentCodebook() noexcept {
		// Reconstructed symbol prefixes from the verified native Standard exponent table; see docs/recovery/wma.md.
		static constexpr std::array<WmaHuffmanCode, 121> kCodes = {
		    WmaHuffmanCode{0, 0x3ffe8, 18},
		    WmaHuffmanCode{1, 0x3ffe6, 18},
		    WmaHuffmanCode{2, 0x3ffe7, 18},
		    WmaHuffmanCode{3, 0x3ffe5, 18},
		    WmaHuffmanCode{4, 0x7fff5, 19},
		    WmaHuffmanCode{5, 0x7fff1, 19},
		    WmaHuffmanCode{6, 0x7ffed, 19},
		    WmaHuffmanCode{7, 0x7fff6, 19},
		    WmaHuffmanCode{8, 0x7ffee, 19},
		    WmaHuffmanCode{9, 0x7ffef, 19},
		    WmaHuffmanCode{10, 0x7fff0, 19},
		    WmaHuffmanCode{11, 0x7fffc, 19},
		    WmaHuffmanCode{12, 0x7fffd, 19},
		    WmaHuffmanCode{13, 0x7ffff, 19},
		    WmaHuffmanCode{14, 0x7fffe, 19},
		    WmaHuffmanCode{15, 0x7fff7, 19},
		    WmaHuffmanCode{16, 0x7fff8, 19},
		    WmaHuffmanCode{17, 0x7fffb, 19},
		    WmaHuffmanCode{18, 0x7fff9, 19},
		    WmaHuffmanCode{19, 0x3ffe4, 18},
		    WmaHuffmanCode{20, 0x7fffa, 19},
		    WmaHuffmanCode{21, 0x3ffe3, 18},
		    WmaHuffmanCode{22, 0x1ffef, 17},
		    WmaHuffmanCode{23, 0x1fff0, 17},
		    WmaHuffmanCode{24, 0xfff5, 16},
		    WmaHuffmanCode{25, 0x1ffee, 17},
		    WmaHuffmanCode{26, 0xfff2, 16},
		    WmaHuffmanCode{27, 0xfff3, 16},
		    WmaHuffmanCode{28, 0xfff4, 16},
		    WmaHuffmanCode{29, 0xfff1, 16},
		    WmaHuffmanCode{30, 0x7ff6, 15},
		    WmaHuffmanCode{31, 0x7ff7, 15},
		    WmaHuffmanCode{32, 0x3ff9, 14},
		    WmaHuffmanCode{33, 0x3ff5, 14},
		    WmaHuffmanCode{34, 0x3ff7, 14},
		    WmaHuffmanCode{35, 0x3ff3, 14},
		    WmaHuffmanCode{36, 0x3ff6, 14},
		    WmaHuffmanCode{37, 0x3ff2, 14},
		    WmaHuffmanCode{38, 0x1ff7, 13},
		    WmaHuffmanCode{39, 0x1ff5, 13},
		    WmaHuffmanCode{40, 0xff9, 12},
		    WmaHuffmanCode{41, 0xff7, 12},
		    WmaHuffmanCode{42, 0xff6, 12},
		    WmaHuffmanCode{43, 0x7f9, 11},
		    WmaHuffmanCode{44, 0xff4, 12},
		    WmaHuffmanCode{45, 0x7f8, 11},
		    WmaHuffmanCode{46, 0x3f9, 10},
		    WmaHuffmanCode{47, 0x3f7, 10},
		    WmaHuffmanCode{48, 0x3f5, 10},
		    WmaHuffmanCode{49, 0x1f8, 9},
		    WmaHuffmanCode{50, 0x1f7, 9},
		    WmaHuffmanCode{51, 0xfa, 8},
		    WmaHuffmanCode{52, 0xf8, 8},
		    WmaHuffmanCode{53, 0xf6, 8},
		    WmaHuffmanCode{54, 0x79, 7},
		    WmaHuffmanCode{55, 0x3a, 6},
		    WmaHuffmanCode{56, 0x38, 6},
		    WmaHuffmanCode{57, 0x1a, 5},
		    WmaHuffmanCode{58, 0xb, 4},
		    WmaHuffmanCode{59, 0x4, 3},
		    WmaHuffmanCode{60, 0x0, 1},
		    WmaHuffmanCode{61, 0xa, 4},
		    WmaHuffmanCode{62, 0xc, 4},
		    WmaHuffmanCode{63, 0x1b, 5},
		    WmaHuffmanCode{64, 0x39, 6},
		    WmaHuffmanCode{65, 0x3b, 6},
		    WmaHuffmanCode{66, 0x78, 7},
		    WmaHuffmanCode{67, 0x7a, 7},
		    WmaHuffmanCode{68, 0xf7, 8},
		    WmaHuffmanCode{69, 0xf9, 8},
		    WmaHuffmanCode{70, 0x1f6, 9},
		    WmaHuffmanCode{71, 0x1f9, 9},
		    WmaHuffmanCode{72, 0x3f4, 10},
		    WmaHuffmanCode{73, 0x3f6, 10},
		    WmaHuffmanCode{74, 0x3f8, 10},
		    WmaHuffmanCode{75, 0x7f5, 11},
		    WmaHuffmanCode{76, 0x7f4, 11},
		    WmaHuffmanCode{77, 0x7f6, 11},
		    WmaHuffmanCode{78, 0x7f7, 11},
		    WmaHuffmanCode{79, 0xff5, 12},
		    WmaHuffmanCode{80, 0xff8, 12},
		    WmaHuffmanCode{81, 0x1ff4, 13},
		    WmaHuffmanCode{82, 0x1ff6, 13},
		    WmaHuffmanCode{83, 0x1ff8, 13},
		    WmaHuffmanCode{84, 0x3ff8, 14},
		    WmaHuffmanCode{85, 0x3ff4, 14},
		    WmaHuffmanCode{86, 0xfff0, 16},
		    WmaHuffmanCode{87, 0x7ff4, 15},
		    WmaHuffmanCode{88, 0xfff6, 16},
		    WmaHuffmanCode{89, 0x7ff5, 15},
		    WmaHuffmanCode{90, 0x3ffe2, 18},
		    WmaHuffmanCode{91, 0x7ffd9, 19},
		    WmaHuffmanCode{92, 0x7ffda, 19},
		    WmaHuffmanCode{93, 0x7ffdb, 19},
		    WmaHuffmanCode{94, 0x7ffdc, 19},
		    WmaHuffmanCode{95, 0x7ffdd, 19},
		    WmaHuffmanCode{96, 0x7ffde, 19},
		    WmaHuffmanCode{97, 0x7ffd8, 19},
		    WmaHuffmanCode{98, 0x7ffd2, 19},
		    WmaHuffmanCode{99, 0x7ffd3, 19},
		    WmaHuffmanCode{100, 0x7ffd4, 19},
		    WmaHuffmanCode{101, 0x7ffd5, 19},
		    WmaHuffmanCode{102, 0x7ffd6, 19},
		    WmaHuffmanCode{103, 0x7fff2, 19},
		    WmaHuffmanCode{104, 0x7ffdf, 19},
		    WmaHuffmanCode{105, 0x7ffe7, 19},
		    WmaHuffmanCode{106, 0x7ffe8, 19},
		    WmaHuffmanCode{107, 0x7ffe9, 19},
		    WmaHuffmanCode{108, 0x7ffea, 19},
		    WmaHuffmanCode{109, 0x7ffeb, 19},
		    WmaHuffmanCode{110, 0x7ffe6, 19},
		    WmaHuffmanCode{111, 0x7ffe0, 19},
		    WmaHuffmanCode{112, 0x7ffe1, 19},
		    WmaHuffmanCode{113, 0x7ffe2, 19},
		    WmaHuffmanCode{114, 0x7ffe3, 19},
		    WmaHuffmanCode{115, 0x7ffe4, 19},
		    WmaHuffmanCode{116, 0x7ffe5, 19},
		    WmaHuffmanCode{117, 0x7ffd7, 19},
		    WmaHuffmanCode{118, 0x7ffec, 19},
		    WmaHuffmanCode{119, 0x7fff4, 19},
		    WmaHuffmanCode{120, 0x7fff3, 19}};

		return kCodes;
	}

	std::int32_t DecodeWmaExponentDelta(WmaBitReader& reader, const WmaHuffmanDecoder& decoder) {
		auto trial_reader = reader;
		const auto symbol = decoder.DecodeSymbol(trial_reader);
		if (symbol > 120) {
			throw std::runtime_error("WMA Standard exponent symbol exceeds its 121-symbol alphabet");
		}

		reader = trial_reader;

		return static_cast<std::int32_t>(symbol) - 60;
	}

	std::vector<WmaPacketHeader> ReadWmaPacketHeaders(const resource::WaveFormat& format, std::span<const std::uint8_t> encoded, std::span<const std::uint32_t> decoded_packet_cumulative_bytes, std::size_t max_packets) {
		if (format.Encoding != resource::WaveEncoding::WmaStandard || format.Channels == 0 || format.BitsPerSample != 16) {
			throw std::runtime_error("WMA packet reader requires Standard encoding with nonzero channels and 16-bit decoded samples");
		}

		if (!format.Extra.empty()) {
			throw std::runtime_error("WMA packet reader does not yet support codec extension variants");
		}

		if (format.BlockAlignment == 0 || encoded.empty() || encoded.size() % format.BlockAlignment != 0) {
			throw std::runtime_error("WMA encoded extent does not contain complete nonempty packets");
		}

		const std::size_t packet_count = encoded.size() / format.BlockAlignment;
		if (packet_count > max_packets || decoded_packet_cumulative_bytes.size() != packet_count) {
			throw std::runtime_error("WMA decoded-byte ledger count differs from the packet extent or exceeds the allocation limit");
		}

		const std::uint32_t decoded_sample_bytes = static_cast<std::uint32_t>(format.Channels) * 2;
		std::vector<WmaPacketHeader> headers;
		headers.reserve(packet_count);
		std::uint32_t previous_decoded_bytes = 0;
		for (std::size_t index = 0; index < packet_count; ++index) {
			WmaBitReader reader(encoded.subspan(index * format.BlockAlignment, format.BlockAlignment));
			WmaPacketHeader header;
			header.Sequence = static_cast<std::uint8_t>(reader.ReadBits(4));
			header.FrameCountField = static_cast<std::uint8_t>(reader.ReadBits(4));
			if (header.Sequence != index % 16) {
				throw std::runtime_error("WMA packet " + std::to_string(index) + " sequence differs from the complete resource stream");
			}

			const auto cumulative = decoded_packet_cumulative_bytes[index];
			if (cumulative < previous_decoded_bytes || (cumulative - previous_decoded_bytes) % decoded_sample_bytes != 0) {
				throw std::runtime_error("WMA packet " + std::to_string(index) + " has a decreasing or partial-sample decoded-byte ledger");
			}

			header.DecodedSampleCount = (cumulative - previous_decoded_bytes) / decoded_sample_bytes;
			previous_decoded_bytes = cumulative;
			headers.push_back(header);
		}

		return headers;
	}

} // namespace sgre::audio
