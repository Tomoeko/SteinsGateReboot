#include "sgre/text/content_owner.hpp"
#include "sgre/text/recursive_decomposition.hpp"
#include "sgre/text/vector_font.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view message) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(message) != std::string_view::npos, error.what());
		return;
	}
	throw std::runtime_error("text content fixture expected failure");
}

static std::vector<std::uint16_t> Units(std::u16string_view source) {
	return {source.begin(), source.end()};
}

struct MeasuredFixture {
	sgre::text::TextContentOwner Content;
	sgre::text::TextFormattingState Formatting;
	sgre::text::TextProcessingOptions Options;
	sgre::text::TextContentCallbacks Callbacks;
	std::vector<std::uint16_t> Prepared;
	std::vector<std::string> Events;
	sgre::text::OwnedTextGlyphRun LastRun;

	MeasuredFixture() {
		Formatting.Defaults.FontSize = 12;
		Formatting.Defaults.LineSize = 12;
		Formatting.Defaults.LineSpacing = 2;
		Formatting.Defaults.Shadow = false;
		Formatting.Defaults.WordBreak = false;
		Callbacks.Fonts.SelectNormal = [this] {
			Events.emplace_back("normal");
		};
		Callbacks.Fonts.MeasureNormal = [this](std::uint16_t unit) {
			Require(unit == 0x3000, "real line preparation first measures U+3000");
			Events.emplace_back("unit");
			return sgre::resource::GlyphSize{7, 12};
		};
		Callbacks.Fonts.SelectRuby = [this] {
			Events.emplace_back("ruby");
		};
		Callbacks.PrepareNormal = [this](std::uint16_t unit) {
			Prepared.push_back(unit);
			LastRun.Glyphs = {{std::uint32_t(unit) + 100, {1.25f, -2.5f}}};
			LastRun.Advance = {7, 0};
			return LastRun;
		};
		sgre::text::ResetTextFontDefaults(Formatting, Callbacks.Fonts);
		sgre::text::ResetTextGeometryDefaults(Formatting);
		Events.clear();
	}
};

static void TestNativeMeasuredAppendAndHistoryOwnership() {
	MeasuredFixture fixture;
	const auto input = Units(u"AB CD");
	Require(fixture.Content.Render(input, fixture.Formatting, fixture.Options, {22, 0}, {}, fixture.Callbacks), "native measured sequence accepts");
	Require(fixture.Events == std::vector<std::string>{"normal", "unit", "ruby"}, "actual normal/measure/ruby callback order");
	Require(fixture.Content.Lines().size() == 1 && fixture.Content.CurrentLine().size() == 2, "native22px wrap retains first line and real current tail");
	const auto& first = fixture.Content.Lines()[0].Elements;
	Require(first.size() == 3 && first[0].Text == u"A" && first[1].Text == u"B" && first[2].Text == u" ", "native word tail retained in order");
	Require(first[0].Geometry.Position.X == 0 && first[1].Geometry.Position.X == 7 && first[2].Geometry.Position.X == 14 && first[0].Geometry.Position.Y == 0, "native measured first-line geometry");
	Require(fixture.Content.CurrentLine()[0].Text == u"C" && fixture.Content.CurrentLine()[0].Geometry.Position.Y == 2, "native current glyph remains pre-finalization baseline");
	fixture.LastRun.Glyphs[0].GlyphIndex = 999;
	Require(first[0].Run.Glyphs[0].GlyphIndex == std::uint32_t(u'A') + 100 && first[0].Run.Glyphs[0].Position.Y == -2.5f, "history owns the transferred shaped glyph run independently");
	fixture.Content.Finalize(fixture.Formatting, fixture.Options, {22, 0});
	Require(fixture.Content.CurrentLine().empty() && fixture.Content.Lines().size() == 2 && fixture.Content.ProcessedText() == u"AB \nCD\n", "native deep line history and processed text");
	Require(fixture.Content.Lines()[1].Elements[0].Geometry.Position.Y == 14 && fixture.Content.Plan().RenderOrder == std::vector<std::uint32_t>{0, 1, 2, 3, 4}, "done publishes actual retained geometry and stable element identities");
	fixture.Content.Clear();
	Require(fixture.Content.Lines().empty() && fixture.Content.ProcessedText().empty() && fixture.Content.ElementCount() == 0, "real clear retires content and owned runs");
}

static void TestNativeCenterDirectiveAndProcessedPrefix() {
	MeasuredFixture fixture;
	Require(fixture.Content.Render(Units(u"%CAB"), fixture.Formatting, fixture.Options, {70, 0}, {}, fixture.Callbacks), "native center directive accepts real measured elements");
	fixture.Content.Finalize(fixture.Formatting, fixture.Options, {70, 0});
	Require(fixture.Formatting.Current.Alignment == 0 && fixture.Content.Lines()[0].Elements[0].Geometry.Position.X == 28, "native percent-C changes current horizontal alignment before finalization");
	Require(fixture.Content.ProcessedText() == u"\u3000\u3000\u3000\u3000AB\n" && fixture.Prepared == std::vector<std::uint16_t>{u'A', u'B'}, "native center prefix consists of U3000 units and the directive emits no glyph");
	fixture.Content.Clear();
	fixture.Formatting.Current.Alignment = -1;
	fixture.Options.IgnoreStyle = true;
	Require(fixture.Content.Render(Units(u"%CA"), fixture.Formatting, fixture.Options, {70, 0}, {}, fixture.Callbacks), "ignored native style directive still parses");
	Require(fixture.Formatting.Current.Alignment == -1, "ignore-style guards percent-C scalar store");
}

