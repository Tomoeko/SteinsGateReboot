#include "sgre/text/block_geometry.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace sgre::text {

	static constexpr std::size_t kMaximumBlockEntries = 1024 * 1024;

	static void ValidateBounds(const TextBounds& bounds) {
		if (!std::isfinite(bounds.Left) || !std::isfinite(bounds.Top) || !std::isfinite(bounds.Right) || !std::isfinite(bounds.Bottom)) {
			throw std::runtime_error("text block bounds are nonfinite");
		}
	}

	static void IncludeGeometry(TextBounds& bounds, const TextGlyphGeometry& glyph) {
		const auto& position = glyph.Position;
		const auto right = glyph.Metrics.Width + position.X;
		const auto bottom = glyph.Metrics.Height + position.Y;
		if (!std::isfinite(position.X) || !std::isfinite(position.Y) || !std::isfinite(right) || !std::isfinite(bottom)) {
			throw std::runtime_error("text block glyph geometry overflows finite coordinates");
		}

		if (position.X < bounds.Left) {
			bounds.Left = position.X;
		}
		if (right > bounds.Right) {
			bounds.Right = right;
		}
		if (position.Y < bounds.Top) {
			bounds.Top = position.Y;
		}
		if (bottom > bounds.Bottom) {
			bounds.Bottom = bottom;
		}
	}

	static TextBounds ExpandLineBounds(const TextBlockLine& line) {
		auto bounds = line.Bounds;
		ValidateBounds(bounds);
		for (const auto& element : line.Elements) {
			IncludeGeometry(bounds, element.Geometry);
			for (const auto& ruby : element.Ruby) {
				IncludeGeometry(bounds, ruby);
			}
		}

		return bounds;
	}

	static void IncludeLineBounds(TextBounds& bounds, const TextBounds& line) {
		if (line.Top < bounds.Top) {
			bounds.Top = line.Top;
		}
		if (line.Bottom > bounds.Bottom) {
			bounds.Bottom = line.Bottom;
		}
		if (line.Left < bounds.Left) {
			bounds.Left = line.Left;
		}
		if (line.Right > bounds.Right) {
			bounds.Right = line.Right;
		}
	}

	static float BlockAlignmentOffset(const TextBlockGeometrySettings& settings, const TextBounds& bounds) {
		const auto horizontal = settings.Direction == TextDirection::Horizontal;
		const auto remaining = horizontal ? settings.RenderHeight - bounds.Bottom : settings.RenderWidth - bounds.Left;
		float offset = 0;
		if (settings.Alignment == TextAlignment::Center) {
			offset = remaining * (horizontal ? 0.5f : -0.5f);
		} else if (settings.Alignment == TextAlignment::End) {
			offset = remaining;
		}

		if (!std::isfinite(offset) || double(offset) < double(std::numeric_limits<std::int32_t>::min()) || double(offset) > double(std::numeric_limits<std::int32_t>::max())) {
			throw std::runtime_error("text block alignment exceeds its signed integer range");
		}

		auto integral_offset = static_cast<std::int32_t>(offset);
		if (!horizontal && settings.Alignment == TextAlignment::End) {
			if (integral_offset == std::numeric_limits<std::int32_t>::min()) {
				throw std::runtime_error("text block end alignment cannot negate its signed offset");
			}

			integral_offset = -integral_offset;
		}

		return static_cast<float>(integral_offset);
	}

	static TextCursor AlignedPosition(TextDirection direction, const TextCursor& position, float offset) {
		auto result = position;
		if (offset != 0) {
			if (direction == TextDirection::Horizontal) {
				result.Y = offset + position.Y;
			} else {
				result.X = offset + position.X;
			}
		}

		if (!std::isfinite(result.X) || !std::isfinite(result.Y)) {
			throw std::runtime_error("text block alignment overflows an element position");
		}

		return result;
	}

	TextBlockGeometry FinalizeTextBlockGeometry(const TextBlockGeometrySettings& settings, std::span<TextBlockLine> lines, const TextBounds& previous_bounds) {
		if ((settings.Direction != TextDirection::Horizontal && settings.Direction != TextDirection::Vertical) || !std::isfinite(settings.RenderWidth) || !std::isfinite(settings.RenderHeight)) {
			throw std::runtime_error("text block geometry settings are invalid");
		}

		if (lines.size() > kMaximumBlockEntries) {
			throw std::runtime_error("text block line count exceeds the preparation limit");
		}

		ValidateBounds(previous_bounds);
		TextBlockGeometry block;
		block.Bounds = previous_bounds;
		std::size_t entry_count = lines.size();
		bool first_line = true;
		for (const auto& line : lines) {
			if (line.Elements.size() > kMaximumBlockEntries - entry_count) {
				throw std::runtime_error("text block element count exceeds the preparation limit");
			}

			entry_count += line.Elements.size();
			for (const auto& element : line.Elements) {
				if (element.Ruby.size() > kMaximumBlockEntries - entry_count) {
					throw std::runtime_error("text block ruby count exceeds the preparation limit");
				}

				entry_count += element.Ruby.size();
			}

			const auto expanded = ExpandLineBounds(line);
			if (first_line) {
				block.Bounds = expanded;
			} else {
				IncludeLineBounds(block.Bounds, expanded);
			}

			first_line = false;
		}

		block.CrossAlignmentOffset = BlockAlignmentOffset(settings, block.Bounds);
		if (settings.Direction == TextDirection::Horizontal) {
			block.Bounds.Top = block.CrossAlignmentOffset + block.Bounds.Top;
			block.Bounds.Bottom = block.CrossAlignmentOffset + block.Bounds.Bottom;
		} else {
			block.Bounds.Left = block.CrossAlignmentOffset + block.Bounds.Left;
			block.Bounds.Right = block.CrossAlignmentOffset + block.Bounds.Right;
		}

		ValidateBounds(block.Bounds);
		for (const auto& line : lines) {
			for (const auto& element : line.Elements) {
				static_cast<void>(AlignedPosition(settings.Direction, element.Geometry.Position, block.CrossAlignmentOffset));
			}
		}

		for (auto& line : lines) {
			line.Bounds = ExpandLineBounds(line);
		}

		for (auto& line : lines) {
			for (auto& element : line.Elements) {
				element.Geometry.Position = AlignedPosition(settings.Direction, element.Geometry.Position, block.CrossAlignmentOffset);
			}
		}

		return block;
	}

} // namespace sgre::text
