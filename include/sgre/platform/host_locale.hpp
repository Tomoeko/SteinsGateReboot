#pragma once

#include <string>

namespace sgre::platform {

	struct HostLocale {
		std::string Language;
		std::string Country;
	};

	[[nodiscard]] HostLocale ReadHostLocale();

} // namespace sgre::platform
