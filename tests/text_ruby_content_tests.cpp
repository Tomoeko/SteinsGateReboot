#include "sgre/text/content_owner.hpp"
#include "sgre/text/recursive_decomposition.hpp"
#include "sgre/text/vector_font.hpp"

#include "glyph_atlas_owner_fixture.hpp"
#include "text_glyph_drawing_fixture.hpp"
#include "vector_run_fixture.hpp"

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

static void Require(bool condition, std::string_view context) {
	if (!condition) {
		throw std::runtime_error(std::string(context));
	}
}

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view context) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, error.what());
		return;
	}
	throw std::runtime_error("ruby fixture expected failure: " + std::string(context));
}

static std::vector<std::uint16_t> Units(std::u16string_view input) {
	return {input.begin(), input.end()};
}

static void TestOwnedRubyProviderDrawAndReveal() {
	text_atlas_test::Backend backend;
	sgre::scene::TextureCache cache([](auto, const auto&) -> sgre::render::SpriteTextureHandle {
		throw std::runtime_error("ruby fixture must use its actual vector provider");
	},
	    [](auto) {
	    });
	std::vector<sgre::text::VectorGlyphDrawRectangle> drawn;
	std::vector<std::size_t> batches;
	text_drawing_test::Fixture* active = nullptr;
	text_drawing_test::Fixture fixture(cache, backend, [&](auto&, auto rectangles, const auto&) {
		drawn.insert(drawn.end(), rectangles.begin(), rectangles.end());
		batches.push_back(rectangles.size());
		RequireFailure([&] {
			active->Call("clear");
		},
		    "during actual glyph");
	});
	active = &fixture;
	Require(std::get<bool>(fixture.Call("render", {std::string("[B,1]BB"), std::int32_t{0}, std::int32_t{3}, std::int32_t{0}, false})), "actual counted ruby must render through its real FontInfo owner");
	fixture.Call("done");
	const auto& content = fixture.Text.Context(fixture.Instance).Content();
	Require(content.ElementCount() == 2 && content.RenderedElementCount() == 2 && content.Plan().RenderOrder.size() == 2, "ruby annotation must not manufacture another base/reveal element");
	Require(content.Plan().RevealTimes == std::vector<float>{0, 3}, "base units retain independent reveal times despite a shared counted annotation");
	const auto& first = content.Lines().front().Elements.front();
	Require(first.Text == u"B" && first.Ruby.size() == 1 && first.Ruby[0].Text == u"B" && first.Ruby[0].Run.Glyphs.size() == 1, "counted ruby must own actual shaped annotation text/run");
	Require(first.Geometry.Metrics.Width == 8 && first.Ruby[0].Geometry.Metrics.Width == 4 && first.Ruby[0].Geometry.Position.X == 2 && first.Ruby[0].Geometry.Position.Y == -2, "normal8/ruby4 actual interfaces must independently size the same retained VectorFont source");
	fixture.Call("setShowCount", {std::int32_t{1}});
	fixture.Draw();
	Require(drawn.size() == 2 && drawn[0].SourcePixels.Right - drawn[0].SourcePixels.Left == 8 && drawn[1].SourcePixels.Right - drawn[1].SourcePixels.Left == 4, "one revealed base must draw its true normal8 and ruby4 glyphs");
	Require(drawn[1].DestinationPixels.Left == drawn[0].DestinationPixels.Left + 2, "actual ruby offset must reach rectangle receiving relative to its parent");
	const auto finds = fixture.FindCalls;
	drawn.clear();
	fixture.Call("setShowCount", {std::int32_t{2}});
	fixture.Draw();
	Require(drawn.size() == 3 && fixture.FindCalls == finds && backend.Observed.Creates == 1, "continued drawing must retain real ruby FontInfo and shared atlas without provider lookup/allocation");
	fixture.Call("clearFont");
	RequireFailure([&] {
		fixture.Draw();
	},
	    "actual prepared atlas");
	fixture.Call("done");
	drawn.clear();
	fixture.Draw();
	Require(drawn.size() == 3, "actual annotation runs must repopulate retired font caches without flattening text");
	fixture.Call("clear");
	Require(content.ElementCount() == 0 && content.Lines().empty(), "Clear must retire all independent ruby owners");
	fixture.Call("setFont", {fixture.Properties({{"shadow", true}, {"shadowdiff", std::int32_t{3}}})});
	fixture.Render("[BB]B");
	drawn.clear();
	fixture.Draw();
	Require(drawn.size() == 6 && drawn[0].ColorRgba == 0x000000ff && drawn[1].ColorRgba == 0x804020ff && drawn[2].ColorRgba == 0x000000ff && drawn[3].ColorRgba == 0x000000ff && drawn[4].ColorRgba == 0x804020ff && drawn[5].ColorRgba == 0x804020ff, "native order draws every base pass before each full annotation pass");
	fixture.Layers.ReleaseInstance(fixture.Layer);
	Require(fixture.Text.IsDetached(fixture.Instance), "containing Layer retirement must detach the real annotated Text task");
	fixture.Text.ReleaseInstance(fixture.Instance);
}

