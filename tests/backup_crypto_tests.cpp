#include "sgre/platform/backup_crypto.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static std::vector<std::uint8_t> Unhex(std::string_view text) {
	static constexpr std::string_view kDigits = "0123456789abcdef";
	std::vector<std::uint8_t> bytes;
	Require(text.size() % 2 == 0, "invalid independent AES hex vector");
	for (std::size_t index = 0; index < text.size(); index += 2) {
		const auto high = kDigits.find(text[index]);
		const auto low = kDigits.find(text[index + 1]);
		Require(high < 16 && low < 16, "invalid independent AES hex digit");
		bytes.push_back(static_cast<std::uint8_t>((high << 4) | low));
	}

	return bytes;
}

static void TestIndependentVectors() {
	struct Vector {
		std::string_view Key;
		std::size_t Bytes;
		std::string_view Cipher;
	};
	// Generated independently with system OpenSSL AES256-CBC/zero IV/PKCS7, including C-string key preparation.
	const std::array<Vector, 8> vectors{{
	    {"short", 0, "2170a2e4fb4007f78c93df05174a562a"},
	    {"short", 1, "8b793eb3b275a5455aee543a980e0cdd"},
	    {"short", 15, "16902cd98d91816abfb9a6b061f72e46"},
	    {"short", 16, "45d0b959033ad351aeb63d1ad2a7b7e02a4226eebaebfa7c52e3f54f505957bd"},
	    {"short", 17, "45d0b959033ad351aeb63d1ad2a7b7e0ec4c229d5ceb1164061a5aae71ef0540"},
	    {"0123456789abcdef0123456789abcdef", 79, "ea2b469a4514e18661e82beda72f5fb1045efeca5b9124bf4c990e774183b804d3040be0e8bbf8169863ab350f57200135576e068833a9dcf63c6959ef283a815a27fb185199a89a08e26e1bae93bc38"},
	    {"0123456789abcdef0123456789abcdefignored", 31, "ea2b469a4514e18661e82beda72f5fb13dc6a8f0ca6f23415875314961d01b07"},
	    {std::string_view("abc\0ignored", 11), 32, "1464d313f1d21dcbfd4bad81df622179d4b05bdb13852f844001e3746f62d475deca442d6227644105699b90e1f2eece"},
	}};
	for (const auto& vector : vectors) {
		std::vector<std::uint8_t> plain(vector.Bytes);
		for (std::size_t index = 0; index < plain.size(); ++index) {
			plain[index] = static_cast<std::uint8_t>(index * 17 + 3);
		}
		const auto cipher = Unhex(vector.Cipher);
		Require(sgre::platform::EncryptBackupBytes(plain, vector.Key) == cipher, "native-key-prepared AES encryption differs from independent vector");
		Require(sgre::platform::DecryptBackupBytes(cipher, vector.Key) == plain, "AES decrypt/padding differs from independent vector");
	}
}

static void RequireRejection(std::string_view context, const std::function<void()>& operation) {
	try {
		operation();
	} catch (const std::runtime_error&) {
		return;
	}

	throw std::runtime_error("invalid backup AES input was accepted: " + std::string(context));
}

int main() {
	try {
		TestIndependentVectors();
		RequireRejection("empty", [] {
			static_cast<void>(sgre::platform::DecryptBackupBytes({}, "short"));
		});
		RequireRejection("alignment", [] {
			const std::array<std::uint8_t, 17> misaligned{};
			static_cast<void>(sgre::platform::DecryptBackupBytes(misaligned, "short"));
		});
		RequireRejection("padding", [] {
			const std::array<std::uint8_t, 16> invalid_padding{};
			static_cast<void>(sgre::platform::DecryptBackupBytes(invalid_padding, "short"));
		});
		std::cout << "macOS backup AES256-CBC/PKCS7 independent vectors and malformed-input checks passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
