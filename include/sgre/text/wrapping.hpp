#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace sgre::text {

	struct TextWrapState {
		// One-based element position recorded after an ASCII-space-to-word transition.
		std::uint32_t WordStartElement = 0;
		bool PreviousWasAsciiSpace = false;
		std::uint32_t LeadingProhibitionOverhang = 0;
	};

	struct TextWrapSettings {
		bool KeepWordsTogether = true;
		std::uint32_t MaximumLeadingProhibitionOverhang = 1;
		// Borrow the owner's prepared UTF-16 lists without Unicode normalization.
		std::u16string_view ProhibitedLineStart;
		std::u16string_view ProhibitedLineEnd;
	};

	struct TextWrapPlan {
		bool FinalizeLine = true;
		std::size_t RetainedElementCount = 0;
		std::size_t ReplayElementCount = 0;
		std::uint32_t LeadingProhibitionOverhang = 0;
	};

	// Call after a successful append; native word grouping recognizes only exactly one U+0020 unit.
	void UpdateTextWordBoundary(TextWrapState& state, bool keep_words_together, std::u16string_view element, std::size_t appended_element_count);
	// Call after the primary line-limit predicate fires. Finalization retains the old advanced cursor;
	// replay the selected tail in its existing order before the incoming element on the next line.
	[[nodiscard]] TextWrapPlan PlanTextLineOverflow(const TextWrapSettings& settings, const TextWrapState& state, std::span<const std::u16string_view> current_line, std::u16string_view incoming_element);

} // namespace sgre::text
