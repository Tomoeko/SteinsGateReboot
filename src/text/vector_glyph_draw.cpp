#include "sgre/text/vector_glyph_draw.hpp"

#include <cmath>
#include <initializer_list>
#include <stdexcept>

namespace sgre::text {

	std::optional<VectorGlyphDrawRectangle> PrepareVectorGlyphDrawRectangle(const CachedVectorGlyphDraw& draw, bool atlas_available) {
		if (!atlas_available) {
			return {};
		}
		if (!std::isfinite(draw.Position.X) || !std::isfinite(draw.Position.Y)) {
			throw std::runtime_error("vector glyph rectangle position is not finite");
		}

		const auto& region = draw.Region;
		const auto width = static_cast<float>(region.WidthPixels);
		const auto height = static_cast<float>(region.HeightPixels);
		VectorGlyphDrawRectangle rectangle;
		rectangle.SourcePixels.Left = static_cast<float>(region.AtlasLeftPixels);
		rectangle.SourcePixels.Top = static_cast<float>(region.AtlasTopPixels);
		rectangle.SourcePixels.Right = width + rectangle.SourcePixels.Left;
		rectangle.SourcePixels.Bottom = height + rectangle.SourcePixels.Top;

		rectangle.DestinationPixels.Left = static_cast<float>(region.LeftPixel) + draw.Position.X;
		rectangle.DestinationPixels.Top = std::floor(draw.Position.Y - static_cast<float>(region.TopTranslationPixel));
		rectangle.DestinationPixels.Right = width + rectangle.DestinationPixels.Left;
		rectangle.DestinationPixels.Bottom = std::floor(height + rectangle.DestinationPixels.Top);
		rectangle.ColorRgba = draw.ColorRgba;

		return rectangle;
	}

	std::array<scene::MotionSpriteVertex, 4> PrepareVectorGlyphVertices(const VectorGlyphDrawRectangle& rectangle, std::uint32_t texture_width, std::uint32_t texture_height) {
		const auto& source = rectangle.SourcePixels;
		const auto& destination = rectangle.DestinationPixels;
		for (const auto value : {source.Left, source.Top, source.Right, source.Bottom, destination.Left, destination.Top}) {
			if (!std::isfinite(value)) {
				throw std::runtime_error("vector glyph primitive requires finite actual source/destination coordinates");
			}
		}
		if (texture_width == 0 || texture_height == 0 || source.Right <= source.Left || source.Bottom <= source.Top) {
			throw std::runtime_error("vector glyph primitive requires positive actual atlas dimensions and region");
		}

		const auto right = (source.Right - source.Left) + destination.Left;
		const auto top = destination.Top - 0.0f;
		const auto bottom = ((source.Bottom - source.Top) + -0.0f) + destination.Top;
		const auto u0 = source.Left / static_cast<float>(texture_width);
		const auto v0 = source.Top / static_cast<float>(texture_height);
		const auto u1 = source.Right / static_cast<float>(texture_width);
		const auto v1 = source.Bottom / static_cast<float>(texture_height);
		const auto rgba = rectangle.ColorRgba;
		const auto native_color = (rgba >> 24) | ((rgba >> 8) & 0x0000ff00u) | ((rgba << 8) & 0x00ff0000u) | (rgba << 24);
		const auto host_color = (native_color << 8) | (native_color >> 24);
		std::array<scene::MotionSpriteVertex, 4> vertices;
		vertices[0] = {{destination.Left, top}, {u0, v0}, host_color};
		vertices[1] = {{destination.Left, bottom}, {u0, v1}, host_color};
		vertices[2] = {{right, top}, {u1, v0}, host_color};
		vertices[3] = {{right, bottom}, {u1, v1}, host_color};

		return vertices;
	}

} // namespace sgre::text
