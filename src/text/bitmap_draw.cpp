#include "sgre/text/bitmap_draw.hpp"

#include <stdexcept>

namespace sgre::text {

	std::optional<BitmapGlyphCommand> PrepareBitmapGlyphCommand(const resource::BitmapFont& font, std::uint16_t code_unit, const TextCursor& draw_position, float scale_x, float scale_y, std::uint32_t color_rgba) {
		const auto* glyph = font.FindGlyphCodeUnit(code_unit);
		if (!glyph) {
			return std::nullopt;
		}

		if (glyph->SourceIndex >= font.Sources.size()) {
			throw std::runtime_error("bitmap glyph draw source exceeds its prepared font owner");
		}

		const auto quad = resource::EvaluateGlyphQuad(*glyph, font.Sources[glyph->SourceIndex], draw_position.X, draw_position.Y, scale_x, scale_y);
		BitmapGlyphCommand command;
		command.Font = &font;
		command.TextureSourceIndex = glyph->SourceIndex;
		const std::array<resource::MotionPoint, 4> positions{{{quad.Left, quad.Top}, {quad.Left, quad.Bottom}, {quad.Right, quad.Top}, {quad.Right, quad.Bottom}}};
		const std::array<resource::MotionPoint, 4> texture_coordinates{{{quad.U0, quad.V0}, {quad.U0, quad.V1}, {quad.U1, quad.V0}, {quad.U1, quad.V1}}};
		for (std::size_t index = 0; index < command.Vertices.size(); ++index) {
			command.Vertices[index].Position = positions[index];
			command.Vertices[index].Uv = texture_coordinates[index];
			command.Vertices[index].Color = color_rgba;
		}

		return command;
	}

} // namespace sgre::text
