#include "sgre/platform/backup_crypto.hpp"

#include <CommonCrypto/CommonCryptor.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>

namespace sgre::platform {

	static constexpr std::size_t kMaximumBackupCipherBytes = 256 * 1024 * 1024;

	static std::vector<std::uint8_t> TransformBackupBytes(std::span<const std::uint8_t> input, std::string_view secure_file_id, CCOperation operation) {
		if (input.size() > kMaximumBackupCipherBytes || (operation == kCCDecrypt && (input.empty() || input.size() % kCCBlockSizeAES128 != 0))) {
			throw std::runtime_error("backup AES input exceeds its bounded block-aligned domain");
		}

		// Native553520 strncpy stops at NUL and ignores bytes beyond the actual32-byte imported key.
		secure_file_id = secure_file_id.substr(0, secure_file_id.find('\0'));
		std::array<std::uint8_t, kCCKeySizeAES256> key{};
		for (std::size_t index = 0; index < std::min(key.size(), secure_file_id.size()); ++index) {
			key[index] = static_cast<std::uint8_t>(static_cast<unsigned char>(secure_file_id[index]));
		}

		const std::array<std::uint8_t, kCCBlockSizeAES128> initialization_vector{};
		std::vector<std::uint8_t> output(input.size() + kCCBlockSizeAES128);
		std::size_t produced = 0;
		const auto options = operation == kCCDecrypt ? CCOptions{0} : CCOptions{kCCOptionPKCS7Padding};
		const auto status = CCCrypt(operation, kCCAlgorithmAES, options, key.data(), key.size(), initialization_vector.data(), input.data(), input.size(), output.data(), output.size(), &produced);
		if (status != kCCSuccess || produced > output.size()) {
			throw std::runtime_error("backup AES CBC/PKCS7 transformation failed with status " + std::to_string(status));
		}
		if (operation == kCCDecrypt) {
			// Enforce the native Final=true padding contract explicitly; do not accept an unchecked OS depadding result.
			if (produced != input.size()) {
				throw std::runtime_error("backup AES CBC decryption returned an inconsistent block count");
			}
			const auto padding = static_cast<std::size_t>(output[produced - 1]);
			if (padding == 0 || padding > kCCBlockSizeAES128 || padding > produced) {
				throw std::runtime_error("backup AES PKCS7 padding length is invalid");
			}
			for (std::size_t index = produced - padding; index < produced; ++index) {
				if (output[index] != padding) {
					throw std::runtime_error("backup AES PKCS7 padding bytes are invalid");
				}
			}
			produced -= padding;
		}
		output.resize(produced);

		return output;
	}

	std::vector<std::uint8_t> DecryptBackupBytes(std::span<const std::uint8_t> ciphertext, std::string_view secure_file_id) {
		return TransformBackupBytes(ciphertext, secure_file_id, kCCDecrypt);
	}

	std::vector<std::uint8_t> EncryptBackupBytes(std::span<const std::uint8_t> plaintext, std::string_view secure_file_id) {
		return TransformBackupBytes(plaintext, secure_file_id, kCCEncrypt);
	}

} // namespace sgre::platform
