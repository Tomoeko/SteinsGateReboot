#pragma once

#include "sgre/resource/motion.hpp"

#include <array>
#include <cstdint>

namespace sgre::scene {
	struct FillRectangleDrawPacket {
		// Native4f5740 format0, nonindexed strip: top-left, bottom-left, top-right, bottom-right.
		std::array<resource::MotionPoint, 4> Vertices{};
		// Actual record AARRGGBB after native opacity and RGBA conversion; VS3/PS60 consumes this uniform.
		std::uint32_t Color = 0xffffffff;
	};
} // namespace sgre::scene
