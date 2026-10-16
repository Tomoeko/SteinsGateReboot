#pragma once

#include "sgre/resource/truetype_outline.hpp"
#include "sgre/text/cff_glyph_path.hpp"

#include <cstdint>
#include <span>

namespace sgre::text {

	// Points are already scaled to native 26.6 coordinates. The shared path owns
	// downward-positive pixels; loading, hinting and rasterization are separate.
	[[nodiscard]] CffGlyphPath PrepareTrueTypeGlyphPath(std::span<const resource::TrueTypePoint> scaled_points, std::span<const std::uint16_t> contour_ends);

} // namespace sgre::text
