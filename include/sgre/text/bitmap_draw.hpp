#pragma once

#include "sgre/resource/font.hpp"
#include "sgre/scene/motion_draw.hpp"
#include "sgre/text/layout.hpp"

#include <array>
#include <cstdint>
#include <optional>

namespace sgre::text {

	struct BitmapGlyphCommand {
		// The prepared font owns its atlas sources and must outlive this borrowed draw command.
		const resource::BitmapFont* Font = nullptr;
		std::uint32_t TextureSourceIndex = 0;
		// Original bitmap batches emit TL, BL, TR, BR vertices and the explicit triangle list below.
		std::array<scene::MotionSpriteVertex, 4> Vertices{};
		std::array<std::uint32_t, 6> Indices = {0, 1, 2, 1, 3, 2};
	};

	// Preserve the native font queue's RGBA word; script-facing color conversion belongs to its owner.
	// Missing bitmap glyphs return no command, as in native 0x1404c1970. Shader/state selection is separate.
	[[nodiscard]] std::optional<BitmapGlyphCommand> PrepareBitmapGlyphCommand(const resource::BitmapFont& font, std::uint16_t code_unit, const TextCursor& draw_position, float scale_x, float scale_y, std::uint32_t color_rgba);

} // namespace sgre::text
