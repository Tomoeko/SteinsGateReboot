#include "sgre/scene/movie_packet.hpp"

#include <cmath>
#include <stdexcept>

namespace sgre::scene {

	static void RequireMovieQuad(const MovieQuadDescription& description) {
		if (description.SourceWidth == 0 || description.SourceHeight == 0 || description.SourceWidth > description.TextureWidth || description.SourceHeight > description.TextureHeight || description.TextureWidth > 16384 || description.TextureHeight > 16384 || !std::isfinite(description.Position.X) || !std::isfinite(description.Position.Y) || !std::isfinite(description.Extent.X) || !std::isfinite(description.Extent.Y) || !std::isfinite(description.Position.X + description.Extent.X) || !std::isfinite(description.Position.Y + description.Extent.Y)) {
			throw std::runtime_error("movie quad requires actual bounded source/texture dimensions and finite native view coordinates");
		}
	}

	RawTextureDrawPacket PrepareMovieDrawPacket(const MovieQuadDescription& description) {
		RequireMovieQuad(description);
		const auto color = ((description.Opacity & 255u) << 24) | 0x00ffffffu;
		auto packet = PrepareRawTextureDrawPacket(description.Position, description.Extent, color);
		packet.TextureAttributes = 0;
		const auto maximum_u = static_cast<float>(description.SourceWidth) / static_cast<float>(description.TextureWidth);
		const auto minimum_v = 1.0f - static_cast<float>(description.SourceHeight) / static_cast<float>(description.TextureHeight);
		packet.Vertices[0].Uv = {0, 1};
		packet.Vertices[1].Uv = {0, minimum_v};
		packet.Vertices[2].Uv = {maximum_u, 1};
		packet.Vertices[3].Uv = {maximum_u, minimum_v};

		return packet;
	}

	MovieAlphaDrawPacket PrepareMovieAlphaDrawPacket(const MovieQuadDescription& description) {
		const auto ordinary = PrepareMovieDrawPacket(description);
		const auto maximum_u = ordinary.Vertices[2].Uv.X;
		const auto minimum_v = ordinary.Vertices[1].Uv.Y;
		// Original38fb02 halves the padding complement, not the visible interval midpoint.
		const auto middle_v = minimum_v * 0.5f;
		const std::array<resource::MotionPoint, 4> color_uv{{{0, 1}, {0, middle_v}, {maximum_u, 1}, {maximum_u, middle_v}}};
		const std::array<resource::MotionPoint, 4> alpha_uv{{{0, middle_v}, {0, minimum_v}, {maximum_u, middle_v}, {maximum_u, minimum_v}}};
		MovieAlphaDrawPacket packet;
		for (std::size_t index = 0; index < packet.Vertices.size(); ++index) {
			packet.Vertices[index] = {ordinary.Vertices[index].Position, ordinary.Color, color_uv[index], alpha_uv[index]};
		}

		return packet;
	}

} // namespace sgre::scene
