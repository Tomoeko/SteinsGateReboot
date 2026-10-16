#pragma once

#include "sgre/text/layout.hpp"

#include <span>

namespace sgre::text {

	struct TextLineGeometrySettings {
		TextDirection Direction = TextDirection::Horizontal;
		TextAlignment Alignment = TextAlignment::Start;
		float RenderWidth = 0;
		float RenderHeight = 0;
		float MinimumCrossExtent = 0;
		float LineSpacing = 0;
		float LineStart = 0;
		bool AllowCrossOverflow = false;
		bool CenterVerticalElements = false;
	};

	struct TextLineGeometry {
		bool Accepted = false;
		bool CrossLimitExceeded = false;
		float CrossExtent = 0;
		float CrossAdvance = 0;
		float LeadingCrossPosition = 0;
		TextCursor NextCursor;
	};

	// Positions borrow the owner's already measured/appended elements and are changed only on acceptance.
	// The owner retains/discards line entries, styles, ruby, and text separately from this numeric operation.
	[[nodiscard]] TextLineGeometry FinalizeTextLineGeometry(const TextLineGeometrySettings& settings, const TextCursor& advanced_cursor, std::span<const resource::GlyphSize> metrics, std::span<TextCursor> positions);

} // namespace sgre::text
