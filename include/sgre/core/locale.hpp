#pragma once

#include <cstdint>
#include <string_view>

namespace sgre::core {

	struct OriginalLocale {
		// Names borrow immutable original locale catalog storage.
		std::string_view Language;
		std::string_view Country;
	};

	[[nodiscard]] OriginalLocale ResolveOriginalWindowsLocale(std::uint32_t locale_identifier);
	// Host services supply standard language/country codes; unmatched pairs use the original ja/JP default.
	[[nodiscard]] OriginalLocale ResolveOriginalHostLocale(std::string_view language, std::string_view country);
	[[nodiscard]] std::uint32_t PackOriginalLocalePrefix(std::string_view code);

} // namespace sgre::core