static void TestOverflowRetiresActualCurrentLine() {
	MeasuredFixture fixture;
	const auto input = Units(u"AB CD EF");
	Require(!fixture.Content.Render(input, fixture.Formatting, fixture.Options, {22, 12}, {}, fixture.Callbacks), "native cross-axis failure propagates");
	Require(fixture.Content.RenderedElementCount() == 7 && fixture.Content.ElementCount() == 3 && fixture.Content.Overflowed() && fixture.Content.CurrentLine().empty() && fixture.Content.Lines().size() == 1 && fixture.Content.ProcessedText() == u"AB \n", "rejected current line is retired while prior history survives");
}

static void TestPreflightAndCallbackReentry() {
	MeasuredFixture fixture;
	RequireFailure([&] {
		static_cast<void>(fixture.Content.Render(Units(u"$name;A"), fixture.Formatting, fixture.Options, {}, {}, fixture.Callbacks));
	},
	    "markup effect");
	Require(fixture.Events.empty() && fixture.Content.ElementCount() == 0, "unrecovered markup fails before callbacks/content writes");
	fixture.Callbacks.Fonts.SelectNormal = [&] {
		fixture.Content.Clear();
	};
	RequireFailure([&] {
		static_cast<void>(fixture.Content.Render(Units(u"A"), fixture.Formatting, fixture.Options, {}, {}, fixture.Callbacks));
	},
	    "cannot clear");
	fixture.Callbacks.Fonts.SelectNormal = [] {
	};
	Require(fixture.Content.Render(Units(u"A"), fixture.Formatting, fixture.Options, {}, {}, fixture.Callbacks), "failed callback releases operation borrow");
}

static void TestRepeatedEmptyAndRejectedRenderContinuation() {
	for (const auto input : std::array<std::u16string_view, 3>{u"", u"\n\n", u"A"}) {
		MeasuredFixture fixture;
		const auto size = input == u"A" ? sgre::text::TextCursor{1, 1} : sgre::text::TextCursor{};
		static_cast<void>(fixture.Content.Render(Units(input), fixture.Formatting, fixture.Options, size, {}, fixture.Callbacks));
		Require(fixture.Content.ElementCount() == 0, "prior empty/LF/rejected segment has no retained glyph elements");
		const auto prior_count = fixture.Content.RenderedElementCount();
		fixture.Events.clear();
		Require(fixture.Content.Render(Units(u"B"), fixture.Formatting, fixture.Options, {}, {}, fixture.Callbacks), "native repeated Render must continue after an empty, LF-only or rejected prior call");
		Require(fixture.Events == std::vector<std::string>{"normal", "unit", "ruby"} && fixture.Content.ElementCount() == 1 && fixture.Content.RenderedElementCount() == prior_count + 1, "repeated preparation must execute real font callbacks and retain the independent native attempted-element counter");
		fixture.Content.Clear();
		Require(fixture.Content.Render(Units(u"B"), fixture.Formatting, fixture.Options, {}, {}, fixture.Callbacks) && fixture.Content.RenderedElementCount() == 1, "real Clear resets both attempted-element history and repeated-call state");
	}
}

static void TestNativeEmptyAppendTimingAndKeep() {
	for (const auto keep : {false, true}) {
		MeasuredFixture fixture;
		sgre::text::TextContentRequest first;
		first.ElementDelay = 3;
		Require(fixture.Content.Render(Units(u"AB"), fixture.Formatting, fixture.Options, {}, first, fixture.Callbacks), "first retained segment must prepare real glyphs and reveal times");
		fixture.Content.Finalize(fixture.Formatting, fixture.Options, {});
		Require(fixture.Content.RevealBoundaries().size() == 1 && fixture.Content.RevealBoundaries()[0].FirstElement == 2 && fixture.Content.RevealBoundaries()[0].Time == 6, "native initial terminal boundary retains the actual rendered count and completed segment end");
		sgre::text::TextContentRequest empty;
		empty.ElementDelay = 5;
		empty.KeepPreviousTimes = keep;
		Require(fixture.Content.Render({}, fixture.Formatting, fixture.Options, {}, empty, fixture.Callbacks), "native empty append must preserve actual retained content");
		const auto expected_times = keep ? std::vector<float>{0, 3} : std::vector<float>{0, 0};
		Require(fixture.Content.Plan().RevealTimes == expected_times && fixture.Content.RenderDelay(1) == (keep ? 6 : 0) && fixture.Content.RenderedElementCount() == 2 && fixture.Content.ElementCount() == 2, "whole native empty-call vectors retain first/count and reset history/duration only for non-keep requests");
		Require(fixture.Content.RevealBoundaries().size() == 1 && fixture.Content.RevealBoundaries()[0].FirstElement == 2 && fixture.Content.RevealBoundaries()[0].Time == (keep ? 6 : 0), "keep must not append a new key-wait terminal; non-keep replaces it with actual current duration");
	}
}