static void TestWholeRubyLigatureOwner() {
	text_atlas_test::Backend backend;
	sgre::scene::TextureCache cache([](auto, const auto&) -> sgre::render::SpriteTextureHandle {
		throw std::runtime_error("whole ruby fixture requires its actual vector provider");
	},
	    [](auto) {
	    });
	std::vector<sgre::text::VectorGlyphDrawRectangle> drawn;
	text_drawing_test::Fixture fixture(cache, backend, [&](auto&, auto rectangles, const auto&) {
		drawn.insert(drawn.end(), rectangles.begin(), rectangles.end());
	});
	fixture.FontBytes = vector_run_test::Font(true, false);
	fixture.Source = fixture.Construct(fixture.Vectors.VectorFontClass(), {std::string("whole-run-ligature.otf")});
	const std::array<sgre::script::SquirrelValue, 1> receiver{fixture.Source};
	Require(!std::get<bool>(fixture.Vm.Invoke(fixture.Vm.GetSlot(fixture.Source, std::string("getLoading")), receiver).Value), "actual ligature provider must complete before real font selection");
	fixture.Call("setFont", {fixture.Properties({{"face", std::string("whole-base")}, {"rubyface", std::string("whole-ruby")}})});
	fixture.Render("[AB]B");
	const auto& element = fixture.Text.Context(fixture.Instance).Content().Lines().front().Elements.front();
	Require(element.Ruby.size() == 1 && element.Ruby.front().Text == u"AB" && element.Ruby.front().Run.Glyphs.size() == 1 && element.Ruby.front().Run.Glyphs.front().GlyphIndex == 2, "actual VM/FontInfo must submit the entire annotation to ligature shaping once");
	Require(element.Ruby.front().Run.Advance.X == 4 && element.Ruby.front().Geometry.Position.X == 2, "annotation geometry must use the resulting one-glyph ruby4 advance rather than two unit advances");
	fixture.Draw();
	Require(drawn.size() == 2 && drawn[1].SourcePixels.Right - drawn[1].SourcePixels.Left == 4, "whole shaped annotation must reach actual retained atlas/draw receiving");
	fixture.Call("clear");
	Require(fixture.Text.Context(fixture.Instance).Content().ElementCount() == 0, "actual Clear must retire the whole annotation owner");
}

