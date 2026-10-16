#include "sgre/text/wrapping.hpp"

#include <array>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static void RequireFailure(const std::function<void()>& operation) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find("text wrapping") != std::string_view::npos, "wrapping failure lost its context");

		return;
	}

	throw std::runtime_error("invalid wrapping input was accepted");
}

struct NativeWordBoundaryFixture {
	bool KeepWords = true;
	bool PreviousSpace = false;
	std::uint32_t PreviousStart = 0;
	std::size_t AppendedCount = 0;
	std::array<char16_t, 13> Units{};
	std::size_t UnitCount = 0;
	std::uint32_t ExpectedStart = 0;
	bool ExpectedSpace = false;
};

static void TestOriginalWordBoundaryInstructions() {
	// Original 0x14002dcde..0x14002e2b0 paths preserve code units, including lone surrogates.
	const std::array<NativeWordBoundaryFixture, 6> fixtures = {{
	    {false, false, 49, 57, {0x20}, 1, 49, false},
	    {false, true, 2, 14, {0x20, 0xdc00}, 2, 2, true},
	    {true, true, 3, 8, {0xd800, 0x20, 0}, 3, 8, false},
	    {true, false, 34, 56, {0x20}, 1, 34, true},
	    {false, true, 0, 12, {0x3000, 0x41, 0x3042, 0x20, 0x3042, 0x3000}, 6, 0, true},
	    {true, true, 93, 99, {0x20, 0x3042, 0, 0xdc00, 0x20, 0xdc00, 0x20}, 7, 99, false},
	}};
	for (const auto& fixture : fixtures) {
		sgre::text::TextWrapState state;
		state.WordStartElement = fixture.PreviousStart;
		state.PreviousWasAsciiSpace = fixture.PreviousSpace;
		sgre::text::UpdateTextWordBoundary(state, fixture.KeepWords, std::u16string_view(fixture.Units.data(), fixture.UnitCount), fixture.AppendedCount);
		Require(state.WordStartElement == fixture.ExpectedStart && state.PreviousWasAsciiSpace == fixture.ExpectedSpace, "word-boundary transition differs from original instructions");
	}
}

static void TestWordBoundaries() {
	sgre::text::TextWrapState state;
	const std::array<std::u16string_view, 7> elements{u"A", u" ", u" ", u"B", u"C", u"\u3000", u"D"};
	for (std::size_t index = 0; index < elements.size(); ++index) {
		sgre::text::UpdateTextWordBoundary(state, true, elements[index], index + 1);
	}

	Require(state.WordStartElement == 4 && !state.PreviousWasAsciiSpace, "word start did not follow the last consecutive ASCII space");
	sgre::text::UpdateTextWordBoundary(state, true, u" ", 8);
	sgre::text::UpdateTextWordBoundary(state, true, u"", 9);
	Require(state.WordStartElement == 9 && !state.PreviousWasAsciiSpace, "empty native element did not end ASCII-space run");
	state.PreviousWasAsciiSpace = true;
	sgre::text::UpdateTextWordBoundary(state, false, u"X", 10);
	Require(state.WordStartElement == 9 && state.PreviousWasAsciiSpace, "disabled word grouping mutated retained state");
	sgre::text::UpdateTextWordBoundary(state, true, u"  ", 11);
	Require(state.WordStartElement == 11 && !state.PreviousWasAsciiSpace, "multi-unit whitespace was mistaken for a single ASCII-space element");
}

static void TestWordMove() {
	sgre::text::TextWrapSettings settings;
	settings.ProhibitedLineStart = u")";
	sgre::text::TextWrapState state;
	state.WordStartElement = 3;
	state.LeadingProhibitionOverhang = 1;
	const std::array<std::u16string_view, 5> elements{u"A", u" ", u"B", u"C", u"D"};
	const auto plan = sgre::text::PlanTextLineOverflow(settings, state, elements, u")");
	Require(plan.FinalizeLine && plan.RetainedElementCount == 2 && plan.ReplayElementCount == 3 && plan.LeadingProhibitionOverhang == 1, "whole-word move did not precede the incoming prohibition rule");
	state.WordStartElement = 7;
	const auto stale_plan = sgre::text::PlanTextLineOverflow(settings, state, elements, u")");
	Require(stale_plan.FinalizeLine && stale_plan.ReplayElementCount == 0, "out-of-line native word marker invented a replay tail");
}

