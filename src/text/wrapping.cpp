#include "sgre/text/wrapping.hpp"

#include <limits>
#include <stdexcept>

namespace sgre::text {

	static constexpr std::size_t kMaximumElements = 1'000'000;
	static constexpr std::size_t kMaximumStringUnits = 65'536;
	static constexpr std::size_t kMaximumSearchComparisons = 16'777'216;

	static void ValidateWrapState(const TextWrapState& state) {
		if (state.WordStartElement > kMaximumElements || state.LeadingProhibitionOverhang > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) {
			throw std::runtime_error("text wrapping state exceeds recovery limits");
		}
	}

	static bool IsProhibited(std::u16string_view list, std::u16string_view element) {
		if (list.size() > kMaximumStringUnits || element.size() > kMaximumStringUnits || (!element.empty() && list.size() > kMaximumSearchComparisons / element.size())) {
			throw std::runtime_error("text wrapping prohibition search exceeds recovery limits");
		}

		// Original 0x140030ea0 is an exact UTF-16 substring search, including empty strings.
		return list.find(element) != std::u16string_view::npos;
	}

	void UpdateTextWordBoundary(TextWrapState& state, bool keep_words_together, std::u16string_view element, std::size_t appended_element_count) {
		ValidateWrapState(state);
		if (appended_element_count == 0 || appended_element_count > kMaximumElements || element.size() > kMaximumStringUnits) {
			throw std::runtime_error("text wrapping append exceeds recovery limits");
		}

		if (!keep_words_together) {
			return;
		}

		const bool is_ascii_space = element.size() == 1 && element.front() == u' ';
		if (state.PreviousWasAsciiSpace && !is_ascii_space) {
			state.WordStartElement = static_cast<std::uint32_t>(appended_element_count);
		}

		state.PreviousWasAsciiSpace = is_ascii_space;
	}

	static TextWrapPlan MakePlan(std::size_t element_count, std::size_t retained_count, std::uint32_t overhang) {
		TextWrapPlan plan;
		plan.RetainedElementCount = retained_count;
		plan.ReplayElementCount = element_count - retained_count;
		plan.LeadingProhibitionOverhang = overhang;

		return plan;
	}

	static TextWrapPlan PlanWordMove(const TextWrapState& state, std::size_t element_count) {
		const auto retained_count = state.WordStartElement <= element_count ? static_cast<std::size_t>(state.WordStartElement - 1) : element_count;

		return MakePlan(element_count, retained_count, state.LeadingProhibitionOverhang);
	}

	static TextWrapPlan PlanLeadingProhibition(const TextWrapSettings& settings, const TextWrapState& state, std::span<const std::u16string_view> current_line) {
		if (state.LeadingProhibitionOverhang < settings.MaximumLeadingProhibitionOverhang) {
			auto plan = MakePlan(current_line.size(), current_line.size(), state.LeadingProhibitionOverhang + 1);
			plan.FinalizeLine = false;

			return plan;
		}

		std::size_t retained_count = current_line.size();
		auto overhang = state.LeadingProhibitionOverhang;
		while (retained_count > 0 && overhang > 0) {
			--retained_count;
			--overhang;
		}

		if (retained_count > 0 && !IsProhibited(settings.ProhibitedLineStart, current_line[retained_count - 1])) {
			--retained_count;
		}

		return MakePlan(current_line.size(), retained_count, overhang);
	}

	static TextWrapPlan PlanTrailingProhibition(const TextWrapSettings& settings, const TextWrapState& state, std::span<const std::u16string_view> current_line) {
		std::size_t retained_count = current_line.size();
		if (retained_count > 2 && !IsProhibited(settings.ProhibitedLineEnd, current_line[retained_count - 2]) && IsProhibited(settings.ProhibitedLineEnd, current_line[retained_count - 1])) {
			--retained_count;
		}

		if (retained_count > 0 && IsProhibited(settings.ProhibitedLineEnd, current_line[retained_count - 1])) {
			--retained_count;
		}

		return MakePlan(current_line.size(), retained_count, state.LeadingProhibitionOverhang);
	}

	TextWrapPlan PlanTextLineOverflow(const TextWrapSettings& settings, const TextWrapState& state, std::span<const std::u16string_view> current_line, std::u16string_view incoming_element) {
		ValidateWrapState(state);
		if (current_line.size() > kMaximumElements || settings.MaximumLeadingProhibitionOverhang > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()) || incoming_element.size() > kMaximumStringUnits || settings.ProhibitedLineStart.size() > kMaximumStringUnits || settings.ProhibitedLineEnd.size() > kMaximumStringUnits) {
			throw std::runtime_error("text wrapping input exceeds recovery limits");
		}

		if (settings.KeepWordsTogether && state.WordStartElement > 0) {
			return PlanWordMove(state, current_line.size());
		}

		if (IsProhibited(settings.ProhibitedLineStart, incoming_element)) {
			return PlanLeadingProhibition(settings, state, current_line);
		}

		return PlanTrailingProhibition(settings, state, current_line);
	}

} // namespace sgre::text