static void TestPendingRubyAndFailureOwners() {
	sgre::text::TextContentOwner content;
	sgre::text::TextFormattingState formatting;
	sgre::text::TextProcessingOptions options;
	sgre::text::TextContentCallbacks callbacks;
	std::vector<std::uint16_t> normal;
	callbacks.Fonts.SelectNormal = [] {
	};
	callbacks.Fonts.SelectRuby = [] {
	};
	callbacks.Fonts.MeasureNormal = [](auto) {
		return sgre::resource::GlyphSize{8, 8};
	};
	callbacks.PrepareNormal = [&](std::uint16_t unit) {
		normal.push_back(unit);
		sgre::text::OwnedTextGlyphRun run;
		run.Advance = {8, 0};
		return run;
	};
	callbacks.PrepareRuby = [](auto) {
		sgre::text::OwnedTextGlyphRun run;
		run.Advance = {4, 0};
		return run;
	};
	callbacks.MeasureRuby = [](auto) {
		return 4.0f;
	};
	sgre::text::ResetTextFontDefaults(formatting, callbacks.Fonts);
	sgre::text::ResetTextGeometryDefaults(formatting);
	Require(content.Render(Units(u"[xy,2]AB"), formatting, options, {}, {}, callbacks), "incomplete counted annotation must accept buffered input");
	content.Finalize(formatting, options, {});
	Require(normal.empty() && content.ElementCount() == 0 && content.RenderedElementCount() == 0, "Done must not fabricate an incomplete base group or shape its pending units");
	content.Clear();
	Require(content.Render(Units(u"[xy,2]A\nBCD"), formatting, options, {}, {}, callbacks), "real newline must retire pending annotation/base state");
	content.Finalize(formatting, options, {});
	Require(normal == std::vector<std::uint16_t>{u'B', u'C', u'D'} && content.ElementCount() == 3, "newline must not leak discarded pending text into its following line");
	content.Clear();
	options.IgnoreRuby = true;
	callbacks.PrepareRuby = {};
	callbacks.MeasureRuby = {};
	Require(content.Render(Units(u"[xy,2]AB"), formatting, options, {}, {}, callbacks), "IgnoreRuby must consume annotation operands without requiring an unused provider");
	Require(content.CurrentLine().size() == 2 && content.CurrentLine().front().Ruby.empty(), "ignored annotation must leave real ordinary base ownership intact");
	content.Clear();
	options.IgnoreRuby = false;
	RequireFailure([&] {
		static_cast<void>(content.Render(Units(u"[xy]AB"), formatting, options, {}, {}, callbacks));
	},
	    "actual retained ruby provider");
	Require(content.ElementCount() == 0, "missing ruby producer must reject before content publication");
	callbacks.MeasureRuby = [](auto) {
		return 4.0f;
	};
	callbacks.PrepareRuby = [&](auto) -> sgre::text::OwnedTextGlyphRun {
		content.Clear();
		return {};
	};
	RequireFailure([&] {
		static_cast<void>(content.Render(Units(u"[xy]A"), formatting, options, {}, {}, callbacks));
	},
	    "cannot clear");
	content.Clear();
}

static std::uint32_t ReadWord() {
	std::array<char, 4> bytes{};
	std::cin.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
	Require(bool(std::cin), "native ruby input word extent");
	std::uint32_t result = 0;
	for (std::size_t byte = 0; byte < bytes.size(); ++byte) {
		result |= std::uint32_t(static_cast<unsigned char>(bytes[byte])) << (byte * 8);
	}
	return result;
}

static void NativeWord(std::uint32_t actual, std::size_t& ordinal) {
	const auto expected = ReadWord();
	Require(actual == expected, "native ruby word " + std::to_string(ordinal) + " expected " + std::to_string(expected) + " actual " + std::to_string(actual));
	++ordinal;
}

static void NativeText(std::u16string_view text, std::size_t& ordinal) {
	NativeWord(static_cast<std::uint32_t>(text.size()), ordinal);
	for (const auto unit : text) {
		NativeWord(static_cast<std::uint32_t>(unit), ordinal);
	}
}

static void NativeRun(const sgre::text::OwnedTextGlyphRun& run, std::size_t& ordinal) {
	NativeWord(std::bit_cast<std::uint32_t>(run.Advance.X), ordinal);
	NativeWord(std::bit_cast<std::uint32_t>(run.Advance.Y), ordinal);
	NativeWord(static_cast<std::uint32_t>(run.Glyphs.size()), ordinal);
	for (const auto& glyph : run.Glyphs) {
		NativeWord(glyph.GlyphIndex, ordinal);
		NativeWord(std::bit_cast<std::uint32_t>(glyph.Position.X), ordinal);
		NativeWord(std::bit_cast<std::uint32_t>(glyph.Position.Y), ordinal);
	}
}

