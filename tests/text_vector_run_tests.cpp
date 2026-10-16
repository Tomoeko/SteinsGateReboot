#include "sgre/resource/opentype_layout.hpp"
#include "sgre/text/recursive_decomposition.hpp"
#include "sgre/text/vector_font.hpp"

#include "vector_run_fixture.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <map>
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
	throw std::runtime_error("whole-run fixture expected failure: " + std::string(expected));
}

static void TestWholeLigatureAndOwnedPublication() {
	sgre::text::VectorFont font(vector_run_test::Font(true, false));
	font.SetSize(10);
	const auto run = font.PrepareRun(u"ABAB");
	Require(run.Glyphs.size() == 2 && run.Glyphs[0].GlyphIndex == 2 && run.Glyphs[1].GlyphIndex == 2 && run.Advance.X == 20 && run.Glyphs[1].Position.X == 10, "whole buffer must apply two real ligatures before publishing advances");
	font.SetSize(24);
	const auto later = font.PrepareRun(u"A");
	Require(later.Glyphs.size() == 1 && later.Glyphs.front().GlyphIndex == 1 && later.Advance.X == 24 && run.Advance.X == 20, "actual size mutation and buffer reuse must not mutate a retained owned run");
	Require(font.PrepareRun(u"").Glyphs.empty() && font.InputBuffer().InputGlyphs().empty(), "empty native shaping must clear live input without inventing glyphs");
	const auto source = vector_run_test::Font(true, false);
	const sgre::resource::SfntFont sfnt(*source);
	const sgre::resource::OpenTypeLayoutFont layout(sfnt);
	std::size_t work = 0;
	const std::array<std::uint32_t, 2> ab{1, 2};
	const auto match = layout.MatchLigatureSubstitution(0, ab, work);
	Require(match && match->ComponentCount == 2 && match->GlyphIndex == 2, "ligature reader must retain actual component count and replacement");
	work = 1024 * 1024;
	RequireFailure([&] {
		static_cast<void>(layout.MatchLigatureSubstitution(0, ab, work));
	},
	    "record count");
	sgre::text::VectorFont malformed(vector_run_test::Font(true, false, true));
	malformed.SetSize(10);
	RequireFailure([&] {
		static_cast<void>(malformed.PrepareRun(u"AB"));
	},
	    "replacement");
	sgre::text::VectorFont filtered(vector_run_test::Font(true, false, false, 2));
	filtered.SetSize(10);
	RequireFailure([&] {
		static_cast<void>(filtered.PrepareRun(u"AB"));
	},
	    "covered substitution");
}

static void TestActualZeroLayoutScaleAndGates() {
	const auto bytes = vector_run_test::Font(false, true);
	const sgre::resource::SfntFont sfnt(*bytes);
	const sgre::resource::OpenTypeLayoutFont layout(sfnt, sgre::resource::OpenTypeLayoutKind::Positioning);
	std::size_t work = 0;
	const auto pair = layout.MatchPairPositioning(0, 1, 2, work);
	Require(pair && pair->First[2] == -70 && !pair->HasSecondValue, "pair reader must decode actual signed design values before zero-scale projection");
	sgre::text::VectorFont font(bytes);
	font.SetSize(10);
	const auto first = font.PrepareRun(u"AB");
	font.SetSize(24);
	const auto second = font.PrepareRun(u"AB");
	Require(first.Advance.X == 20 && first.Glyphs[1].Position.X == 10 && second.Advance.X == 48 && second.Glyphs[1].Position.X == 24, "FT-size advances change while fresh native HB layout scales remain zero");
	sgre::text::VectorFont explicit_scales(bytes);
	explicit_scales.SetSize(10);
	explicit_scales.BindMetricServices(sgre::text::VectorFontMetricScaleSign::Nonnegative, sgre::text::VectorFontMetricScaleSign::Nonnegative);
	RequireFailure([&] {
		static_cast<void>(explicit_scales.PrepareRun(u"AB"));
	},
	    "zero HB scale provenance");
	RequireFailure([&] {
		static_cast<void>(font.PrepareRun(u"A\u0307"));
	},
	    "retained mark");
	RequireFailure([&] {
		static_cast<void>(font.PrepareRun(std::u16string(4097, u'A')));
	},
	    "bounded");
}

static std::uint32_t ReadWord() {
	std::array<char, 4> bytes{};
	std::cin.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
	Require(bool(std::cin), "whole-run native word extent");
	std::uint32_t result = 0;
	for (std::size_t index = 0; index < bytes.size(); ++index) {
		result |= std::uint32_t(static_cast<unsigned char>(bytes[index])) << (index * 8);
	}
	return result;
}

static void TestNativeWholeRun() {
	const auto length = ReadWord();
	Require(length > 0 && length <= 64 * 1024 * 1024, "whole-run native font extent");
	auto bytes = std::make_shared<std::vector<std::uint8_t>>(length);
	for (auto& byte : *bytes) {
		const auto value = std::cin.get();
		Require(value != std::char_traits<char>::eof(), "whole-run native font bytes");
		byte = static_cast<std::uint8_t>(static_cast<unsigned char>(value));
	}
	const auto language_word = ReadWord();
	Require(language_word <= 1, "whole-run native language profile");
	sgre::text::VectorFont font(bytes, 0, language_word == 0 ? "en" : "ja");
	const auto cases = ReadWord();
	Require(cases <= 1024, "whole-run native bounded case count");
	std::size_t words = 0;
	for (std::uint32_t index = 0; index < cases; ++index) {
		const auto size = ReadWord();
		const auto count = ReadWord();
		Require(size > 0 && size <= 2000 && count <= 256, "whole-run native bounded input");
		std::u16string input;
		for (std::uint32_t unit = 0; unit < count; ++unit) {
			const auto value = ReadWord();
			Require(value <= 0xffff, "whole-run native UTF16 unit");
			input.push_back(static_cast<char16_t>(value));
		}
		font.SetSize(static_cast<float>(size));
		const auto run = font.PrepareRun(input);
		auto compare = [&](std::uint32_t actual) {
			const auto expected = ReadWord();
			Require(actual == expected, "native whole-run case " + std::to_string(index) + " word " + std::to_string(words) + " expected " + std::to_string(expected) + " actual " + std::to_string(actual));
			++words;
		};
		compare(static_cast<std::uint32_t>(run.Glyphs.size()));
		for (const auto& glyph : run.Glyphs) {
			compare(glyph.GlyphIndex);
			compare(std::bit_cast<std::uint32_t>(glyph.Position.X));
			compare(std::bit_cast<std::uint32_t>(glyph.Position.Y));
		}
		compare(std::bit_cast<std::uint32_t>(run.Advance.X));
		compare(std::bit_cast<std::uint32_t>(run.Advance.Y));
	}
	Require(std::cin.peek() == std::char_traits<char>::eof(), "whole-run native comparison consumed exact stream");
	std::cout << "PASS " << cases << " actual whole-run shapes / " << words << " original output words\n";
}

int main(int argc, char** argv) {
	try {
		if (argc == 2 && std::string_view(argv[1]) == "--native") {
			TestNativeWholeRun();
			return 0;
		}
		TestWholeLigatureAndOwnedPublication();
		TestActualZeroLayoutScaleAndGates();
		std::cout << "PASS whole-input neutral normalization/ligature/pair/run ownership\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
