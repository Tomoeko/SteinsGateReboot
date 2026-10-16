#include "sgre/resource/mzs_shell.hpp"
#include "sgre/resource/resource_cipher.hpp"

#include <algorithm>
#include <stdexcept>

namespace sgre::resource {
	static std::uint32_t ReadLittleEndianWord(std::span<const std::uint8_t> bytes, std::size_t offset) {
		return static_cast<std::uint32_t>(bytes[offset]) | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) | (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) | (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
	}

	MzsPayload DecodeMzsShell(std::span<const std::uint8_t> bytes, std::string_view filename, std::string_view seed, std::size_t period, std::size_t max_decoded_size) {
		if (bytes.size() < 8 || bytes[0] != 'm' || bytes[1] != 'z' || bytes[2] != 's' || bytes[3] != 0) {
			throw std::runtime_error("Invalid MZS header");
		}

		if (bytes.size() < 12) {
			throw std::runtime_error("MZS payload is too short for a Zstandard frame");
		}

		if (period == 0) {
			throw std::runtime_error("MZS key-stream period must be positive");
		}

		const std::uint32_t decoded_size = ReadLittleEndianWord(bytes, 4);

		if (decoded_size > max_decoded_size) {
			throw std::runtime_error("MZS decoded size exceeds the configured limit");
		}

		const std::size_t payload_size = bytes.size() - 8;
		// A period beyond the payload never wraps, so generate only the required prefix.
		const ResourceCipher cipher(filename, seed, std::min(period, payload_size));
		MzsPayload payload;
		payload.DecodedSize = decoded_size;
		payload.CompressedBytes.assign(bytes.begin() + 8, bytes.end());
		cipher.Transform(payload.CompressedBytes);

		if (ReadLittleEndianWord(payload.CompressedBytes, 0) != 0xfd2fb528U) {
			throw std::runtime_error("MZS key material did not recover a Zstandard frame");
		}

		return payload;
	}
} // namespace sgre::resource
