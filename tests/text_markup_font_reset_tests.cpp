#include "sgre/text/content_owner.hpp"

#include "glyph_atlas_owner_fixture.hpp"
#include "text_glyph_drawing_fixture.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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

	throw std::runtime_error("font-reset fixture expected failure");
}

static std::vector<std::uint16_t> Units(std::u16string_view input) {
	return {input.begin(), input.end()};
}

struct FontResetFixture {
	sgre::text::TextContentOwner Content;
	sgre::text::TextFormattingState Formatting;
	sgre::text::TextProcessingOptions Options;
	sgre::text::TextContentCallbacks Callbacks;
	std::vector<std::string> Events;
	std::vector<std::uint16_t> Prepared;

	FontResetFixture() {
		Formatting.Defaults.FontSize = 12;
		Formatting.Defaults.LineSize = 12;
		Formatting.Defaults.Shadow = false;
		Formatting.Defaults.WordBreak = false;
		Callbacks.Fonts.SelectNormal = [this] {
			Events.emplace_back("normal");
		};
		Callbacks.Fonts.MeasureNormal = [this](std::uint16_t unit) {
			Require(unit == 0x3000, "font reset must measure its genuine ideographic-space request");
			Events.emplace_back("measure");
			return sgre::resource::GlyphSize{7, 12};
		};
		Callbacks.Fonts.SelectRuby = [this] {
			Events.emplace_back("ruby");
		};
		Callbacks.PrepareNormal = [this](std::uint16_t unit) {
			Prepared.push_back(unit);
			sgre::text::OwnedTextGlyphRun run;
			run.Glyphs.push_back({unit, {0, 0}});
			run.Advance = {7, 0};
			return run;
		};
		sgre::text::ResetTextFontDefaults(Formatting, Callbacks.Fonts);
		sgre::text::ResetTextGeometryDefaults(Formatting);
		Events.clear();
	}

	void Render(std::u16string_view input) {
		Require(Content.Render(Units(input), Formatting, Options, {}, {}, Callbacks), "synthetic reset content unexpectedly overflowed");
	}
};

static void TestTipColorResetAndOwnedHistory() {
	FontResetFixture fixture;
	auto input = Units(u"A#ff8A00;B%rC");
	const auto original = input;
	Require(fixture.Content.Render(input, fixture.Formatting, fixture.Options, {}, {}, fixture.Callbacks), "actual tips control sequence must accept");
	Require(input == original && fixture.Prepared == std::vector<std::uint16_t>{u'A', u'B', u'C'}, "color/reset controls must preserve input and create no glyph");
	const auto& current = fixture.Content.CurrentLine();
	Require(current.size() == 3 && current[0].Style.MainColor == 0xffffffff && current[1].Style.MainColor == 0xffff8a00 && current[2].Style.MainColor == 0xffffffff, "reset must restore defaults only for subsequent real elements");
	Require(fixture.Events == std::vector<std::string>{"normal", "measure", "ruby"}, "matching reset keys must avoid redundant provider selection");
	input.assign(input.size(), u'X');
	fixture.Content.Finalize(fixture.Formatting, fixture.Options, {});
	Require(fixture.Content.ProcessedText() == u"ABC\n" && fixture.Content.Plan().RenderOrder == std::vector<std::uint32_t>{0, 1, 2}, "Done must retain owned text and exact glyph identity after source retirement");
	fixture.Content.Clear();
	Require(fixture.Content.ElementCount() == 0 && fixture.Content.Lines().empty(), "Clear must release reset-bearing content normally");
}

static void TestUnconditionalResetAndGeometryScope() {
	for (const bool ignore_type : std::array<bool, 2>{false, true}) {
		FontResetFixture fixture;
		fixture.Options.IgnoreType = ignore_type;
		fixture.Options.IgnoreColor = true;
		fixture.Options.IgnoreSize = true;
		fixture.Options.IgnoreFace = true;
		fixture.Options.IgnoreStyle = true;
		fixture.Formatting.Current.Color = 0x11223344;
		fixture.Formatting.Current.Edge = true;
		fixture.Formatting.Current.Pitch = -3;
		fixture.Formatting.Current.Alignment = 2;
		fixture.Formatting.Current.LineSize = 37;
		fixture.Formatting.Current.LineSpacing = 4;
		fixture.Render(u"%rB");
		Require(fixture.Formatting.Current.Color == 0xffffffff && !fixture.Formatting.Current.Edge, "all ignore flags must leave the native font reset unconditional");
		Require(fixture.Formatting.Current.Pitch == -3 && fixture.Formatting.Current.Alignment == 2 && fixture.Formatting.Current.LineSize == 37 && fixture.Formatting.Current.LineSpacing == 4, "font reset must not invoke the distinct geometry reset");
	}
	FontResetFixture fixture;
	fixture.Render(u"%r;B%r");
	Require(fixture.Prepared == std::vector<std::uint16_t>{u';', u'B'}, "percent-r is immediate; a following semicolon is ordinary text");
}

