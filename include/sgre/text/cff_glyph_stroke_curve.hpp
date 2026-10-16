#pragma once

#include "sgre/text/cff_glyph_path.hpp"

#include <array>
#include <vector>

namespace sgre::text {
	struct CffStrokeCurveSides {
		// Entry joins compare the original cubic tangent; EndNormal follows the last offset piece.
		CffGlyphPathPoint StartNormal;
		CffGlyphPathPoint EndNormal;
		std::vector<CffGlyphPathCommand> Left;
		std::vector<CffGlyphPathCommand> Right;
	};

	// Native42EF90 cubic reduction and paired quadratic offsets, after glyph translation.
	// The returned commands exclude the contour's initial move, joins and endpoint caps.
	[[nodiscard]] CffStrokeCurveSides PrepareCffGlyphStrokeCurve(const std::array<CffGlyphPathPoint, 4>& cubic, double width_pixels);
	// Native42F6F2/4300EE direct nonflat conic branch; no cubic elevation.
	[[nodiscard]] CffStrokeCurveSides PrepareCffGlyphStrokeQuadratic(const std::array<CffGlyphPathPoint, 3>& quadratic, double width_pixels);
} // namespace sgre::text
