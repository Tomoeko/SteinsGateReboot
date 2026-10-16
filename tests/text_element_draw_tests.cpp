#include "sgre/text/element_draw.hpp"

#include <array>
#include <bit>
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
		Require(std::string_view(error.what()).find("text") != std::string_view::npos, "text draw failure lost its context");

		return;
	}

	throw std::runtime_error("invalid text draw geometry was accepted");
}

struct NativeTextDrawFixture {
	std::array<std::uint32_t, 13> Input{};
	std::uint32_t FontStyle = 0;
	std::size_t PassCount = 0;
	std::array<std::array<std::uint32_t, 4>, 3> Passes{};
};

static sgre::text::TextElementDrawPlan ReadFixturePlan(const NativeTextDrawFixture& fixture) {
	const auto& input = fixture.Input;
	const sgre::text::TextCursor origin{std::bit_cast<float>(input[0]), std::bit_cast<float>(input[1])};
	const sgre::text::TextCursor position{std::bit_cast<float>(input[2]), std::bit_cast<float>(input[3])};
	sgre::text::TextElementDrawStyle style;
	style.MainColor = input[4];
	style.ShadowColor = input[5];
	style.EdgeColor = input[6];
	style.Bold = input[7] != 0;
	style.Italic = input[8] != 0;
	style.ShadowEnabled = input[9] != 0;
	style.EdgeEnabled = input[10] != 0;
	style.ShadowOffset = std::bit_cast<std::int32_t>(input[11]);
	style.EdgeWidth = std::bit_cast<std::int32_t>(input[12]);

	return sgre::text::PrepareTextElementDrawPlan(origin, position, style);
}

static void RequirePass(const sgre::text::TextGlyphDrawPass& pass, const std::array<std::uint32_t, 4>& expected) {
	const std::array<std::uint32_t, 4> actual{std::bit_cast<std::uint32_t>(pass.Position.X), std::bit_cast<std::uint32_t>(pass.Position.Y), pass.ColorRgba, static_cast<std::uint32_t>(pass.EdgeWidth)};
	Require(actual == expected, "text draw values differ from original caller instructions");
}

static void TestRetainedOriginalDrawCaller() {
	// Original element caller and ruby traversal executed against supplied logging font interfaces.
	const std::array<NativeTextDrawFixture, 3> fixtures = {{
	    {{0x41200000, 0x41a00000, 0x40400000, 0x40800000, 0xed06481e, 0x52f4cd66, 0x51cdd9f5, 1, 1, 1, 1, 2, 3}, 1, 3, {{{0x41500000, 0x41c00000, 0xf5d9cd51, 3}, {0x41700000, 0x41d00000, 0x66cdf452, 0}, {0x41500000, 0x41c00000, 0x1e4806ed, 0}}}},
	    {{0xc5918d96, 0x45828450, 0x4617e831, 0xc58c7c82, 0xe208b1f3, 0xac750b66, 0xf0eb4d4b, 0, 0, 1, 0, 0x00697337, 5}, 0, 2, {{{0x4ad30dff, 0x4ad2e3f0, 0x660b75ac, 0}, {0x459e42cc, 0xc39f8320, 0xf3b108e2, 0}, {0, 0, 0, 0}}}},
	    {{0x4596ee6a, 0xc60968d5, 0x452020a4, 0xc35af671, 0x62c74a76, 0x5af99dfe, 0xd9d98ec3, 0, 1, 1, 0, 0x00aa657d, 2}, 2, 2, {{{0x4b2a825d, 0x4b2a4248, 0xfe9df95a, 0}, {0x45e6febc, 0xc60cd4af, 0x764ac762, 0}, {0, 0, 0, 0}}}},
	}};
	for (const auto& fixture : fixtures) {
		const auto plan = ReadFixturePlan(fixture);
		Require(static_cast<std::uint32_t>(plan.FontStyle) == fixture.FontStyle, "text font style differs from original selection");
		Require(plan.PassCount == fixture.PassCount, "text draw pass count differs from original caller");
		for (std::size_t index = 0; index < plan.PassCount; ++index) {
			RequirePass(plan.Passes[index], fixture.Passes[index]);
		}
	}

	const auto plan = ReadFixturePlan(fixtures[0]);
	Require(plan.Passes[0].Kind == sgre::text::TextGlyphPassKind::Edge && plan.Passes[1].Kind == sgre::text::TextGlyphPassKind::Shadow && plan.Passes[2].Kind == sgre::text::TextGlyphPassKind::Main, "text draw ordering changed");
	const std::array<sgre::text::TextCursor, 2> ruby_offsets = {{{std::bit_cast<float>(0x419a8fa6u), std::bit_cast<float>(0x422d694du)}, {std::bit_cast<float>(0x428c5074u), std::bit_cast<float>(0x42b2d716u)}}};
	const std::array<std::array<std::uint32_t, 4>, 6> ruby_draws = {{{0x420147d3, 0x4286b4a6, 0xf5d9cd51, 3}, {0x42a65074, 0x42e2d716, 0xf5d9cd51, 3}, {0x420947d3, 0x428ab4a6, 0x66cdf452, 0}, {0x42aa5074, 0x42e6d716, 0x66cdf452, 0}, {0x420147d3, 0x4286b4a6, 0x1e4806ed, 0}, {0x42a65074, 0x42e2d716, 0x1e4806ed, 0}}};
	std::size_t draw_index = 0;
	for (std::size_t pass_index = 0; pass_index < plan.PassCount; ++pass_index) {
		for (const auto& ruby_offset : ruby_offsets) {
			const auto ruby_pass = sgre::text::PrepareTextRubyDrawPass(plan.Passes[pass_index], ruby_offset);
			Require(ruby_pass.Kind == plan.Passes[pass_index].Kind, "ruby draw changed its pass identity");
			RequirePass(ruby_pass, ruby_draws[draw_index++]);
		}
	}
}