static void TestAppendedRevealQueryReadinessAndSorting() {
	for (const auto keep : {false, true}) {
		MeasuredFixture fixture;
		sgre::text::TextContentRequest first;
		first.ElementDelay = 3;
		Require(fixture.Content.Render(Units(u"AB"), fixture.Formatting, fixture.Options, {}, first, fixture.Callbacks), "first append fixture must prepare the actual history");
		fixture.Content.Finalize(fixture.Formatting, fixture.Options, {});
		sgre::text::TextContentRequest next;
		next.ElementDelay = 5;
		next.KeepPreviousTimes = keep;
		Require(fixture.Content.Render(Units(u"CD"), fixture.Formatting, fixture.Options, {}, next, fixture.Callbacks), "second actual render must append its own glyphs rather than replacing prior text");
		Require(fixture.Content.RenderedElementCount() == 4 && fixture.Content.ElementCount() == 4 && fixture.Content.RenderDelay(1) == 10, "new relative timing must restart at zero and retime only the new physical range");
		Require(fixture.Content.Plan().RevealTimes == (keep ? std::vector<float>{0, 3} : std::vector<float>{0, 0, 0, 5}), "keep defers query-pointer reconstruction until done; non-keep rebuilds the actual whole table immediately");
		Require(fixture.Content.RevealBoundaries().size() == 1 && fixture.Content.RevealBoundaries()[0].FirstElement == (keep ? 2u : 4u) && fixture.Content.RevealBoundaries()[0].Time == (keep ? 6 : 10), "appending must retain or rebuild original terminal ownership according to the actual query flag");
		fixture.Content.Finalize(fixture.Formatting, fixture.Options, {});
		Require(fixture.Content.Plan().RenderOrder == (keep ? std::vector<std::uint32_t>{0, 2, 1, 3} : std::vector<std::uint32_t>{0, 1, 2, 3}) && fixture.Content.Plan().RevealTimes == (keep ? std::vector<float>{0, 0, 3, 5} : std::vector<float>{0, 0, 0, 5}), "second-call done must sort real retained elements by actual reveal time while preserving their identities");
	}

	MeasuredFixture current;
	sgre::text::TextContentRequest first;
	first.ElementDelay = 3;
	Require(current.Content.Render(Units(u"ABC"), current.Formatting, current.Options, {}, first, current.Callbacks), "unfinalized append fixture requires an actual current line");
	sgre::text::TextContentRequest next;
	next.ElementDelay = 5;
	Require(current.Content.Render(Units(u"D"), current.Formatting, current.Options, {}, next, current.Callbacks), "unfinalized second render must append to the actual current line");
	Require(current.Content.Plan().RevealTimes == std::vector<float>{0, 3, 6, 0} && current.Content.RenderDelay(1) == 5 && current.Content.CurrentLine().size() == 4, "non-keep resets completed history only; it must not flatten current-line reveal times or retime earlier elements");
	current.Content.Finalize(current.Formatting, current.Options, {});
	Require(current.Content.Plan().RenderOrder == std::vector<std::uint32_t>{0, 3, 1, 2}, "done must still order repeated non-keep current-line times using the native call counter");
}

static void TestSignedIndentWithoutGrouping() {
	for (const auto indent : std::array<std::int32_t, 6>{0, 1, 2, -1, std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max()}) {
		MeasuredFixture fixture;
		sgre::text::TextContentRequest request;
		request.Indent = indent;
		Require(fixture.Content.Render(Units(u"AB CD\nEF"), fixture.Formatting, fixture.Options, {22, 0}, request, fixture.Callbacks), "native nonmatching indentation must preserve real horizontal append, wrap and LF");
		Require(fixture.Events == std::vector<std::string>{"normal", "unit", "ruby"}, "nonmatching signed indentation adds no measurement or font callback");
		Require(fixture.Prepared == std::vector<std::uint16_t>{u'A', u'B', u' ', u'C', u'D', u'E', u'F'} && fixture.Content.RenderedElementCount() == 7, "nonmatching indentation must neither create nor replay an extra source unit");
		fixture.Content.Finalize(fixture.Formatting, fixture.Options, {22, 0});
		Require(fixture.Content.Lines().size() == 3 && fixture.Content.ProcessedText() == u"AB \nCD\nEF\n", "signed indentation leaves actual processed text and wrapped history unchanged without grouping");
		const auto& lines = fixture.Content.Lines();
		Require(lines[0].Elements[0].Geometry.Position.X == 0 && lines[0].Elements[0].Geometry.Position.Y == 0 && lines[1].Elements[0].Geometry.Position.X == 0 && lines[1].Elements[0].Geometry.Position.Y == 14 && lines[2].Elements[0].Geometry.Position.X == 0 && lines[2].Elements[0].Geometry.Position.Y == 28, "native indentation mode is not a numeric initial or following-line offset");
		Require(fixture.Content.ElementCount() == 7 && fixture.Content.Plan().RenderOrder == std::vector<std::uint32_t>{0, 1, 2, 3, 4, 5, 6}, "signed indentation retains all real owned glyphs and draw identities");
	}
	for (const auto indent : std::array<std::int32_t, 2>{1, -1}) {
		MeasuredFixture fixture;
		sgre::text::TextContentRequest request;
		request.Indent = indent;
		Require(fixture.Content.Render({}, fixture.Formatting, fixture.Options, {}, request, fixture.Callbacks), "native signed indentation accepts an empty real segment");
		fixture.Content.Finalize(fixture.Formatting, fixture.Options, {});
		Require(fixture.Prepared.empty() && fixture.Content.ElementCount() == 0 && fixture.Content.Lines().empty() && fixture.Content.ProcessedText().empty(), "empty indentation creates no fictional spaces or geometry");
	}
}

static void RequireQuoteLines(std::u16string_view input, std::int32_t indent, std::span<const float> first_positions) {
	MeasuredFixture fixture;
	sgre::text::TextContentRequest request;
	request.Indent = indent;
	fixture.Callbacks.Fonts.MeasureNormal = [](std::uint16_t) {
		return sgre::resource::GlyphSize{7, 12};
	};
	Require(fixture.Content.Render(Units(input), fixture.Formatting, fixture.Options, {240, 0}, request, fixture.Callbacks), "native quote state accepts its ordinary owned units");
	fixture.Content.Finalize(fixture.Formatting, fixture.Options, {240, 0});
	Require(fixture.Content.Lines().size() == first_positions.size(), "quote state retains exact explicit line count");
	for (std::size_t index = 0; index < first_positions.size(); ++index) {
		const auto& line = fixture.Content.Lines()[index].Elements;
		if (!line.empty()) {
			Require(line.front().Geometry.Position.X == first_positions[index], "quote indentation controls actual following-line start");
		}
		for (const auto& element : line) {
			Require(element.Text.size() == 1 && element.Run.Glyphs.size() == 1, "quote units remain independent owned elements, not a grouped/ruby placeholder");
		}
	}
	fixture.Content.Clear();
	Require(fixture.Content.Render(Units(u"A\nB"), fixture.Formatting, fixture.Options, {240, 0}, {}, fixture.Callbacks), "Clear resets retained indentation before the next real render");
	fixture.Content.Finalize(fixture.Formatting, fixture.Options, {240, 0});
	Require(fixture.Content.Lines()[1].Elements.front().Geometry.Position.X == 0, "new render after Clear has no retained quote margin");
}

