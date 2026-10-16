#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace sgre::platform {

	// Native save AES256 uses first32 C-string bytes, zero padding, CBC/zero IV and PKCS7 finalization.
	[[nodiscard]] std::vector<std::uint8_t> DecryptBackupBytes(std::span<const std::uint8_t> ciphertext, std::string_view secure_file_id);
	[[nodiscard]] std::vector<std::uint8_t> EncryptBackupBytes(std::span<const std::uint8_t> plaintext, std::string_view secure_file_id);

} // namespace sgre::platform
