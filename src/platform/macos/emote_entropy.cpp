#include "sgre/platform/emote_entropy.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

#include <Security/SecRandom.h>

namespace sgre::platform {

	std::uint32_t ReadEmoteEntropyWord() {
		std::array<std::uint8_t, 4> bytes{};
		const auto status = SecRandomCopyBytes(kSecRandomDefault, bytes.size(), bytes.data());
		if (status != errSecSuccess) {
			throw std::runtime_error("macOS Emote initialization entropy failed with Security status " + std::to_string(status));
		}
		std::uint32_t word = 0;
		for (std::size_t index = 0; index != bytes.size(); ++index) {
			word |= static_cast<std::uint32_t>(bytes[index]) << (index * 8);
		}
		return word;
	}

} // namespace sgre::platform