static void TestLeadingProhibition() {
	sgre::text::TextWrapSettings settings;
	settings.KeepWordsTogether = false;
	settings.ProhibitedLineStart = u"),!";
	settings.MaximumLeadingProhibitionOverhang = 1;
	sgre::text::TextWrapState state;
	const std::array<std::u16string_view, 4> elements{u"A", u"B", u"C", u")"};
	const auto first = sgre::text::PlanTextLineOverflow(settings, state, elements, u",");
	Require(!first.FinalizeLine && first.ReplayElementCount == 0 && first.LeadingProhibitionOverhang == 1, "first prohibited-start element did not overhang the line");
	state.LeadingProhibitionOverhang = 1;
	const auto second = sgre::text::PlanTextLineOverflow(settings, state, elements, u"!");
	Require(second.FinalizeLine && second.RetainedElementCount == 2 && second.ReplayElementCount == 2 && second.LeadingProhibitionOverhang == 0, "exhausted overhang did not move prior overhang and preceding ordinary element");
	const std::array<std::u16string_view, 3> punctuation{u"A", u")", u","};
	const auto punctuation_plan = sgre::text::PlanTextLineOverflow(settings, state, punctuation, u"!");
	Require(punctuation_plan.RetainedElementCount == 2 && punctuation_plan.ReplayElementCount == 1, "prohibited-start predecessor was moved a second time");
	state.LeadingProhibitionOverhang = 7;
	const auto empty = sgre::text::PlanTextLineOverflow(settings, state, std::span<const std::u16string_view>{}, u"!");
	Require(empty.FinalizeLine && empty.ReplayElementCount == 0 && empty.LeadingProhibitionOverhang == 7, "empty line decremented an absent overhang element");
	state = {};
	const auto empty_element = sgre::text::PlanTextLineOverflow(settings, state, elements, u"");
	Require(!empty_element.FinalizeLine && empty_element.LeadingProhibitionOverhang == 1, "empty UTF-16 substring was absent from the prohibition list");
}

static void TestTrailingProhibitionAndExactMatching() {
	sgre::text::TextWrapSettings settings;
	settings.ProhibitedLineEnd = u"([{AB";
	sgre::text::TextWrapState state;
	const std::array<std::u16string_view, 4> elements{u"Q", u"R", u"S", u"("};
	const auto plan = sgre::text::PlanTextLineOverflow(settings, state, elements, u"Z");
	Require(plan.FinalizeLine && plan.RetainedElementCount == 3 && plan.ReplayElementCount == 1, "prohibited line-end element stayed on the prior line");
	const std::array<std::u16string_view, 2> consecutive{u"(", u"["};
	const auto consecutive_plan = sgre::text::PlanTextLineOverflow(settings, state, consecutive, u"Z");
	Require(consecutive_plan.RetainedElementCount == 1 && consecutive_plan.ReplayElementCount == 1, "native one-element trailing move was broadened to a punctuation run");
	const std::array<std::u16string_view, 1> substring{u"AB"};
	Require(sgre::text::PlanTextLineOverflow(settings, state, substring, u"Z").ReplayElementCount == 1, "prohibition matching was narrowed to individual code units");
	const std::array<std::u16string_view, 1> reversed{u"BA"};
	Require(sgre::text::PlanTextLineOverflow(settings, state, reversed, u"Z").ReplayElementCount == 0, "prohibition substring was treated as an unordered character set");
	const std::array<std::u16string_view, 1> different_case{u"ab"};
	Require(sgre::text::PlanTextLineOverflow(settings, state, different_case, u"Z").ReplayElementCount == 0, "prohibition names were case-normalized");
}

static void TestInputLimits() {
	sgre::text::TextWrapState state;
	sgre::text::TextWrapSettings settings;
	const std::array<std::u16string_view, 1> elements{u"A"};
	RequireFailure([&] {
		sgre::text::UpdateTextWordBoundary(state, true, u"A", 0);
	});
	RequireFailure([&] {
		sgre::text::UpdateTextWordBoundary(state, true, u"A", 1'000'001);
	});
	state.WordStartElement = 1'000'001;
	RequireFailure([&] {
		static_cast<void>(sgre::text::PlanTextLineOverflow(settings, state, elements, u"B"));
	});
	state = {};
	settings.MaximumLeadingProhibitionOverhang = std::numeric_limits<std::uint32_t>::max();
	RequireFailure([&] {
		static_cast<void>(sgre::text::PlanTextLineOverflow(settings, state, elements, u"B"));
	});
	settings = {};
	const std::u16string oversized(65'537, u'A');
	settings.ProhibitedLineStart = oversized;
	RequireFailure([&] {
		static_cast<void>(sgre::text::PlanTextLineOverflow(settings, state, elements, u"B"));
	});
	const std::u16string excessive_search(8'192, u'A');
	settings.ProhibitedLineStart = excessive_search;
	RequireFailure([&] {
		static_cast<void>(sgre::text::PlanTextLineOverflow(settings, state, elements, excessive_search));
	});
}

int main() {
	try {
		TestOriginalWordBoundaryInstructions();
		TestWordBoundaries();
		TestWordMove();
		TestLeadingProhibition();
		TestTrailingProhibitionAndExactMatching();
		TestInputLimits();
		std::cout << "Text wrapping tests passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
