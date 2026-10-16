#include "sgre/text/line_geometry.hpp"

#include <cmath>
#include <stdexcept>

namespace sgre::text {

	static void ValidateLineGeometry(const TextLineGeometrySettings& settings, const TextCursor& cursor, std::span<const resource::GlyphSize> metrics, std::span<const TextCursor> positions) {
		if (!std::isfinite(settings.RenderWidth) || !std::isfinite(settings.RenderHeight) || !std::isfinite(settings.LineSpacing) || !std::isfinite(settings.LineStart) || !std::isfinite(cursor.X) || !std::isfinite(cursor.Y)) {
			throw std::runtime_error("text line geometry settings or cursor are nonfinite");
		}
		if (metrics.size() != positions.size()) {
			throw std::runtime_error("text line geometry metrics and position counts differ");
		}

		for (const auto& position : positions) {
			if (!std::isfinite(position.X) || !std::isfinite(position.Y)) {
				throw std::runtime_error("text line geometry element position is nonfinite");
			}
		}
	}

	static TextCursor FinalizeElementPosition(const TextLineGeometrySettings& settings, const TextCursor& position, float cross_extent, float alignment_offset) {
		TextCursor finalized = position;
		if (settings.Direction == TextDirection::Horizontal) {
			finalized.X = alignment_offset + position.X;
			finalized.Y = cross_extent + position.Y;
		} else {
			if (settings.CenterVerticalElements) {
				finalized.X = position.X - cross_extent * 0.5f;
			}
			finalized.Y = alignment_offset + position.Y;
		}

		if (!std::isfinite(finalized.X) || !std::isfinite(finalized.Y)) {
			throw std::runtime_error("text line geometry position overflows finite coordinates");
		}

		return finalized;
	}

	TextLineGeometry FinalizeTextLineGeometry(const TextLineGeometrySettings& settings, const TextCursor& advanced_cursor, std::span<const resource::GlyphSize> metrics, std::span<TextCursor> positions) {
		ValidateLineGeometry(settings, advanced_cursor, metrics, positions);
		TextLineGeometry line;
		line.NextCursor = advanced_cursor;
		line.CrossExtent = TextLineCrossExtent(settings.Direction, metrics, settings.MinimumCrossExtent);
		const bool horizontal = settings.Direction == TextDirection::Horizontal;
		const float cross_end = horizontal ? line.CrossExtent + advanced_cursor.Y : advanced_cursor.X - line.CrossExtent;
		if (!std::isfinite(cross_end)) {
			throw std::runtime_error("text line geometry cross extent overflows finite coordinates");
		}

		line.CrossLimitExceeded = horizontal ? settings.RenderHeight > 0 && cross_end > settings.RenderHeight : settings.RenderWidth > 0 && cross_end < 0;
		if (line.CrossLimitExceeded && !settings.AllowCrossOverflow) {
			return line;
		}

		const auto alignment_offset = TextLineAlignmentOffset(settings.Alignment, horizontal ? settings.RenderWidth : settings.RenderHeight, horizontal ? advanced_cursor.X : advanced_cursor.Y);
		line.CrossAdvance = line.CrossExtent + settings.LineSpacing;
		if (horizontal) {
			line.NextCursor.X = settings.LineStart;
			line.NextCursor.Y = cross_end + settings.LineSpacing;
		} else {
			line.NextCursor.X = cross_end - settings.LineSpacing;
			line.NextCursor.Y = settings.LineStart;
		}

		if (!std::isfinite(line.CrossAdvance) || !std::isfinite(line.NextCursor.X) || !std::isfinite(line.NextCursor.Y)) {
			throw std::runtime_error("text line geometry advance overflows finite coordinates");
		}

		// Check all results before writing caller storage, so an invalid line cannot leave partial geometry.
		for (const auto& position : positions) {
			static_cast<void>(FinalizeElementPosition(settings, position, line.CrossExtent, alignment_offset));
		}

		line.LeadingCrossPosition = horizontal ? 0.0f : settings.RenderWidth;
		bool first = true;
		for (auto& position : positions) {
			position = FinalizeElementPosition(settings, position, line.CrossExtent, alignment_offset);
			if (horizontal) {
				if (first || position.Y < line.LeadingCrossPosition) {
					line.LeadingCrossPosition = position.Y;
				}
			} else {
				line.LeadingCrossPosition = position.X > line.LeadingCrossPosition ? position.X : line.LeadingCrossPosition;
			}
			first = false;
		}

		line.Accepted = true;

		return line;
	}

} // namespace sgre::text