static void NativeGeometry(const sgre::text::TextGlyphGeometry& geometry, std::size_t& ordinal) {
	for (const auto value : {geometry.Position.X, geometry.Position.Y, geometry.Metrics.Width, geometry.Metrics.Height}) {
		NativeWord(std::bit_cast<std::uint32_t>(value), ordinal);
	}
}

static void NativeLine(const std::deque<sgre::text::TextContentElement>& line, std::size_t& ordinal) {
	NativeWord(static_cast<std::uint32_t>(line.size()), ordinal);
	for (const auto& element : line) {
		NativeText(element.Text, ordinal);
		NativeRun(element.Run, ordinal);
		NativeGeometry(element.Geometry, ordinal);
		const auto& style = element.Style;
		for (const auto value : std::array<std::uint32_t, 9>{style.MainColor, static_cast<std::uint32_t>(style.Bold), static_cast<std::uint32_t>(style.Italic), static_cast<std::uint32_t>(style.ShadowEnabled), style.ShadowColor, std::bit_cast<std::uint32_t>(style.ShadowOffset), static_cast<std::uint32_t>(style.EdgeEnabled), style.EdgeColor, std::bit_cast<std::uint32_t>(style.EdgeWidth)}) {
			NativeWord(value, ordinal);
		}
		NativeWord(element.RubyFont.FaceIndex, ordinal);
		NativeWord(std::bit_cast<std::uint32_t>(element.RevealTime), ordinal);
		NativeWord(static_cast<std::uint32_t>(element.Ruby.size()), ordinal);
		for (const auto& ruby : element.Ruby) {
			NativeText(ruby.Text, ordinal);
			NativeRun(ruby.Run, ordinal);
			NativeGeometry(ruby.Geometry, ordinal);
		}
	}
}

static void NativeSnapshot(const sgre::text::TextContentOwner& content, std::size_t& ordinal) {
	NativeWord(content.RenderedElementCount(), ordinal);
	NativeWord(static_cast<std::uint32_t>(content.ElementCount()), ordinal);
	for (const auto value : {content.Cursor().X, content.Cursor().Y, content.RenderCursor().X, content.RenderCursor().Y, content.Bounds().Left, content.Bounds().Top, content.Bounds().Right, content.Bounds().Bottom}) {
		NativeWord(std::bit_cast<std::uint32_t>(value), ordinal);
	}
	NativeText(content.ProcessedText(), ordinal);
	NativeLine(content.CurrentLine(), ordinal);
	NativeWord(static_cast<std::uint32_t>(content.Lines().size()), ordinal);
	for (const auto& line : content.Lines()) {
		NativeLine(line.Elements, ordinal);
	}
}

static sgre::text::OwnedTextGlyphRun PrepareUnit(sgre::text::VectorFont& font, std::uint16_t unit) {
	static_cast<void>(font.MeasureCodeUnit(unit));
	const auto glyphs = font.InputBuffer().InputGlyphs();
	Require(glyphs.size() == 1, "native one-unit actual CFF glyph count");
	const auto row = font.InputBuffer().PositionRow(0);
	sgre::text::ShapedTextGlyph shaped;
	shaped.GlyphIndex = glyphs.front().GlyphIndex;
	shaped.XAdvance26Dot6 = row.XAdvance26Dot6;
	shaped.YAdvance26Dot6 = row.YAdvance26Dot6;
	shaped.XOffset26Dot6 = row.XOffset26Dot6;
	shaped.YOffset26Dot6 = row.YOffset26Dot6;
	sgre::text::OwnedTextGlyphRun result;
	result.Glyphs.resize(1);
	result.Advance = sgre::text::PrepareTextGlyphRun(std::span<const sgre::text::ShapedTextGlyph>(&shaped, 1), result.Glyphs);
	return result;
}

