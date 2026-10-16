#pragma once

#include "sgre/resource/motion.hpp"

#include <array>
#include <cstdint>

namespace sgre::scene {
	struct RawTextureVertex {
		resource::MotionPoint Position;
		resource::MotionPoint Uv;
	};

	struct RawTextureDrawPacket {
		// Native submission uses this four-vertex triangle strip with no index buffer.
		std::array<RawTextureVertex, 4> Vertices{};
		std::uint32_t Color = 0xffffffff;
		std::uint32_t TextureAttributes = 4;
	};

	// Color is the already converted ARGB word; texture lifetime and pipeline state remain caller-owned.
	[[nodiscard]] RawTextureDrawPacket PrepareRawTextureDrawPacket(resource::MotionPoint position, resource::MotionPoint size, std::uint32_t packed_argb);
} // namespace sgre::scene