static void TestEnabledEffectsAndSignedOffset() {
	sgre::text::TextElementDrawStyle style;
	style.MainColor = 0x04030201;
	style.ShadowColor = 0x08070605;
	style.EdgeColor = 0x0c0b0a09;
	style.ShadowEnabled = true;
	style.ShadowOffset = -3;
	style.EdgeEnabled = true;
	style.EdgeWidth = -1;
	const sgre::text::TextCursor origin{10, 20};
	const sgre::text::TextCursor position{2, 4};
	const auto plan = sgre::text::PrepareTextElementDrawPlan(origin, position, style);
	Require(plan.PassCount == 2 && plan.Passes[0].Kind == sgre::text::TextGlyphPassKind::Shadow, "nonpositive edge width did not omit the edge draw");
	Require(plan.Passes[0].Position.X == 9 && plan.Passes[0].Position.Y == 21, "signed shadow offset was not applied to both coordinates");
	Require(plan.Passes[0].ColorRgba == 0x05060708 && plan.Passes[1].ColorRgba == 0x01020304, "stored text colors were not byte-swapped");
	Require(plan.Passes[1].Position.X == 12 && plan.Passes[1].Position.Y == 24, "main text draw was displaced by an effect");
}

static void TestInvalidGeometry() {
	sgre::text::TextElementDrawStyle style;
	const sgre::text::TextCursor zero;
	const sgre::text::TextCursor invalid{std::numeric_limits<float>::infinity(), 0};
	const sgre::text::TextCursor maximum{std::numeric_limits<float>::max(), 0};
	RequireFailure([&] {
		static_cast<void>(sgre::text::PrepareTextElementDrawPlan(zero, invalid, style));
	});
	RequireFailure([&] {
		static_cast<void>(sgre::text::PrepareTextElementDrawPlan(maximum, maximum, style));
	});
	sgre::text::TextGlyphDrawPass pass;
	pass.Position = maximum;
	RequireFailure([&] {
		static_cast<void>(sgre::text::PrepareTextRubyDrawPass(pass, maximum));
	});
	pass.Position = zero;
	pass.EdgeWidth = -1;
	RequireFailure([&] {
		static_cast<void>(sgre::text::PrepareTextRubyDrawPass(pass, zero));
	});
}

int main() {
	try {
		TestRetainedOriginalDrawCaller();
		TestEnabledEffectsAndSignedOffset();
		TestInvalidGeometry();
		std::cout << "Text element draw tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << "Text element draw test failure: " << error.what() << '\n';

		return 1;
	}
}
