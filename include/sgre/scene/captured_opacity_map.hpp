#pragma once

#include "sgre/resource/motion.hpp"

#include <array>
#include <cstdint>

namespace sgre::scene {
	struct CapturedTaskState;
	struct CapturedOpacityMapState {
		resource::MotionPoint ImageRatio{1, 1};
		resource::MotionPoint Repeat{1, 1};
		std::uint32_t Width = 1;
		std::uint32_t AddressU = 0;
		std::uint32_t AddressV = 0;
	};
	struct CapturedOpacityMapVertex {
		resource::MotionPoint Position;
		std::uint32_t Color = 0xffffffff;
		resource::MotionPoint SourceUv;
		resource::MotionPoint MapUv;
	};
	struct CapturedOpacityMapDrawPacket {
		// Native format5/VS2 is XY, ARGB, UV0, UV1 with stride28 and this exact index16 strip.
		std::array<CapturedOpacityMapVertex, 4> Vertices{};
		std::array<std::uint16_t, 4> Indices{0, 1, 2, 3};
		float Phase = 0;
		float InverseVague = 255;
		std::uint32_t AddressU = 0;
		std::uint32_t AddressV = 0;
	};
	// Actual first-image-source dimensions are retained by4fb990; current opacity supplies PS2040 constants.
	[[nodiscard]] CapturedOpacityMapDrawPacket PrepareCapturedOpacityMapDrawPacket(const CapturedTaskState& actual_state, const CapturedOpacityMapState& actual_map);
} // namespace sgre::scene