static void TestNativeQuoteNestingAndLineStarts() {
	RequireQuoteLines(u"\u300cA\nB\u300dC\nD", 1, std::array<float, 3>{0, 7, 0});
	RequireQuoteLines(u"\u300cA\nB\u300dC\nD", -1, std::array<float, 3>{-7, 0, 0});
	RequireQuoteLines(u"\u300cA\nB\u300dC\nD", 0, std::array<float, 3>{0, 0, 0});
	RequireQuoteLines(u"\u300cA\u300eB\nC\u300fD\u300dE\nF", 1, std::array<float, 3>{0, 7, 0});
	RequireQuoteLines(u"\u300cA\u300fB\nC", 1, std::array<float, 2>{0, 7});
	RequireQuoteLines(u"\u300dA\u300cB\nC", 1, std::array<float, 2>{0, 0});
	RequireQuoteLines(u"A\u300cB\nC\u300dD", 1, std::array<float, 2>{0, 0});
	RequireQuoteLines(u"\n\u300cAB\nC\u300dD", -1, std::array<float, 3>{0, -7, 0});
	RequireQuoteLines(u"A\n\u300cB\nC\u300d", 1, std::array<float, 3>{0, 0, 7});
}

static void TestDistinctOpeningMeasurementAndCallbackOrder() {
	MeasuredFixture fixture;
	sgre::text::TextContentRequest request;
	request.Indent = -1;
	fixture.Options.Begin = {u'A'};
	fixture.Options.End = {u'Z'};
	fixture.Callbacks.Fonts.MeasureNormal = [&](std::uint16_t unit) {
		if (unit == 0x3000) {
			fixture.Events.emplace_back("unit");
			return sgre::resource::GlyphSize{7, 12};
		}
		Require(unit == u'A', "only the first ordinary opening performs the additional native measurement");
		fixture.Events.emplace_back("opening-measure");
		Require(fixture.Prepared.empty() && fixture.Content.Cursor().X == 0, "native opening measurement precedes run preparation and cursor subtraction");
		fixture.Options.Begin = {u'B'};
		fixture.Formatting.Current.Color = 0xff123456;
		return sgre::resource::GlyphSize{3, 12};
	};
	const auto prepare = fixture.Callbacks.PrepareNormal;
	fixture.Callbacks.PrepareNormal = [&](std::uint16_t unit) {
		fixture.Events.emplace_back("prepare");
		if (unit == u'A') {
			Require(fixture.Content.Cursor().X == -3, "native extra measurement, rather than shaped advance, moves the initial cursor");
		}
		return prepare(unit);
	};
	Require(fixture.Content.Render(Units(u"A\nZ\nC"), fixture.Formatting, fixture.Options, {240, 0}, request, fixture.Callbacks), "callback-mutated live punctuation tables retain native lookup order");
	Require(fixture.Events == std::vector<std::string>{"normal", "unit", "ruby", "opening-measure", "prepare", "prepare", "prepare"}, "normal/basewidth/ruby/extra-measure/owned-run callback order");
	fixture.Content.Finalize(fixture.Formatting, fixture.Options, {240, 0});
	const auto lines = fixture.Content.Lines();
	Require(lines[0].Elements.front().Geometry.Position.X == -3 && lines[1].Elements.front().Geometry.Position.X == 4 && lines[2].Elements.front().Geometry.Position.X == 0, "post-append cursor saves the margin and the current Begin table supplies the saved opener");
	Require(lines[0].Elements.front().Style.MainColor == 0xff123456, "element style is captured after the real opening measurement callback");

	MeasuredFixture reentry;
	reentry.Callbacks.Fonts.MeasureNormal = [&](std::uint16_t unit) {
		if (unit == 0x300c) {
			reentry.Content.Clear();
		}
		return sgre::resource::GlyphSize{7, 12};
	};
	RequireFailure([&] {
		static_cast<void>(reentry.Content.Render(Units(u"\u300cA"), reentry.Formatting, reentry.Options, {240, 0}, request, reentry.Callbacks));
	},
	    "cannot clear");
	Require(reentry.Prepared.empty() && reentry.Content.ElementCount() == 0 && reentry.Content.Cursor().X == 0, "reentrant opening measurement cannot destroy its actual content owner or fabricate a shaped run");
	reentry.Callbacks.Fonts.MeasureNormal = [](std::uint16_t) {
		return sgre::resource::GlyphSize{7, 12};
	};
	Require(reentry.Content.Render(Units(u"\u300cA"), reentry.Formatting, reentry.Options, {240, 0}, request, reentry.Callbacks), "throwing opening callback releases the operation borrow for a real retry");
}

