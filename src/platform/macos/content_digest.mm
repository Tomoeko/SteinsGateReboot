#include "sgre/platform/content_digest.hpp"

#include "sgre/io/file_reader.hpp"

#include <CommonCrypto/CommonDigest.h>

#include <array>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace sgre::platform {

	std::string ContentSha256(std::span<const std::uint8_t> bytes) {
		if (bytes.size() > std::numeric_limits<CC_LONG>::max()) {
			throw std::runtime_error("SHA-256 input exceeds the platform one-shot range");
		}
		std::array<std::uint8_t, CC_SHA256_DIGEST_LENGTH> digest{};
		if (CC_SHA256(bytes.data(), static_cast<CC_LONG>(bytes.size()), digest.data()) != digest.data()) {
			throw std::runtime_error("platform SHA-256 failed");
		}
		constexpr std::string_view digits = "0123456789abcdef";
		std::string result;
		result.reserve(64);
		for (const auto byte : digest) {
			result.push_back(digits[byte >> 4]);
			result.push_back(digits[byte & 15]);
		}

		return result;
	}

	std::string OriginalCheckpointResourceIdentity(const std::filesystem::path& installation) {
		constexpr std::array<std::string_view, 5> files{"sgre_steam.exe", "wind3d11data/script_info.psb.m", "wind3d11data/script_body.bin", "wind3d11data/scenario_info.psb.m", "wind3d11data/scenario_body.bin"};
		std::vector<std::uint8_t> identity;
		for (const auto name : files) {
			io::FileReader reader(installation / name);
			if (reader.Size() == 0 || reader.Size() > 64 * 1024 * 1024) {
				throw std::runtime_error("checkpoint identity input exceeds its verified executable/scenario/script extent");
			}
			const auto digest = ContentSha256(reader.Read(0, static_cast<std::size_t>(reader.Size())));
			for (const auto byte : name) {
				identity.push_back(static_cast<std::uint8_t>(static_cast<unsigned char>(byte)));
			}
			identity.push_back(0);
			identity.insert(identity.end(), digest.begin(), digest.end());
		}

		return ContentSha256(identity);
	}

} // namespace sgre::platform
