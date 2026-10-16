#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace sgre::core {

	class Md5 {
	public:
		void Update(std::span<const std::uint8_t> bytes);
		void Update(std::string_view bytes);
		// Returns the little-endian16-byte file-format digest; repeated Finalize is observational.
		[[nodiscard]] std::array<std::uint8_t, 16> Finalize();

	private:
		void RequireUpdating() const;
		void AppendByte(std::uint8_t byte);
		void ProcessBlock();
		std::array<std::uint32_t, 4> m_State{0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
		std::array<std::uint8_t, 64> m_Block{};
		std::uint64_t m_ByteCount = 0;
		std::size_t m_BufferedBytes = 0;
		bool m_Finalized = false;
	};

} // namespace sgre::core
