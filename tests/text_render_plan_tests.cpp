#include "sgre/text/render_plan.hpp"

#include "text_key_wait_native_vectors.hpp"
#include "text_reveal_query_native_vectors.hpp"

#include <array>
#include <bit>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static void RequireFailure(const std::function<void()>& operation, std::string_view context) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, "text render failure lost its context");

		return;
	}

	throw std::runtime_error("invalid text render plan was accepted");
}

static std::array<std::uint32_t, 4> BoundsWords(const sgre::text::TextBounds& bounds) {
	return {std::bit_cast<std::uint32_t>(bounds.Left), std::bit_cast<std::uint32_t>(bounds.Top), std::bit_cast<std::uint32_t>(bounds.Right), std::bit_cast<std::uint32_t>(bounds.Bottom)};
}

static sgre::text::TextRenderElement MakeElement(std::uint32_t id, const sgre::text::TextCursor& position, const sgre::resource::GlyphSize& metrics, std::u16string_view text, float time, std::int32_t link) {
	sgre::text::TextRenderElement result;
	result.ElementId = id;
	result.Geometry.Position = position;
	result.Geometry.Metrics = metrics;
	result.Text = text;
	result.RevealTime = time;
	result.LinkIndex = link;

	return result;
}

static void TestRetainedOriginalRenderPlan() {
	// Independently retained original list/link/threshold words for a synthetic history.
	const std::array<std::array<std::uint32_t, 5>, 3> input_words = {{{1127882988, 1130536774, 1115801509, 3241177481, 3229614080}, {1125995761, 1123613200, 3243219852, 1104556613, 3217031168}, {1132952640, 3267301763, 1114098134, 1117579870, 1087373312}}};
	const std::array<std::int32_t, 3> link_indices = {0, 3, 1};
	const std::array<std::u16string, 3> text = {std::u16string{static_cast<char16_t>(42360)}, std::u16string{static_cast<char16_t>(56187), static_cast<char16_t>(55740), static_cast<char16_t>(38167)}, std::u16string{static_cast<char16_t>(40972), static_cast<char16_t>(59917), static_cast<char16_t>(42310)}};
	std::array<sgre::text::TextRenderElement, 3> elements{};
	for (std::size_t index = 0; index < elements.size(); ++index) {
		const auto& words = input_words[index];
		elements[index] = MakeElement(static_cast<std::uint32_t>(index), {std::bit_cast<float>(words[0]), std::bit_cast<float>(words[1])}, {std::bit_cast<float>(words[2]), std::bit_cast<float>(words[3])}, text[index], std::bit_cast<float>(words[4]), link_indices[index]);
	}
	const std::array<sgre::text::TextRenderLine, 1> lines = {{{elements}}};
	const std::array<sgre::text::TextLinkState, 4> links{};
	const std::array<std::uint32_t, 5> boundary_words = {3234251113, 1118840789, 1117863297, 3237456419, 1105229839};
	std::array<sgre::text::TextRevealBoundary, 5> boundaries{};
	for (std::size_t index = 0; index < boundaries.size(); ++index) {
		boundaries[index].FirstElement = static_cast<std::uint32_t>(100 + index);
		boundaries[index].Time = std::bit_cast<float>(boundary_words[index]);
	}

	const auto plan = sgre::text::PrepareTextRenderPlan(lines, links, boundaries, 2);
	Require(plan.RenderOrder == std::vector<std::uint32_t>{0, 1, 2}, "native render pointer order changed");
	const std::array<std::int32_t, 4> expected_runs = {1, 3, -1, 2};
	for (std::size_t index = 0; index < links.size(); ++index) {
		Require(plan.Links[index].LastRun == expected_runs[index], "native link run identity changed");
	}
	Require(plan.Links[0].Elements == std::vector<std::uint32_t>{0} && plan.Links[1].Elements == std::vector<std::uint32_t>{2} && plan.Links[3].Elements == std::vector<std::uint32_t>{1}, "native per-link traversal order changed");
	Require(plan.Links[0].Text == text[0] && plan.Links[1].Text == text[2] && plan.Links[3].Text == text[1] && plan.Links[2].Text.empty(), "native UTF-16 link text changed");
	Require(BoundsWords(plan.Links[0].RunBounds[0]) == std::array<std::uint32_t, 4>{1127882988, 1130536774, 1132135614, 1129814061}, "first native link rectangle changed");
	Require(BoundsWords(plan.Links[1].RunBounds[0]) == std::array<std::uint32_t, 4>{1132952640, 3267301763, 1134851451, 3246955668}, "second native link rectangle changed");
	Require(BoundsWords(plan.Links[3].RunBounds[0]) == std::array<std::uint32_t, 4>{1125995761, 1123613200, 1125145400, 1125598033}, "third native link rectangle changed");
	const std::array<std::uint32_t, 5> expected_boundaries = {0, 3, 3, 3, 104};
	for (std::size_t index = 0; index < boundaries.size(); ++index) {
		Require(plan.RevealBoundaries[index].FirstElement == expected_boundaries[index] && std::bit_cast<std::uint32_t>(plan.RevealBoundaries[index].Time) == boundary_words[index], "native reveal boundary count or retained time changed");
	}
}

