#include "sgre/text/bitmap_element_draw.hpp"

#include <cmath>
#include <stdexcept>

namespace sgre::text {

	static void RequireFiniteBitmapDraw(float value) {
		if (!std::isfinite(value)) {
			throw std::runtime_error("bitmap element drawing requires finite native positions, spacing and scale");
		}
	}

	static resource::GlyphSize BitmapElementMetrics(const resource::BitmapFont& font, const IndicatorTextElement& element) {
		const auto unit = element.CodeUnits.empty() ? std::uint16_t{0} : element.CodeUnits.front();

		return resource::MeasureBitmapGlyph(font.FindGlyphCodeUnit(unit), 1, 1);
	}

	void EmitBitmapElementGlyphs(const resource::BitmapFont& font, std::span<const IndicatorTextElement> elements, TextCursor origin, resource::GlyphSize spacing, std::uint32_t opacity, std::int32_t alignment, float line_extent, BitmapElementDrawScratch scratch, const std::function<void(const BitmapGlyphCommand&)>& output) {
		if (!output || elements.size() > 65536 || scratch.Positions.size() < elements.size() || scratch.LineWidths.size() <= elements.size()) {
			throw std::runtime_error("bitmap element drawing requires bounded distinct prepared position/line storage and a real glyph receiver");
		}
		RequireFiniteBitmapDraw(origin.X);
		RequireFiniteBitmapDraw(origin.Y);
		RequireFiniteBitmapDraw(spacing.Width);
		RequireFiniteBitmapDraw(spacing.Height);
		RequireFiniteBitmapDraw(line_extent);

		TextCursor cursor = origin;
		float line_height = 0;
		std::size_t line = 0;
		for (std::size_t index = 0; index < elements.size(); ++index) {
			const auto& element = elements[index];
			if (element.IsLineBreak()) {
				scratch.LineWidths[line++] = (cursor.X - spacing.Width) - origin.X;
				cursor.Y = (cursor.Y + spacing.Height) + line_height;
				cursor.X = origin.X;
				line_height = 0;
				RequireFiniteBitmapDraw(cursor.Y);
				continue;
			}

			scratch.Positions[index] = cursor;
			const auto metrics = BitmapElementMetrics(font, element);
			const auto width = element.Scale * metrics.Width;
			const auto height = element.Scale * metrics.Height;
			RequireFiniteBitmapDraw(width);
			RequireFiniteBitmapDraw(height);
			cursor.X = (cursor.X + width) + spacing.Width;
			line_height = height > line_height ? height : line_height;
			RequireFiniteBitmapDraw(cursor.X);
		}
		scratch.LineWidths[line] = (cursor.X - spacing.Width) - origin.X;

		line = 0;
		for (std::size_t index = 0; index < elements.size(); ++index) {
			const auto& element = elements[index];
			if (element.IsLineBreak()) {
				++line;
				continue;
			}

			float alignment_offset = 0;
			if (alignment == 1) {
				alignment_offset = (line_extent - scratch.LineWidths[line]) * 0.5f;
			} else if (alignment == 2) {
				alignment_offset = line_extent - scratch.LineWidths[line];
			}
			TextCursor position;
			position.X = alignment_offset + scratch.Positions[index].X;
			position.Y = scratch.Positions[index].Y + 0.0f;
			RequireFiniteBitmapDraw(position.X);
			RequireFiniteBitmapDraw(position.Y);
			auto color = element.Color;
			if (opacity != 255) {
				// Native534be7 performs unsigned32 multiplication/division, preserving the other stored color bytes.
				color = (color & 0xffffff00u) | (((color & 255u) * opacity) / 255u);
			}
			const auto unit = element.CodeUnits.empty() ? std::uint16_t{0} : element.CodeUnits.front();
			const auto glyph = PrepareBitmapGlyphCommand(font, unit, position, element.Scale, element.Scale, color);
			if (glyph) {
				output(*glyph);
			}
		}
	}

} // namespace sgre::text
