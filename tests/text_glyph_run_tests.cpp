#include "sgre/text/glyph_run.hpp"

#include <array>
#include <bit>
#include <functional>
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

static void RequireFailure(const std::function<void()>& operation, std::string_view context) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, "glyph run failure lost its context");

		return;
	}

	throw std::runtime_error("invalid glyph run was accepted");
}

struct NativeGlyphRunFixture {
	std::size_t Count = 0;
	std::array<sgre::text::ShapedTextGlyph, 3> Shaped{};
	std::array<std::uint32_t, 2> Advance{};
	std::array<std::array<std::uint32_t, 3>, 3> Glyphs{};
};

static void TestRetainedOriginalGlyphRuns() {
	// The original post-shaping loop emitted these independently retained glyph/float words.
	const std::array<NativeGlyphRunFixture, 3> fixtures = {{
	    {1, {{{3225825765u, 84886, -55802, 87699, -18010}, {}, {}}}, {0x44a5cb00, 0xc459fa00}, {{{0xc04631e5, 0x44ab4980, 0xc38cb400}, {}, {}}}},
	    {2, {{{2910707936u, 760755003, -1641609738, -538134670, -1106663282}, {400928029u, -1009316893, 1218001815, -760239150, 399637369}, {}}}, {0xca6d0c0c, 0xcac9fdf4}, {{{0xad7de0e0, 0xcb004d22, 0xcb83ecaf}, {0x17e5ad1d, 0x45fbe000, 0xcb940dfd}, {}}}},
	    {3, {{{2838283645u, -80708, -62379, -48619, 60639}, {492182478u, 22585, 54609, -26613, 94258}, {2301812016u, 70722, -24613, -83940, 34791}}}, {0x4344dc00, 0xc3fcfe00}, {{{0xa92cc57d, 0xc43deb00, 0x446cdf00}, {0x1d561bce, 0xc4d19c80, 0x43f90e00}, {0x8932dd30, 0xc50abbc0, 0x43d31a00}}}},
	}};
	for (const auto& fixture : fixtures) {
		std::array<sgre::text::TextRunGlyph, 3> output{};
		const auto advance = sgre::text::PrepareTextGlyphRun(std::span(fixture.Shaped).first(fixture.Count), output);
		Require(std::bit_cast<std::uint32_t>(advance.X) == fixture.Advance[0] && std::bit_cast<std::uint32_t>(advance.Y) == fixture.Advance[1], "glyph run advance differs from original float accumulation");
		for (std::size_t index = 0; index < fixture.Count; ++index) {
			const auto& glyph = output[index];
			const std::array<std::uint32_t, 3> words{glyph.GlyphIndex, std::bit_cast<std::uint32_t>(glyph.Position.X), std::bit_cast<std::uint32_t>(glyph.Position.Y)};
			Require(words == fixture.Glyphs[index], "glyph run position differs from original instructions");
		}
	}
}

static void TestOffsetsAndSignedAdvance() {
	const std::array<sgre::text::ShapedTextGlyph, 3> shaped = {{{7, 640, -128, 64, 32}, {8, 0, 0, -128, 64}, {9, 320, -64, 0, 0}}};
	std::array<sgre::text::TextRunGlyph, 4> output{};
	output.back().GlyphIndex = 999;
	const auto advance = sgre::text::PrepareTextGlyphRun(shaped, output);
	Require(advance.X == 15 && advance.Y == -3, "glyph offsets changed the signed run advance");
	Require(output[0].Position.X == 1 && output[0].Position.Y == 0.5f, "first glyph offset was not preserved");
	Require(output[1].Position.X == 8 && output[1].Position.Y == -1, "mark offset did not use the prior pen position");
	Require(output[2].Position.X == 10 && output[2].Position.Y == -2, "zero-advance mark displaced the next glyph");
	Require(output.back().GlyphIndex == 999, "glyph run modified unused output capacity");
	const auto empty_advance = sgre::text::PrepareTextGlyphRun({}, output);
	Require(empty_advance.X == 0 && empty_advance.Y == 0 && output.back().GlyphIndex == 999, "empty glyph run did not preserve output storage");
}

static void TestPreparationLimits() {
	const std::array<sgre::text::ShapedTextGlyph, 2> shaped{};
	std::array<sgre::text::TextRunGlyph, 1> output{};
	output[0].GlyphIndex = 77;
	RequireFailure([&] {
		static_cast<void>(sgre::text::PrepareTextGlyphRun(shaped, output));
	},
	    "shorter");
	Require(output[0].GlyphIndex == 77, "short glyph run output was partially modified");
	const std::vector<sgre::text::ShapedTextGlyph> oversized(1024 * 1024 + 1);
	RequireFailure([&] {
		static_cast<void>(sgre::text::PrepareTextGlyphRun(oversized, output));
	},
	    "glyph-count");
	Require(output[0].GlyphIndex == 77, "oversized glyph run partially modified output");
}

int main() {
	try {
		TestRetainedOriginalGlyphRuns();
		TestOffsetsAndSignedAdvance();
		TestPreparationLimits();
		std::cout << "Text glyph run tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << "Text glyph run test failure: " << error.what() << '\n';

		return 1;
	}
}
