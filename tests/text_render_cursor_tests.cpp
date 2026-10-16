#include "sgre/text/content_owner.hpp"
#include "sgre/text/recursive_decomposition.hpp"
#include "sgre/text/vector_font.hpp"

#include "glyph_atlas_owner_fixture.hpp"
#include "text_glyph_drawing_fixture.hpp"
#include "text_render_cursor_native_vectors.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
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
static void RequireFailure(Operation operation, std::string_view expected) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(expected) != std::string_view::npos, error.what());
		return;
	}
	throw std::runtime_error("text render cursor expected actual owner failure");
}

static std::uint32_t ReadWord() {
	std::array<std::uint8_t, 4> bytes{};
	std::cin.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
	Require(bool(std::cin), "native cursor stream word extent");
	return std::uint32_t(bytes[0]) | (std::uint32_t(bytes[1]) << 8) | (std::uint32_t(bytes[2]) << 16) | (std::uint32_t(bytes[3]) << 24);
}

static void NativeWord(std::uint32_t actual, std::size_t& ordinal) {
	const auto expected = ReadWord();
	Require(actual == expected, "native cursor word " + std::to_string(ordinal++) + " expected " + std::to_string(expected) + " actual " + std::to_string(actual));
}

static void NativeCursorState(const sgre::text::TextContentOwner& owner, std::size_t& ordinal) {
	const auto cursor = owner.RenderCursor();
	const auto current = owner.Cursor();
	NativeWord(std::bit_cast<std::uint32_t>(cursor.X), ordinal);
	NativeWord(std::bit_cast<std::uint32_t>(cursor.Y), ordinal);
	NativeWord(std::bit_cast<std::uint32_t>(current.X), ordinal);
	NativeWord(std::bit_cast<std::uint32_t>(current.Y), ordinal);
	NativeWord(std::bit_cast<std::uint32_t>(sgre::script::TryNativeInteger(cursor.X).value()), ordinal);
	NativeWord(std::bit_cast<std::uint32_t>(sgre::script::TryNativeInteger(cursor.Y).value()), ordinal);
}

static void TestWholeNativeCursor() {
	const auto size = ReadWord();
	Require(size != 0 && size <= 64 * 1024 * 1024, "native cursor actual font extent");
	auto bytes = std::make_shared<std::vector<std::uint8_t>>(size);
	std::cin.read(reinterpret_cast<char*>(bytes->data()), size);
	Require(bool(std::cin), "native cursor font bytes");
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
		Require(glyphs.size() == 1, "native cursor actual single unit glyph");
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
	const sgre::text::TextCursor render_size{std::bit_cast<float>(ReadWord()), std::bit_cast<float>(ReadWord())};
	formatting.Current.Alignment = std::bit_cast<std::int32_t>(ReadWord());
	formatting.Current.Pitch = std::bit_cast<float>(ReadWord());
	formatting.Defaults.WordBreak = ReadWord() != 0;
	formatting.Current.WordBreak = ReadWord() != 0;
	const auto count = ReadWord();
	Require(count <= 8192, "native cursor input unit extent");
	std::vector<std::uint16_t> input;
	for (std::uint32_t index = 0; index < count; ++index) {
		const auto unit = ReadWord();
		Require(unit <= 65535, "native cursor UTF16 unit");
		input.push_back(static_cast<std::uint16_t>(unit));
	}
	const bool accepted = ReadWord() != 0;
	std::size_t ordinal = 0;
	NativeCursorState(owner, ordinal);
	Require(owner.Render(input, formatting, options, render_size, {}, callbacks) == accepted, "native cursor actual render acceptance");
	NativeCursorState(owner, ordinal);
	owner.Finalize(formatting, options, render_size);
	NativeCursorState(owner, ordinal);
	owner.Clear();
	NativeCursorState(owner, ordinal);
}

