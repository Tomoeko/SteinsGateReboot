#pragma once

#include "sgre/scene/raw_texture_packet.hpp"

#include <array>
#include <cstdint>

namespace sgre::scene {

	struct MovieQuadDescription {
		resource::MotionPoint Position;
		resource::MotionPoint Extent;
		std::uint32_t SourceWidth = 0;
		std::uint32_t SourceHeight = 0;
		std::uint32_t TextureWidth = 0;
		std::uint32_t TextureHeight = 0;
		std::uint32_t Opacity = 255;
	};

	struct MovieAlphaVertex {
		resource::MotionPoint Position;
		std::uint32_t Color = 0xffffffff;
		resource::MotionPoint ColorUv;
		resource::MotionPoint AlphaUv;
	};

	struct MovieAlphaDrawPacket {
		std::array<MovieAlphaVertex, 4> Vertices{};
		std::array<std::uint16_t, 4> Indices{0, 1, 2, 3};
	};

	[[nodiscard]] RawTextureDrawPacket PrepareMovieDrawPacket(const MovieQuadDescription& description);
	[[nodiscard]] MovieAlphaDrawPacket PrepareMovieAlphaDrawPacket(const MovieQuadDescription& description);

} // namespace sgre::scene
