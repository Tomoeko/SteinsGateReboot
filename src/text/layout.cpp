#include "sgre/text/layout.hpp"

#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace sgre::text {

	static constexpr std::size_t kMaximumLineElements = 1000000;

	static void ValidateDirection(TextDirection direction) {
		if (direction != TextDirection::Horizontal && direction != TextDirection::Vertical) {
			throw std::runtime_error("text layout direction is invalid");
		}
	}

	static void ValidateElement(const TextCursor& cursor, const resource::GlyphSize& metrics) {
		if (!std::isfinite(cursor.X) || !std::isfinite(cursor.Y) || !std::isfinite(metrics.Width) || !std::isfinite(metrics.Height)) {
			throw std::runtime_error("text layout cursor or measured element is nonfinite");
		}
	}

	static float NativeAlignmentInteger(double offset) {
		if (!std::isfinite(offset) || offset < -2147483648.0 || offset >= 2147483648.0) {
			throw std::runtime_error("text line alignment exceeds the native integer range");
		}

		return static_cast<float>(static_cast<std::int32_t>(offset));
	}

	TextAlignment DecodeTextAlignment(std::int32_t native_alignment) {
		if (native_alignment == 0) {
			return TextAlignment::Center;
		}
		if (native_alignment == 1) {
			return TextAlignment::End;
		}

		return TextAlignment::Start;
	}

	float TextLineAlignmentOffset(TextAlignment alignment, float line_extent, float advanced_cursor) {
		if (!std::isfinite(line_extent) || !std::isfinite(advanced_cursor)) {
			throw std::runtime_error("text line alignment input is nonfinite");
		}

		if (alignment == TextAlignment::Start) {
			return 0;
		}
		const auto difference = line_extent - advanced_cursor;
		if (alignment == TextAlignment::Center) {
			// Native line alignment subtracts in float, then halves in double before CVTTSD2SI.
			return NativeAlignmentInteger(static_cast<double>(difference) * 0.5);
		}
		if (alignment == TextAlignment::End) {
			return NativeAlignmentInteger(difference);
		}

		throw std::runtime_error("text line alignment mode is invalid");
	}

	bool TextElementReachesLineLimit(TextDirection direction, const TextCursor& cursor, const resource::GlyphSize& metrics, float line_extent, bool allow_overflow) {
		ValidateDirection(direction);
		ValidateElement(cursor, metrics);
		if (!std::isfinite(line_extent)) {
			throw std::runtime_error("text line extent is nonfinite");
		}

		if (line_extent <= 0 || allow_overflow) {
			return false;
		}

		const auto end = direction == TextDirection::Horizontal ? cursor.X + metrics.Width : cursor.Y + metrics.Height;
		if (!std::isfinite(end)) {
			throw std::runtime_error("text element extent overflows finite coordinates");
		}

		return end >= line_extent;
	}

	TextElementPlacement PlaceMeasuredTextElement(TextDirection direction, const TextCursor& cursor, const resource::GlyphSize& metrics, float pitch) {
		ValidateDirection(direction);
		ValidateElement(cursor, metrics);
		if (!std::isfinite(pitch)) {
			throw std::runtime_error("text element pitch is nonfinite");
		}

		TextElementPlacement placement;
		placement.Position = cursor;
		placement.NextCursor = cursor;
		if (direction == TextDirection::Horizontal) {
			placement.Position.Y = cursor.Y - metrics.Height;
			placement.NextCursor.X = metrics.Width + cursor.X + pitch;
		} else {
			placement.NextCursor.Y = cursor.Y + metrics.Height + pitch;
		}

		if (!std::isfinite(placement.Position.Y) || !std::isfinite(placement.NextCursor.X) || !std::isfinite(placement.NextCursor.Y)) {
			throw std::runtime_error("text element placement overflows finite coordinates");
		}

		return placement;
	}

	float TextLineCrossExtent(TextDirection direction, std::span<const resource::GlyphSize> elements, float minimum_extent) {
		ValidateDirection(direction);
		if (!std::isfinite(minimum_extent) || elements.size() > kMaximumLineElements) {
			throw std::runtime_error("text line minimum extent is nonfinite or element count exceeds its limit");
		}

		float extent = 0;
		for (const auto& element : elements) {
			ValidateElement({}, element);
			const auto cross = direction == TextDirection::Horizontal ? element.Height : element.Width;
			// MAXSS chooses its second operand for equal values, including signed zero.
			extent = cross > extent ? cross : extent;
		}

		return extent > minimum_extent ? extent : minimum_extent;
	}

} // namespace sgre::text
