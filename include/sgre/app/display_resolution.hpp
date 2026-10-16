#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace sgre::app {
	struct DisplayResolution {
		std::string_view Name;
		std::string_view Dimensions;
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
	};

	// The last two entries are user-authorized additions to the original three choices.
	inline constexpr std::array<DisplayResolution, 5> kDisplayResolutions{{
	    {"576p", "1024x576", 1024, 576},
	    {"720p", "1280x720", 1280, 720},
	    {"1080p", "1920x1080", 1920, 1080},
	    {"1440p", "2560x1440", 2560, 1440},
	    {"4k", "3840x2160", 3840, 2160},
	}};
} // namespace sgre::app
