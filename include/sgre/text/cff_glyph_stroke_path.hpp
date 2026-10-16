#pragma once

#include "sgre/text/cff_glyph_path.hpp"

#include <span>
#include <vector>

namespace sgre::text {
	// Native glyph paths retain open flags, including paths ending at their start point.
	// Builds owned round4/cap2 contours after the caller's native bitmap translation.
	[[nodiscard]] std::vector<CffGlyphPathCommand> PrepareCffGlyphStroke(std::span<const CffGlyphPathCommand> translated_commands, double width_pixels);
} // namespace sgre::text