static void TestEscapesAndRejectedQuoteAppends() {
	RequireQuoteLines(u"\\\u300cA\nB\u300dC", 1, std::array<float, 2>{0, 0});
	RequireQuoteLines(u"\\\u300cA\nB\u300dC", -1, std::array<float, 2>{0, 0});
	RequireQuoteLines(u"\u300cA\\\u300dB\nC", 1, std::array<float, 2>{0, 7});

	MeasuredFixture opening;
	sgre::text::TextContentRequest request;
	request.Indent = 1;
	Require(!opening.Content.Render(Units(u"\u300cA"), opening.Formatting, opening.Options, {1, 1}, request, opening.Callbacks), "failed opening append retains the actual cross-axis rejection");
	Require(opening.Prepared == std::vector<std::uint16_t>{0x300c} && opening.Content.RenderedElementCount() == 1 && opening.Content.ElementCount() == 0 && opening.Content.CurrentLine().empty(), "failed opening owns no phantom glyph while native rendered-attempt count survives");

	MeasuredFixture closing;
	Require(!closing.Content.Render(Units(u"\u300c\u300eA\nBCD\u300f\u300d"), closing.Formatting, closing.Options, {29, 12}, request, closing.Callbacks), "outer matching closer itself can fail the real wrap/line append");
	Require(closing.Prepared.back() == 0x300d && closing.Content.RenderedElementCount() == 8 && closing.Content.ElementCount() == 3 && closing.Content.Lines().size() == 1 && closing.Content.CurrentLine().empty(), "rejected closer retires the current line and leaves only completed earlier content");
	closing.Content.Clear();
	Require(closing.Content.Render(Units(u"A\nB"), closing.Formatting, closing.Options, {240, 0}, {}, closing.Callbacks), "Clear after rejection resets native retained quote state");
	closing.Content.Finalize(closing.Formatting, closing.Options, {240, 0});
	Require(closing.Content.Lines()[1].Elements.front().Geometry.Position.X == 0, "rejected quote state cannot leak through actual Clear");
}

static void TestRemainingContentDomainsStayExplicit() {
	for (const auto input : std::array<std::u16string_view, 1>{u"\\rA"}) {
		MeasuredFixture fixture;
		RequireFailure([&] {
			static_cast<void>(fixture.Content.Render(Units(input), fixture.Formatting, fixture.Options, {}, {}, fixture.Callbacks));
		},
		    "markup effect");
		Require(fixture.Events.empty() && fixture.Prepared.empty(), "unrecovered escape controls remain rejected before real font callbacks");
	}
	MeasuredFixture missing_ruby;
	RequireFailure([&] {
		static_cast<void>(missing_ruby.Content.Render(Units(u"[annotation]A"), missing_ruby.Formatting, missing_ruby.Options, {}, {}, missing_ruby.Callbacks));
	},
	    "actual retained ruby provider");
	Require(missing_ruby.Events.empty() && missing_ruby.Prepared.empty(), "annotation markup must not replace a missing actual ruby producer");
	MeasuredFixture vertical;
	vertical.Formatting.Vertical = true;
	RequireFailure([&] {
		static_cast<void>(vertical.Content.Render(Units(u"\u300cA"), vertical.Formatting, vertical.Options, {}, {}, vertical.Callbacks));
	},
	    "vertical");
	Require(vertical.Events.empty() && vertical.Prepared.empty(), "quote recovery does not admit vertical text");
}

static void ConfigureNativeMarkupFormatting(sgre::text::TextFormattingState& formatting) {
	formatting.Defaults.Color = 0x11223344;
	formatting.Current.Color = 0x8899aabb;
	formatting.Defaults.ShadowColor = 0x12345678;
	formatting.Current.ShadowColor = 0x99abcdef;
	formatting.Defaults.EdgeColor = 0x55667788;
	formatting.Current.EdgeColor = 0xa1b2c3d4;
	formatting.Defaults.Shadow = true;
	formatting.Current.Shadow = true;
	formatting.Defaults.Edge = false;
	formatting.Current.Edge = false;
	formatting.Current.ShadowOffset = 0;
	formatting.Current.EdgeSize = 0;
}

static void TestNativeColorConversionAndRestoration() {
	// Original575BE0 signed32 base16 results, followed by the parser's forced alpha.
	const std::array<std::pair<std::u16string_view, std::uint32_t>, 26> cases{{
	    {u"112233", 0xff112233},
	    {u"80112233", 0xffffffff},
	    {u"ffffffff", 0xffffffff},
	    {u"100000000", 0xffffffff},
	    {u"ffffffffffffffff", 0xffffffff},
	    {u"-1", 0xffffffff},
	    {u"-ffffffff", 0xff000000},
	    {u"-100000000", 0xff000000},
	    {u"+12", 0xff000012},
	    {u"0x12ab", 0xff0012ab},
	    {u"0XABCDEF", 0xffabcdef},
	    {u"0x", 0xff000000},
	    {u"xyz", 0xff000000},
	    {u"12g34", 0xff000012},
	    {u" 12", 0xff000012},
	    {u"\t\r\n12", 0xff000012},
	    {u"--12", 0xff000000},
	    {u"+-12", 0xff000000},
	    {u"0000000000000000000012", 0xff000012},
	    {u"7fffffff", 0xffffffff},
	    {u"80000000", 0xffffffff},
	    {u"ffffffff0", 0xffffffff},
	    {u"-ffffffffffffffff", 0xff000000},
	    {u"-80000000", 0xff000000},
	    {u"+", 0xff000000},
	    {u"  ", 0xff000000},
	}};
	for (const auto& [operand, expected] : cases) {
		MeasuredFixture fixture;
		ConfigureNativeMarkupFormatting(fixture.Formatting);
		std::u16string input = u"#";
		input.append(operand);
		input.append(u";A#;B");
		Require(fixture.Content.Render(Units(input), fixture.Formatting, fixture.Options, {280, 0}, {}, fixture.Callbacks), "native ASCII color conversion accepts its actual delimited operand");
		const auto& line = fixture.Content.CurrentLine();
		Require(line.size() == 2 && line[0].Style.MainColor == expected && line[1].Style.MainColor == 0x11223344 && fixture.Formatting.Current.Color == 0x11223344, "native signed saturation/invalid prefix/default restoration differs");
		Require(fixture.Prepared == std::vector<std::uint16_t>{u'A', u'B'} && fixture.Events == std::vector<std::string>{"normal", "unit", "ruby"}, "color directives emit no glyph or font-selection callback");
	}
}

