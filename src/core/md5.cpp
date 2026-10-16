#include "sgre/core/md5.hpp"

#include <bit>
#include <stdexcept>

namespace sgre::core {

	static std::uint32_t ReadLittleEndianWord(std::span<const std::uint8_t> bytes, std::size_t offset) {
		return static_cast<std::uint32_t>(bytes[offset]) | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) | (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) | (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
	}

	void Md5::RequireUpdating() const {
		if (m_Finalized) {
			throw std::runtime_error("MD5 input cannot be extended after finalization");
		}
	}

	void Md5::Update(std::span<const std::uint8_t> bytes) {
		RequireUpdating();
		m_ByteCount += static_cast<std::uint64_t>(bytes.size());
		for (const auto byte : bytes) {
			AppendByte(byte);
		}
	}

	void Md5::Update(std::string_view bytes) {
		RequireUpdating();
		m_ByteCount += static_cast<std::uint64_t>(bytes.size());
		for (const auto byte : bytes) {
			AppendByte(static_cast<std::uint8_t>(static_cast<unsigned char>(byte)));
		}
	}

	std::array<std::uint8_t, 16> Md5::Finalize() {
		if (!m_Finalized) {
			// RFC1321 retains the bit length modulo2^64 before appending the padding.
			const auto bit_count = m_ByteCount * 8;
			AppendByte(0x80);
			while (m_BufferedBytes != 56) {
				AppendByte(0);
			}
			for (std::size_t index = 0; index < 8; ++index) {
				AppendByte(static_cast<std::uint8_t>(bit_count >> (index * 8)));
			}
			m_Finalized = true;
		}

		std::array<std::uint8_t, 16> digest;
		for (std::size_t index = 0; index < digest.size(); ++index) {
			digest[index] = static_cast<std::uint8_t>(m_State[index / 4] >> ((index % 4) * 8));
		}

		return digest;
	}

	void Md5::AppendByte(std::uint8_t byte) {
		m_Block[m_BufferedBytes++] = byte;

		if (m_BufferedBytes == m_Block.size()) {
			ProcessBlock();
			m_BufferedBytes = 0;
		}
	}

	void Md5::ProcessBlock() {
		static constexpr std::array<std::uint32_t, 64> kRoundConstants{0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8, 0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665, 0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
		static constexpr std::array<std::uint8_t, 64> kRotationCounts{7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
		std::array<std::uint32_t, 16> words{};

		for (std::size_t index = 0; index < words.size(); ++index) {
			words[index] = ReadLittleEndianWord(m_Block, index * 4);
		}

		std::uint32_t a = m_State[0];
		std::uint32_t b = m_State[1];
		std::uint32_t c = m_State[2];
		std::uint32_t d = m_State[3];

		for (std::size_t round = 0; round < kRoundConstants.size(); ++round) {
			std::uint32_t mix = 0;
			std::size_t word_index = 0;

			if (round < 16) {
				mix = (b & c) | (~b & d);
				word_index = round;
			} else if (round < 32) {
				mix = (b & d) | (c & ~d);
				word_index = (5 * round + 1) % words.size();
			} else if (round < 48) {
				mix = b ^ c ^ d;
				word_index = (3 * round + 5) % words.size();
			} else {
				mix = c ^ (b | ~d);
				word_index = (7 * round) % words.size();
			}

			const std::uint32_t next_b = b + std::rotl(a + mix + kRoundConstants[round] + words[word_index], static_cast<int>(kRotationCounts[round]));
			a = d;
			d = c;
			c = b;
			b = next_b;
		}

		m_State[0] += a;
		m_State[1] += b;
		m_State[2] += c;
		m_State[3] += d;
	}

} // namespace sgre::core