static void TestLinkRunsAndRetainedState() {
	const std::array<char16_t, 3> embedded_null = {u'B', 0, u'C'};
	const std::array<sgre::text::TextRenderElement, 4> first = {{MakeElement(10, {1, 2}, {4, 5}, u"A", 3, 0), MakeElement(11, {5, 3}, {4, 5}, std::u16string_view(embedded_null.data(), embedded_null.size()), 2, 0), MakeElement(12, {20, 1}, {2, 3}, u"!", 1, -1), MakeElement(13, {30, 4}, {3, 5}, u"D", 2, 0)}};
	const std::array<sgre::text::TextRenderElement, 1> last = {{MakeElement(14, {1, 20}, {4, 5}, u"Z", 2, 0)}};
	const std::array<sgre::text::TextRenderLine, 3> lines = {{{first}, {}, {last}}};
	std::array<sgre::text::TextLinkState, 1> links{};
	links[0].Elements = {99};
	links[0].Text = u"X";
	links[0].RunBounds = {{-10, -10, -5, -5}};
	links[0].LastRun = 1;
	const std::array<sgre::text::TextRevealBoundary, 3> boundaries = {{{77, 2.9f}, {88, 3}, {999, 100}}};
	const auto plan = sgre::text::PrepareTextRenderPlan(lines, links, boundaries, 2);
	Require(plan.RenderOrder == std::vector<std::uint32_t>{12, 11, 13, 14, 10}, "equal reveal times did not retain history order");
	Require(plan.Links[0].Elements == std::vector<std::uint32_t>{99, 10, 11, 13, 14}, "link ownership was replaced or reordered by reveal sorting");
	const std::u16string expected_text{u'X', u'A', u'B', 0, u'C', u'D', u'Z'};
	Require(plan.Links[0].Text == expected_text, "explicit-length link text lost retained units or embedded NUL");
	Require(plan.Links[0].RunBounds.size() == 3 && plan.Links[0].LastRun == 5, "link runs did not split at unlinked elements and empty lines");
	const auto& first_bounds = plan.Links[0].RunBounds[0];
	Require(first_bounds.Left == -10 && first_bounds.Top == -10 && first_bounds.Right == 9 && first_bounds.Bottom == 8, "retained matching run did not expand its last rectangle");
	Require(plan.Links[0].RunBounds[1].Left == 30 && plan.Links[0].RunBounds[2].Top == 20, "new link run rectangle lost source geometry");
	Require(plan.RevealBoundaries[0].FirstElement == 1 && plan.RevealBoundaries[1].FirstElement == 4 && plan.RevealBoundaries[2].FirstElement == 999, "reveal boundary truncation/equality/terminal handling changed");
	Require(links[0].Text == u"X" && links[0].Elements == std::vector<std::uint32_t>{99} && links[0].RunBounds[0].Right == -5, "preparing a render plan modified its supplied retained state");
}

static void TestUnsortedAndEmptyHistory() {
	const std::array<sgre::text::TextRenderElement, 2> elements = {{MakeElement(5, {1, 2}, {-3, -4}, u"", 10, -2), MakeElement(6, {3, 4}, {5, 6}, u"", -10, -1)}};
	const std::array<sgre::text::TextRenderLine, 1> lines = {{{elements}}};
	const std::array<sgre::text::TextRevealBoundary, 2> boundaries = {{{77, 2.9f}, {99, 3}}};
	const auto plan = sgre::text::PrepareTextRenderPlan(lines, {}, boundaries, 1);
	Require(plan.RenderOrder == std::vector<std::uint32_t>{5, 6} && plan.RevealBoundaries[0].FirstElement == 77 && plan.RevealBoundaries[1].FirstElement == 99, "single-segment render preparation changed order or boundaries");
	const auto empty = sgre::text::PrepareTextRenderPlan({}, {}, boundaries, 2);
	Require(empty.RenderOrder.empty() && empty.RevealBoundaries[0].FirstElement == 0 && empty.RevealBoundaries[1].FirstElement == 99, "empty sorted history changed terminal boundary handling");
}