static void TestNativeEdgeShadowControlsAndOwnedStyles() {
	MeasuredFixture fixture;
	ConfigureNativeMarkupFormatting(fixture.Formatting);
	Require(fixture.Content.Render(Units(u"A%e1B%e#334455;C%e0D%s0E%s#778899;F%sXG%eXH%e#;I%s#;J"), fixture.Formatting, fixture.Options, {280, 0}, {}, fixture.Callbacks), "native effect directives accept their actual one-unit or hash-color operands");
	const auto& line = fixture.Content.CurrentLine();
	Require(line.size() == 10 && !line[0].Style.EdgeEnabled && line[1].Style.EdgeEnabled && line[2].Style.EdgeEnabled && !line[3].Style.EdgeEnabled, "edge color changes must not alter the separately controlled enable flag");
	Require(line[2].Style.EdgeColor == 0xff334455 && line[3].Style.EdgeColor == 0xff334455 && line[8].Style.EdgeColor == 0x55667788, "edge color/default snapshots must follow each actual source unit");
	Require(!line[4].Style.ShadowEnabled && !line[5].Style.ShadowEnabled && line[5].Style.ShadowColor == 0xff778899 && line[6].Style.ShadowEnabled && line[9].Style.ShadowColor == 0x12345678, "shadow color, enable and unknown-operand default restoration differ");
	fixture.Formatting.Current.Color = 0;
	fixture.Formatting.Current.EdgeColor = 0;
	Require(line[0].Style.MainColor == 0x8899aabb && line[2].Style.EdgeColor == 0xff334455, "retained element styles must not alias mutable parser state");
	Require(fixture.Events == std::vector<std::string>{"normal", "unit", "ruby"}, "edge/shadow appearance changes must not select a different font");

	MeasuredFixture literal;
	ConfigureNativeMarkupFormatting(literal.Formatting);
	Require(literal.Content.Render(Units(u"A%e1;B%s0;C%e"), literal.Formatting, literal.Options, {280, 0}, {}, literal.Callbacks), "one-unit effect operands and missing final operand follow native consumption");
	Require(literal.Prepared == std::vector<std::uint16_t>{u'A', u';', u'B', u';', u'C'} && literal.Formatting.Current.Edge && !literal.Formatting.Current.Shadow, "a semicolon after a non-color effect operand remains an ordinary glyph");
}

static void TestNativeEffectIgnoreFlagsAndRemainingDomains() {
	MeasuredFixture colors;
	ConfigureNativeMarkupFormatting(colors.Formatting);
	colors.Options.IgnoreColor = true;
	Require(colors.Content.Render(Units(u"#112233;A%e#334455;B%s#667788;C%e1D%s0E"), colors.Formatting, colors.Options, {280, 0}, {}, colors.Callbacks), "IgnoreColor still consumes complete effects and permits native type changes");
	Require(colors.Formatting.Current.Color == 0x8899aabb && colors.Formatting.Current.EdgeColor == 0xa1b2c3d4 && colors.Formatting.Current.ShadowColor == 0x99abcdef && colors.Formatting.Current.Edge && !colors.Formatting.Current.Shadow, "IgnoreColor must guard only color stores");
	MeasuredFixture types;
	ConfigureNativeMarkupFormatting(types.Formatting);
	types.Options.IgnoreType = true;
	Require(types.Content.Render(Units(u"%e1A%s0B%e#112233;C"), types.Formatting, types.Options, {280, 0}, {}, types.Callbacks), "IgnoreType still admits actual color changes");
	Require(!types.Formatting.Current.Edge && types.Formatting.Current.Shadow && types.Formatting.Current.EdgeColor == 0xff112233, "IgnoreType must guard only enabled flags");
	MeasuredFixture unsupported;
	RequireFailure([&] {
		static_cast<void>(unsupported.Content.Render(Units(u"#\u300012;A"), unsupported.Formatting, unsupported.Options, {}, {}, unsupported.Callbacks));
	},
	    "Unicode numeric classification");
	Require(unsupported.Events.empty() && unsupported.Prepared.empty(), "unrecovered numeric classification must reject before font/content mutation");
	RequireFailure([&] {
		static_cast<void>(unsupported.Content.Render(Units(u"%150;A"), unsupported.Formatting, unsupported.Options, {}, {}, unsupported.Callbacks));
	},
	    "markup effect");
	Require(unsupported.Events.empty() && unsupported.Prepared.empty(), "font-size markup remains a separate actual provider transition gate");
}