static void TestWholeNativeRuby() {
	const auto length = ReadWord();
	Require(length != 0 && length <= 64 * 1024 * 1024, "native ruby real font extent");
	auto bytes = std::make_shared<std::vector<std::uint8_t>>(length);
	for (auto& byte : *bytes) {
		const auto value = std::cin.get();
		Require(value != std::char_traits<char>::eof(), "native ruby real font bytes");
		byte = static_cast<std::uint8_t>(static_cast<unsigned char>(value));
	}
	sgre::text::VectorFont normal(bytes);
	sgre::text::VectorFont ruby(bytes);
	normal.SetSize(24);
	ruby.SetSize(10);
	sgre::text::TextFormattingState formatting;
	sgre::text::TextProcessingOptions options;
	sgre::text::TextContentOwner content;
	sgre::text::TextContentCallbacks callbacks;
	callbacks.Fonts.SelectNormal = [] {
	};
	callbacks.Fonts.SelectRuby = [] {
	};
	callbacks.Fonts.MeasureNormal = [&](auto unit) {
		return normal.MeasureCodeUnit(unit);
	};
	callbacks.PrepareNormal = [&](auto unit) {
		return PrepareUnit(normal, unit);
	};
	callbacks.PrepareRuby = [&](std::u16string_view text) {
		return ruby.PrepareRun(text);
	};
	callbacks.MeasureRuby = [&](auto unit) {
		return ruby.MeasureCodeUnit(unit).Width;
	};
	sgre::text::ResetTextFontDefaults(formatting, callbacks.Fonts);
	sgre::text::ResetTextGeometryDefaults(formatting);
	const auto count = ReadWord();
	Require(count <= 8192, "native ruby bounded source count");
	std::vector<std::uint16_t> input;
	for (std::uint32_t index = 0; index < count; ++index) {
		const auto unit = ReadWord();
		Require(unit <= 65535, "native ruby UTF16 input range");
		input.push_back(static_cast<std::uint16_t>(unit));
	}
	const auto width = std::bit_cast<float>(ReadWord());
	formatting.Current.RubyOffset = std::bit_cast<float>(ReadWord());
	formatting.Current.RubyFaceIndex = ReadWord();
	formatting.Defaults.WordBreak = ReadWord() != 0;
	formatting.Current.WordBreak = ReadWord() != 0;
	options.IgnoreRuby = ReadWord() != 0;
	auto& current = formatting.Current;
	current.Color = ReadWord();
	current.Bold = ReadWord() != 0;
	current.Italic = ReadWord() != 0;
	current.Shadow = ReadWord() != 0;
	current.ShadowColor = ReadWord();
	current.ShadowOffset = std::bit_cast<std::int32_t>(ReadWord());
	current.Edge = ReadWord() != 0;
	current.EdgeColor = ReadWord();
	current.EdgeSize = std::bit_cast<std::int32_t>(ReadWord());
	sgre::text::TextContentRequest request;
	request.ElementDelay = 3;
	std::size_t ordinal = 0;
	NativeWord(static_cast<std::uint32_t>(content.Render(input, formatting, options, {width, 0}, request, callbacks)), ordinal);
	NativeSnapshot(content, ordinal);
	content.Finalize(formatting, options, {width, 0});
	NativeSnapshot(content, ordinal);
	content.Clear();
	NativeSnapshot(content, ordinal);
	Require(std::cin.peek() == std::char_traits<char>::eof(), "native ruby comparison consumed its exact stream");
	std::cout << "PASS " << ordinal << " original ruby owned-content words\n";
}

int main(int argc, char** argv) {
	try {
		if (argc == 2 && std::string_view(argv[1]) == "--native") {
			TestWholeNativeRuby();
			return 0;
		}
		TestOwnedRubyProviderDrawAndReveal();
		TestPendingRubyAndFailureOwners();
		TestWholeRubyLigatureOwner();
		std::cout << "PASS actual owned ruby content/font/reveal/draw lifecycle\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