static void TestActualCursorPublicationAndRetirement() {
	text_atlas_test::Backend backend;
	sgre::scene::TextureCache cache([](auto, const auto&) -> sgre::render::SpriteTextureHandle {
		throw std::runtime_error("cursor fixture must use genuine vector font");
	},
	    [](auto) {
	    });
	text_drawing_test::Fixture* active = nullptr;
	std::int32_t draw_cursor = -1;
	text_drawing_test::Fixture fixture(cache, backend, [&](auto&, auto, const auto&) {
		draw_cursor = std::get<std::int32_t>(active->Call("getRenderCurX"));
	});
	active = &fixture;
	const auto x = [&] {
		return std::get<std::int32_t>(fixture.Call("getRenderCurX", {std::string("ignored")}));
	};
	const auto y = [&] {
		return std::get<std::int32_t>(fixture.Call("getRenderCurY"));
	};
	Require(x() == 0 && y() == 0, "constructed actual text has zero completed cursor");
	fixture.Call("setDefault", {fixture.Properties({{"pitch", 0.75f}})});
	fixture.Call("resetStyle");
	Require(std::get<bool>(fixture.Call("render", {std::string("BB"), std::int32_t{0}, std::int32_t{0}, std::int32_t{0}, false})), "cursor fixture renders actual two glyphs");
	Require(x() == 0 && y() == 0 && fixture.Text.Context(fixture.Instance).Content().Cursor().X == 17.5f, "Render changes working cursor without publishing Done snapshot");
	fixture.Call("done");
	Require(x() == 17 && y() == 0 && fixture.Text.Context(fixture.Instance).Content().Cursor().Y == 8, "Done retains pre-line cursor and integer getter truncates rather than rounds");
	fixture.Draw();
	Require(draw_cursor == 17, "query during genuine glyph drawing is read-only and retains completed cursor");
	fixture.Call("setCoord", {100.0f, 200.0f});
	fixture.Call("setTimeScale", {3.0f});
	fixture.Call("setShowCount", {std::int32_t{0}});
	Require(x() == 17 && y() == 0, "position, reveal and timing controls do not replace completed parser cursor");
	// Default10 has never been selected; Clear must reach the real provider after current9.
	fixture.Call("setDefault", {fixture.Properties({{"fontsize", std::int32_t{10}}})});
	fixture.Call("setFont", {fixture.Properties({{"fontsize", std::int32_t{9}}})});
	fixture.Call("clearFont");
	bool saw_callback = false;
	std::int32_t callback_x = -1;
	std::int32_t callback_y = -1;
	float callback_size = 0;
	fixture.Finding = [&] {
		saw_callback = true;
		callback_x = x();
		callback_y = y();
		callback_size = fixture.Text.Context(fixture.Instance).Formatting().Current.FontSize;
	};
	fixture.Call("clear");
	Require(saw_callback && callback_x == 0 && callback_y == 0 && callback_size == 10, "uncached actual Clear callback observes its already-reset query cursor and new default key");
	Require(x() == 0 && y() == 0, "successful Clear retains the observed native query reset");
	fixture.Finding = {};
	fixture.Call("setDefault", {fixture.Properties({{"fontsize", std::int32_t{8}}})});
	fixture.Call("clear");
	fixture.Call("setVertical", {true});
	fixture.Call("setRenderSize", {123.75f, 0.0f});
	Require(x() == 123 && y() == 0, "genuine empty vertical clear starts cursor at requested width without admitting vertical text");
	fixture.Call("done");
	Require(x() == 123 && y() == 0, "empty Done also publishes the actual clear cursor");
	fixture.Call("setVertical", {false});
	Require(x() == 123, "vertical flag mutation alone does not rewrite completed cursor");
	fixture.Call("clear");
	Require(x() == 0 && y() == 0, "horizontal clear resets both completed cursor words");
	fixture.Render("B");
	Require(x() == 8 && y() == 0, "same genuine owner can publish a new cursor after clear");
	fixture.Layers.ReleaseInstance(fixture.Layer);
	Require(x() == 0 && y() == 0, "parent-first retirement returns native integer zero without accessing retired content");
	fixture.Text.ReleaseInstance(fixture.Instance);
	RequireFailure([&] {
		static_cast<void>(x());
	},
	    "live native wrapper");
}

static void TestPropagatedClearServiceFailure() {
	text_atlas_test::Backend backend;
	sgre::text::TextRenderContext context(backend);
	context.SetVertical(true);
	sgre::text::TextCursor observed;
	sgre::text::TextFontResetCallbacks callbacks;
	callbacks.SelectNormal = [&] {
		observed = context.Content().RenderCursor();
		throw std::runtime_error("actual C++ selection service failure");
	};
	callbacks.MeasureNormal = [](std::uint16_t) -> sgre::resource::GlyphSize {
		throw std::runtime_error("failed selector must precede measurement");
	};
	callbacks.SelectRuby = [] {
	};
	RequireFailure([&] {
		context.SetRenderSize({55.5f, 0}, callbacks);
	},
	    "actual C++ selection service failure");
	Require(observed.X == 55.5f && observed.Y == 0 && context.Content().RenderCursor().X == 55.5f, "propagated service failure keeps the native cursor store preceding that callback");
	context.SetVertical(false);
	context.ClearEmpty(callbacks);
	Require(context.Content().RenderCursor().X == 0 && context.Content().RenderCursor().Y == 0 && backend.Observed.Creates == 0, "CancelClear must release operation state for a later real empty clear without fabricating glyph resources");
}

static void TestNativeIntegerConversionWords() {
	for (const auto& row : kNativeCursorConversions) {
		for (std::size_t axis = 0; axis < 2; ++axis) {
			const auto actual = sgre::script::TryNativeInteger(std::bit_cast<float>(row[axis]));
			Require(actual && std::bit_cast<std::uint32_t>(*actual) == row[axis + 2], "shared native integer converter differs from actual cursor CVTTSS2SI");
		}
	}
}

int main(int argc, char** argv) {
	try {
		if (argc == 2 && std::string_view(argv[1]) == "--native") {
			TestWholeNativeCursor();
		} else {
			Require(argc == 1, "unexpected cursor fixture arguments");
			TestNativeIntegerConversionWords();
			TestPropagatedClearServiceFailure();
			TestActualCursorPublicationAndRetirement();
		}
		std::cout << "text render cursor owner/query tests passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