static void TestNativePitchDirectivePlacementAndDefaults() {
	MeasuredFixture fixture;
	fixture.Formatting.Defaults.Pitch = 1.25f;
	Require(fixture.Content.Render(Units(u"A%p-1;BC%p;D"), fixture.Formatting, fixture.Options, {280, 0}, {}, fixture.Callbacks), "actual percent-p pair accepts its signed decimal and empty reset");
	const auto& line = fixture.Content.CurrentLine();
	Require(line.size() == 4 && line[0].Geometry.Position.X == 0 && line[1].Geometry.Position.X == 7 && line[2].Geometry.Position.X == 13 && line[3].Geometry.Position.X == 19 && fixture.Content.Cursor().X == 27.25f, "pitch directives must affect real successive glyph placement rather than glyph metrics");
	Require(fixture.Formatting.Current.Pitch == 1.25f && fixture.Prepared == std::vector<std::uint16_t>{u'A', u'B', u'C', u'D'} && fixture.Events == std::vector<std::string>{"normal", "unit", "ruby"}, "pitch default reset emits no element or provider refresh");

	MeasuredFixture ignored;
	ignored.Options.IgnoreStyle = true;
	ignored.Formatting.Current.Pitch = 2.125f;
	ignored.Formatting.Defaults.Pitch = -0.5f;
	Require(ignored.Content.Render(Units(u"A%p-1;B%p;C"), ignored.Formatting, ignored.Options, {280, 0}, {}, ignored.Callbacks), "IgnoreStyle still consumes pitch operands");
	Require(ignored.Formatting.Current.Pitch == 2.125f && ignored.Content.CurrentLine()[1].Geometry.Position.X == 9.125f && ignored.Content.CurrentLine()[2].Geometry.Position.X == 18.25f, "IgnoreStyle must retain actual existing pitch for both numeric and empty operands");

	MeasuredFixture zeros;
	zeros.Formatting.Defaults.Pitch = -0.0f;
	std::vector<std::uint32_t> words;
	const auto prepare = zeros.Callbacks.PrepareNormal;
	zeros.Callbacks.PrepareNormal = [&](std::uint16_t unit) {
		words.push_back(std::bit_cast<std::uint32_t>(zeros.Formatting.Current.Pitch));
		return prepare(unit);
	};
	Require(zeros.Content.Render(Units(u"%p-0;A%p;B"), zeros.Formatting, zeros.Options, {280, 0}, {}, zeros.Callbacks), "numeric zero and default negative zero remain distinct source operations");
	Require(words == std::vector<std::uint32_t>{0, 0x80000000}, "integer conversion makes positive zero while empty pitch reset copies the default word");
}

static std::uint32_t ReadWord() {
	std::array<std::uint8_t, 4> bytes{};
	std::cin.read(reinterpret_cast<char*>(bytes.data()), 4);
	Require(bool(std::cin), "native content stream word extent");
	return std::uint32_t(bytes[0]) | (std::uint32_t(bytes[1]) << 8) | (std::uint32_t(bytes[2]) << 16) | (std::uint32_t(bytes[3]) << 24);
}

static void NativeWord(std::uint32_t actual, std::string_view context) {
	const auto expected = ReadWord();
	Require(actual == expected, std::string(context) + " expected " + std::to_string(expected) + " actual " + std::to_string(actual));
}

