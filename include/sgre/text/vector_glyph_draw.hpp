#pragma once

#include "sgre/scene/motion_draw.hpp"
#include "sgre/text/block_geometry.hpp"
#include "sgre/text/vector_glyph_cache.hpp"

#include <array>
#include <cstdint>
#include <optional>

namespace sgre::text {

	struct VectorGlyphDrawRectangle {
		// Both rectangles are pixel coordinates, before texture normalization and scene transforms.
		TextBounds SourcePixels;
		TextBounds DestinationPixels;
		std::uint32_t ColorRgba = 0;
	};

	// Cache lookup decides drawable extents. This preserves the actual final rectangle projection.
	// The caller's text owner retains the atlas texture for the full draw command lifetime.
	[[nodiscard]] std::optional<VectorGlyphDrawRectangle> PrepareVectorGlyphDrawRectangle(const CachedVectorGlyphDraw& draw, bool atlas_available);

	// Native086c50 format4 rectangle group, expressed for the shared host packer's ROR8 color convention.
	[[nodiscard]] std::array<scene::MotionSpriteVertex, 4> PrepareVectorGlyphVertices(const VectorGlyphDrawRectangle& rectangle, std::uint32_t texture_width, std::uint32_t texture_height);

} // namespace sgre::text
