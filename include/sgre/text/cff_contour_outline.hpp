#pragma once

#include "sgre/text/cff_contour_owner.hpp"
#include "sgre/text/cff_glyph_metrics.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace sgre::text {

	struct CffContourOutline {
		std::vector<CffScaledPoint> Points;
		std::vector<std::uint16_t> ContourEnds;
	};

	// Converts actual fixed device callbacks into native signed 26.6 point/tag/contour storage.
	// Finalizes the last contour; a move callback closes its predecessor and does not allocate a point.
	[[nodiscard]] CffContourOutline BuildCffContourOutline(std::span<const CffContourCallback> callbacks);

} // namespace sgre::text