static void TestWholeNativeContent(bool read_indent = false, bool read_edges = false, bool read_markup = false, bool read_pitch = false) {
	const auto length = ReadWord();
	Require(length != 0 && length <= 64 * 1024 * 1024, "native real font extent");
	auto bytes = std::make_shared<std::vector<std::uint8_t>>(length);
	std::cin.read(reinterpret_cast<char*>(bytes->data()), length);
	Require(bool(std::cin), "native real font bytes");
	sgre::text::VectorFont font(bytes);
	font.SetSize(24);
	sgre::text::TextFormattingState formatting;
	formatting.Defaults.WordBreak = false;
	sgre::text::TextProcessingOptions options;
	sgre::text::TextContentOwner owner;
	sgre::text::TextContentCallbacks callbacks;
	callbacks.Fonts.SelectNormal = [] {
	};
	callbacks.Fonts.MeasureNormal = [&](auto unit) {
		return font.MeasureCodeUnit(unit);
	};
	callbacks.Fonts.SelectRuby = [] {
	};
	callbacks.PrepareNormal = [&](auto unit) {
		static_cast<void>(font.MeasureCodeUnit(unit));
		const auto glyphs = font.InputBuffer().InputGlyphs();
		Require(glyphs.size() == 1, "native real single-unit glyph count");
		const auto row = font.InputBuffer().PositionRow(0);
		sgre::text::ShapedTextGlyph shaped;
		shaped.GlyphIndex = glyphs.front().GlyphIndex;
		shaped.XAdvance26Dot6 = row.XAdvance26Dot6;
		shaped.YAdvance26Dot6 = row.YAdvance26Dot6;
		shaped.XOffset26Dot6 = row.XOffset26Dot6;
		shaped.YOffset26Dot6 = row.YOffset26Dot6;
		sgre::text::OwnedTextGlyphRun run;
		run.Glyphs.resize(1);
		run.Advance = sgre::text::PrepareTextGlyphRun(std::span<const sgre::text::ShapedTextGlyph>(&shaped, 1), run.Glyphs);
		return run;
	};
	sgre::text::ResetTextFontDefaults(formatting, callbacks.Fonts);
	sgre::text::ResetTextGeometryDefaults(formatting);
	sgre::text::TextContentRequest request;
	sgre::text::TextCursor render_size{280, 0};
	if (read_markup) {
		ConfigureNativeMarkupFormatting(formatting);
		options.IgnoreColor = ReadWord() != 0;
		options.IgnoreType = ReadWord() != 0;
		render_size.X = std::bit_cast<float>(ReadWord());
		formatting.Current.ShadowOffset = std::bit_cast<std::int32_t>(ReadWord());
		formatting.Current.EdgeSize = std::bit_cast<std::int32_t>(ReadWord());
	}
	if (read_pitch) {
		formatting.Defaults.Pitch = std::bit_cast<float>(ReadWord());
		formatting.Current.Pitch = std::bit_cast<float>(ReadWord());
		options.IgnoreStyle = ReadWord() != 0;
	}
	if (read_indent) {
		request.Indent = std::bit_cast<std::int32_t>(ReadWord());
		render_size.X = std::bit_cast<float>(ReadWord());
	}
	bool expected_accepted = true;
	if (read_edges) {
		render_size.Y = std::bit_cast<float>(ReadWord());
		expected_accepted = ReadWord() != 0;
	}
	const auto count = ReadWord();
	Require(count <= 8192, "native content input count");
	std::vector<std::uint16_t> input;
	for (std::uint32_t i = 0; i < count; ++i) {
		const auto unit = ReadWord();
		Require(unit <= 65535, "native content UTF16 range");
		input.push_back(static_cast<std::uint16_t>(unit));
	}
	Require(owner.Render(input, formatting, options, render_size, request, callbacks) == expected_accepted, "whole original render acceptance");
	if (read_edges) {
		NativeWord(owner.RenderedElementCount(), "native render-attempt count before done");
		NativeWord(static_cast<std::uint32_t>(owner.ElementCount()), "native retained count before done");
		NativeWord(std::bit_cast<std::uint32_t>(owner.Cursor().X), "native cursor X before done");
		NativeWord(std::bit_cast<std::uint32_t>(owner.Cursor().Y), "native cursor Y before done");
	}
	if (read_pitch) {
		NativeWord(std::bit_cast<std::uint32_t>(formatting.Current.Pitch), "native current pitch before done");
		NativeWord(std::bit_cast<std::uint32_t>(owner.Cursor().X), "native pitch cursor X before done");
		NativeWord(std::bit_cast<std::uint32_t>(owner.Cursor().Y), "native pitch cursor Y before done");
	}
	owner.Finalize(formatting, options, render_size);
	NativeWord(static_cast<std::uint32_t>(owner.Lines().size()), "native retained history count");
	for (const auto& line : owner.Lines()) {
		NativeWord(static_cast<std::uint32_t>(line.Elements.size()), "native retained element count");
		for (const auto& element : line.Elements) {
			NativeWord(static_cast<std::uint32_t>(element.Text.front()), "native actual element text");
			for (const auto value : std::array<float, 7>{element.Geometry.Position.X, element.Geometry.Position.Y, element.Geometry.Metrics.Width, element.Geometry.Metrics.Height, element.Run.Advance.X, element.Run.Advance.Y, element.RevealTime}) {
				NativeWord(std::bit_cast<std::uint32_t>(value), "native owned element word");
			}
			if (read_markup) {
				const auto& style = element.Style;
				for (const auto word : std::array<std::uint32_t, 9>{style.MainColor, static_cast<std::uint32_t>(style.Bold), static_cast<std::uint32_t>(style.Italic), static_cast<std::uint32_t>(style.ShadowEnabled), style.ShadowColor, std::bit_cast<std::uint32_t>(style.ShadowOffset), static_cast<std::uint32_t>(style.EdgeEnabled), style.EdgeColor, std::bit_cast<std::uint32_t>(style.EdgeWidth)}) {
					NativeWord(word, "native retained markup style word");
				}
			}
			NativeWord(static_cast<std::uint32_t>(element.Run.Glyphs.size()), "native owned glyph count");
			for (const auto& glyph : element.Run.Glyphs) {
				NativeWord(glyph.GlyphIndex, "native owned glyph ID");
				NativeWord(std::bit_cast<std::uint32_t>(glyph.Position.X), "native owned glyph X");
				NativeWord(std::bit_cast<std::uint32_t>(glyph.Position.Y), "native owned glyph Y");
			}
		}
	}
	NativeWord(static_cast<std::uint32_t>(owner.ProcessedText().size()), "native processed text length");
	for (const auto unit : owner.ProcessedText()) {
		NativeWord(static_cast<std::uint32_t>(unit), "native processed text unit");
	}
	if (read_markup) {
		const auto& current = formatting.Current;
		for (const auto word : std::array<std::uint32_t, 9>{current.Color, current.ShadowColor, current.EdgeColor, static_cast<std::uint32_t>(current.Bold), static_cast<std::uint32_t>(current.Italic), static_cast<std::uint32_t>(current.Shadow), static_cast<std::uint32_t>(current.Edge), std::bit_cast<std::uint32_t>(current.FontSize), std::bit_cast<std::uint32_t>(current.FontBaseWidth)}) {
			NativeWord(word, "native final markup state word");
		}
	}
	if (read_pitch) {
		NativeWord(std::bit_cast<std::uint32_t>(formatting.Current.Pitch), "native final pitch word");
	}
}

int main(int argc, char** argv) {
	try {
		if (argc == 2 && std::string_view(argv[1]) == "--native-pitch") {
			TestWholeNativeContent(false, false, true, true);
			std::cout << "whole native pitch words passed\n";
			return 0;
		}
		if (argc == 2 && std::string_view(argv[1]) == "--native-markup") {
			TestWholeNativeContent(false, false, true);
			std::cout << "whole native markup words passed\n";
			return 0;
		}
		if (argc == 2 && (std::string_view(argv[1]) == "--native" || std::string_view(argv[1]) == "--native-indent" || std::string_view(argv[1]) == "--native-indent-edges")) {
			TestWholeNativeContent(std::string_view(argv[1]) != "--native", std::string_view(argv[1]) == "--native-indent-edges");
			std::cout << "whole native content words passed\n";
			return 0;
		}
		TestNativeMeasuredAppendAndHistoryOwnership();
		TestNativeCenterDirectiveAndProcessedPrefix();
		TestOverflowRetiresActualCurrentLine();
		TestPreflightAndCallbackReentry();
		TestRepeatedEmptyAndRejectedRenderContinuation();
		TestNativeEmptyAppendTimingAndKeep();
		TestAppendedRevealQueryReadinessAndSorting();
		TestSignedIndentWithoutGrouping();
		TestNativeQuoteNestingAndLineStarts();
		TestDistinctOpeningMeasurementAndCallbackOrder();
		TestEscapesAndRejectedQuoteAppends();
		TestRemainingContentDomainsStayExplicit();
		TestNativeColorConversionAndRestoration();
		TestNativeEdgeShadowControlsAndOwnedStyles();
		TestNativeEffectIgnoreFlagsAndRemainingDomains();
		TestNativePitchDirectivePlacementAndDefaults();
		std::cout << "text content ownership tests passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
