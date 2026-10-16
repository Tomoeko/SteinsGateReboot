#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace sgre::resource {

	class ResourceCipher {
	public:
		ResourceCipher(std::string_view filename, std::string_view seed, std::size_t period);
		// Offsets are relative to the encrypted entity, after any clear wrapper header.
		void Transform(std::span<std::uint8_t> bytes, std::uint64_t stream_offset = 0) const;

	private:
		std::vector<std::uint8_t> m_KeyStream;
	};

} // namespace sgre::resource