static void TestActualResetCallbacksAndLateDefaults() {
	FontResetFixture fixture;
	fixture.Formatting.Current.FontSize = 20;
	fixture.Formatting.Current.Bold = true;
	fixture.Formatting.Current.NormalFaceIndex = 7;
	fixture.Formatting.Current.RubySize = 18;
	fixture.Callbacks.Fonts.SelectNormal = [&] {
		fixture.Events.emplace_back("normal");
		if (fixture.Formatting.Current.FontSize == fixture.Formatting.Defaults.FontSize) {
			Require(!fixture.Formatting.Current.Bold && fixture.Formatting.Current.NormalFaceIndex == 0, "normal reset must publish real keys before callback");
			fixture.Formatting.Defaults.RubySize = 9;
			fixture.Formatting.Defaults.Color = 0xff123456;
		}
	};
	fixture.Callbacks.Fonts.SelectRuby = [&] {
		fixture.Events.emplace_back("ruby");
		if (fixture.Formatting.Current.RubySize == 9) {
			fixture.Formatting.Defaults.EdgeSize = 11;
		}
	};
	fixture.Render(u"A%rB");
	Require(fixture.Events == std::vector<std::string>{"normal", "measure", "ruby", "normal", "measure", "ruby"}, "reset must execute actual normal/measure/ruby callbacks in order");
	Require(fixture.Formatting.Current.FontBaseWidth == 7 && fixture.Formatting.Current.RubySize == 9 && fixture.Formatting.Current.EdgeSize == 11, "reset must use live callback-mutated defaults and real width");
	Require(fixture.Content.CurrentLine()[0].Font.RequestedSize == 20 && fixture.Content.CurrentLine()[1].Font.RequestedSize == 12 && fixture.Content.CurrentLine()[1].Style.MainColor == 0xff123456, "each owned element must retain the font/color selected at its own append");
}

static void TestPreflightAndFailedCallbackRelease() {
	FontResetFixture fixture;
	RequireFailure([&] {
		fixture.Render(u"#ff8A00;B%r$unknown;");
	},
	    "markup effect");
	Require(fixture.Events.empty() && fixture.Content.ElementCount() == 0, "later unsupported markup must reject before reset/color/font mutations");
	fixture.Formatting.Current.FontSize = 20;
	std::size_t normal_calls = 0;
	fixture.Callbacks.Fonts.SelectNormal = [&] {
		if (++normal_calls == 2) {
			RequireFailure([&] {
				fixture.Content.Clear();
			},
			    "cannot clear");
			throw std::runtime_error("actual reset selector failed");
		}
	};
	RequireFailure([&] {
		fixture.Render(u"A%rB");
	},
	    "actual reset selector failed");
	Require(fixture.Prepared == std::vector<std::uint16_t>{u'A'}, "a failed actual selector must not manufacture the next glyph");
	fixture.Content.Clear();
	fixture.Render(u"B");
	Require(fixture.Content.CurrentLine().size() == 1, "callback failure must release the real operation borrow for ordinary Clear/reuse");
}

static void TestRealVmFontOwnerAndDrawReceiving() {
	text_atlas_test::Backend backend;
	sgre::scene::TextureCache cache([](auto, const auto&) -> sgre::render::SpriteTextureHandle {
		throw std::runtime_error("reset fixture must select its real vector owner");
	},
	    [](auto) {
	    });
	std::vector<sgre::text::VectorGlyphDrawRectangle> drawn;
	text_drawing_test::Fixture fixture(cache, backend, [&](auto&, auto rectangles, const auto&) {
		drawn.insert(drawn.end(), rectangles.begin(), rectangles.end());
	});
	fixture.Render("#ff8A00;B%rB");
	fixture.Draw();
	Require(drawn.size() == 2 && drawn[0].ColorRgba == 0x008affff && drawn[1].ColorRgba == 0x804020ff, "actual VM/render/done/atlas/draw must preserve orange then default color");
	const auto finds = fixture.FindCalls;
	fixture.Call("clear");
	Require(fixture.Text.Context(fixture.Instance).Content().ElementCount() == 0 && fixture.FindCalls == finds, "actual clear releases retained tip elements and existing private fonts after cached reset callbacks without reacquiring providers");
	fixture.Render("B");
	drawn.clear();
	fixture.Draw();
	Require(drawn.size() == 1 && drawn[0].ColorRgba == 0x804020ff && fixture.FindCalls == finds + 2, "native full Clear retires private font interfaces; subsequent rendering reacquires normal/ruby while preserving the source and default appearance");
}

int main() {
	try {
		TestTipColorResetAndOwnedHistory();
		TestUnconditionalResetAndGeometryScope();
		TestActualResetCallbacksAndLateDefaults();
		TestPreflightAndFailedCallbackRelease();
		TestRealVmFontOwnerAndDrawReceiving();
		std::cout << "Text markup font reset: 5 groups PASS\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