static void TestInvalidPreparation() {
	std::array<sgre::text::TextRenderElement, 1> elements = {{MakeElement(1, {1, 2}, {3, 4}, u"A", 1, 0)}};
	const std::array<sgre::text::TextRenderLine, 1> lines = {{{elements}}};
	RequireFailure([&] {
		static_cast<void>(sgre::text::PrepareTextRenderPlan(lines, {}, {}, 1));
	},
	    "link index");
	elements[0].LinkIndex = -1;
	elements[0].RevealTime = std::numeric_limits<float>::quiet_NaN();
	RequireFailure([&] {
		static_cast<void>(sgre::text::PrepareTextRenderPlan(lines, {}, {}, 1));
	},
	    "reveal time");
	elements[0].RevealTime = 1;
	elements[0].Geometry.Position.X = std::numeric_limits<float>::infinity();
	RequireFailure([&] {
		static_cast<void>(sgre::text::PrepareTextRenderPlan(lines, {}, {}, 1));
	},
	    "bounds");
	const std::array<sgre::text::TextRevealBoundary, 2> boundaries = {{{0, std::numeric_limits<float>::max()}, {0, 0}}};
	RequireFailure([&] {
		static_cast<void>(sgre::text::PrepareTextRenderPlan({}, {}, boundaries, 2));
	},
	    "signed integer");
	const std::vector<sgre::text::TextRenderLine> oversized(1024 * 1024 + 1);
	RequireFailure([&] {
		static_cast<void>(sgre::text::PrepareTextRenderPlan(oversized, {}, {}, 1));
	},
	    "lines");
}

static void TestOriginalKeyWaitBoundaryProjection() {
	for (const auto& sample : text_key_wait_test::kOriginalCases) {
		std::vector<sgre::text::TextRevealBoundary> input;
		for (std::size_t index = 0; index < sample.Count; ++index) {
			input.push_back({sample.Input[index][0], std::bit_cast<float>(sample.Input[index][1])});
		}
		const auto original = input;
		auto result = sgre::text::PrepareTextKeyWaits(input, std::bit_cast<float>(sample.TimeScale));
		Require(result.size() == (sample.Count == 0 ? 0 : sample.Count - 1), "native key-wait getter must exclude exactly its final terminal boundary");
		for (std::size_t index = 0; index < result.size(); ++index) {
			Require(result[index].FirstElement == sample.Expected[index][0] && std::bit_cast<std::uint32_t>(result[index].Time) == sample.Expected[index][1], "native key-wait position/time-scale word differs");
		}
		if (!result.empty()) {
			result.front().FirstElement ^= 1;
			result.front().Time = 123;
		}
		for (std::size_t index = 0; index < input.size(); ++index) {
			Require(input[index].FirstElement == original[index].FirstElement && std::bit_cast<std::uint32_t>(input[index].Time) == std::bit_cast<std::uint32_t>(original[index].Time), "key-wait results must own independent copied records");
		}
	}

	const std::vector<sgre::text::TextRevealBoundary> oversized(1024 * 1024 + 1);
	RequireFailure([&] {
		static_cast<void>(sgre::text::PrepareTextKeyWaits(oversized, 1));
	},
	    "key-wait boundaries");
}

static void TestOriginalRevealQuery() {
	for (const auto& sample : text_reveal_query_test::kOriginalCases) {
		std::vector<float> times;
		for (std::size_t index = 0; index < sample.Count; ++index) {
			times.push_back(std::bit_cast<float>(sample.Times[index]));
		}
		Require(sgre::text::CalculateTextShownElementCount(times, sample.Elapsed, std::bit_cast<float>(sample.TimeScale)) == sample.Expected, "native reverse reveal query differs for retained order, conversion or unordered comparison");
	}

	const std::array<sgre::text::TextRenderElement, 2> elements = {{MakeElement(19, {0, 0}, {1, 1}, u"A", 8, -1), MakeElement(7, {1, 0}, {1, 1}, u"B", 3, -1)}};
	const std::array<sgre::text::TextRenderLine, 1> lines = {{{elements}}};
	const auto plan = sgre::text::PrepareTextRenderPlan(lines, {}, {}, 2);
	Require(plan.RenderOrder == std::vector<std::uint32_t>{7, 19} && plan.RevealTimes == std::vector<float>{3, 8}, "query times must follow actual prepared pointer order without sorting a separate table");
}

int main() {
	try {
		TestRetainedOriginalRenderPlan();
		TestLinkRunsAndRetainedState();
		TestUnsortedAndEmptyHistory();
		TestInvalidPreparation();
		TestOriginalKeyWaitBoundaryProjection();
		TestOriginalRevealQuery();
		std::cout << "Text render plan tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << "Text render plan test failure: " << error.what() << '\n';

		return 1;
	}
}
