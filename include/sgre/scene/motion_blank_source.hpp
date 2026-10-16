#pragma once

#include <cstdint>
#include <string_view>

namespace sgre::scene {
	struct MotionBlankSourceRectangle {
		// Native atoi stores signed dimensions; positive geometry extent validation belongs to its consumer.
		std::int32_t Width = 0;
		std::int32_t Height = 0;
		float OriginX = 0;
		float OriginY = 0;
		float Resolution = 1;
	};

	// Recover the actual blank descriptor from four canonical decimal fields, with no atlas or texture identity.
	[[nodiscard]] MotionBlankSourceRectangle ReadMotionBlankSourceRectangle(std::string_view icon);
} // namespace sgre::scene
