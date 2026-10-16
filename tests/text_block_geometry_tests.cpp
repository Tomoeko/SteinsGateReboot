#include "sgre/text/block_geometry.hpp"

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
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, "text block failure lost its context");

		return;
	}

	throw std::runtime_error("invalid text block geometry was accepted");
}

static std::array<std::uint32_t, 4> BoundsWords(const sgre::text::TextBounds& bounds) {
	return {std::bit_cast<std::uint32_t>(bounds.Left), std::bit_cast<std::uint32_t>(bounds.Top), std::bit_cast<std::uint32_t>(bounds.Right), std::bit_cast<std::uint32_t>(bounds.Bottom)};
}

static void RequirePosition(const sgre::text::TextCursor& position, std::uint32_t x, std::uint32_t y) {
	Require(std::bit_cast<std::uint32_t>(position.X) == x && std::bit_cast<std::uint32_t>(position.Y) == y, "text block position differs from original words");
}

static void TestRetainedHorizontalBlock() {
	// These bounds and positions were retained from the original numeric phase, before list ownership.
	const std::array<sgre::text::TextGlyphGeometry, 1> first_ruby = {{{{-4, -3}, {6, 4}}}};
	const std::array<sgre::text::TextGlyphGeometry, 1> second_ruby = {{{{1, -8}, {8, 5}}}};
	std::array<sgre::text::TextBlockElement, 2> first_elements{};
	first_elements[0].Geometry = {{10, 20}, {12, 8}};
	first_elements[0].Ruby = first_ruby;
	first_elements[1].Geometry = {{30, 20}, {5, 10}};
	std::array<sgre::text::TextBlockElement, 1> second_elements{};
	second_elements[0].Geometry = {{5, 40}, {9, 12}};
	second_elements[0].Ruby = second_ruby;
	std::array<sgre::text::TextBlockLine, 2> lines{};
	lines[0].Elements = first_elements;
	lines[1].Elements = second_elements;
	sgre::text::TextBlockGeometrySettings settings;
	settings.Alignment = sgre::text::TextAlignment::Center;
	settings.RenderWidth = 200;
	settings.RenderHeight = 100;
	const auto block = sgre::text::FinalizeTextBlockGeometry(settings, lines);
	Require(BoundsWords(block.Bounds) == std::array<std::uint32_t, 4>{0xc0800000, 0x41800000, 0x420c0000, 0x42980000}, "horizontal block bounds differ from original");
	Require(std::bit_cast<std::uint32_t>(block.CrossAlignmentOffset) == 0x41c00000, "horizontal block alignment differs from original");
	Require(BoundsWords(lines[0].Bounds) == std::array<std::uint32_t, 4>{0xc0800000, 0xc0400000, 0x420c0000, 0x41f00000}, "first line bounds differ from original");
	Require(BoundsWords(lines[1].Bounds) == std::array<std::uint32_t, 4>{0, 0xc1000000, 0x41600000, 0x42500000}, "second line bounds differ from original");
	RequirePosition(first_elements[0].Geometry.Position, 0x41200000, 0x42300000);
	RequirePosition(first_elements[1].Geometry.Position, 0x41f00000, 0x42300000);
	RequirePosition(second_elements[0].Geometry.Position, 0x40a00000, 0x42800000);
	RequirePosition(first_ruby[0].Position, 0xc0800000, 0xc0400000);
	RequirePosition(second_ruby[0].Position, 0x3f800000, 0xc1000000);
}

static void TestRetainedVerticalBlock() {
	const std::array<sgre::text::TextGlyphGeometry, 1> ruby = {{{{3, -4}, {8, 4}}}};
	std::array<sgre::text::TextBlockElement, 2> elements{};
	elements[0].Geometry = {{180, 10}, {20, 30}};
	elements[0].Ruby = ruby;
	elements[1].Geometry = {{140, 30}, {10, 12}};
	std::array<sgre::text::TextBlockLine, 1> lines{};
	lines[0].Bounds = {200, 0, 200, 0};
	lines[0].Elements = elements;
	sgre::text::TextBlockGeometrySettings settings;
	settings.Direction = sgre::text::TextDirection::Vertical;
	settings.Alignment = sgre::text::TextAlignment::End;
	settings.RenderWidth = 200;
	settings.RenderHeight = 100;
	const auto block = sgre::text::FinalizeTextBlockGeometry(settings, lines);
	Require(BoundsWords(block.Bounds) == std::array<std::uint32_t, 4>{0xc3420000, 0xc0800000, 0x40400000, 0x42280000}, "vertical block bounds differ from original");
	Require(std::bit_cast<std::uint32_t>(block.CrossAlignmentOffset) == 0xc3450000, "vertical block end alignment differs from original");
	Require(BoundsWords(lines[0].Bounds) == std::array<std::uint32_t, 4>{0x40400000, 0xc0800000, 0x43480000, 0x42280000}, "vertical line bounds were shifted by block alignment");
	RequirePosition(elements[0].Geometry.Position, 0xc1880000, 0x41200000);
	RequirePosition(elements[1].Geometry.Position, 0xc2640000, 0x41f00000);
	RequirePosition(ruby[0].Position, 0x40400000, 0xc0800000);
}

static void TestEmptyAndSignedZero() {
	sgre::text::TextBlockGeometrySettings settings;
	settings.Alignment = sgre::text::TextAlignment::Center;
	settings.RenderHeight = 100;
	const auto empty = sgre::text::FinalizeTextBlockGeometry(settings, {}, {-2, 3, 20, 40});
	Require(empty.Bounds.Left == -2 && empty.Bounds.Top == 33 && empty.Bounds.Right == 20 && empty.Bounds.Bottom == 70 && empty.CrossAlignmentOffset == 30, "empty history did not align retained block bounds");
	settings.Alignment = sgre::text::TextAlignment::Start;
	std::array<sgre::text::TextBlockElement, 1> elements{};
	elements[0].Geometry.Position = {-0.0f, -0.0f};
	std::array<sgre::text::TextBlockLine, 1> lines{};
	lines[0].Bounds = {-0.0f, -0.0f, -0.0f, -0.0f};
	lines[0].Elements = elements;
	const auto zero = sgre::text::FinalizeTextBlockGeometry(settings, lines);
	Require(BoundsWords(zero.Bounds) == std::array<std::uint32_t, 4>{0x80000000, 0, 0x80000000, 0}, "zero block offset lost original global signed-zero addition");
	Require(BoundsWords(lines[0].Bounds) == std::array<std::uint32_t, 4>{0x80000000, 0x80000000, 0x80000000, 0x80000000}, "equal signed-zero geometry changed line bounds");
	RequirePosition(elements[0].Geometry.Position, 0x80000000, 0x80000000);
}

static void TestFractionalAlignmentAndInitialBounds() {
	std::array<sgre::text::TextBlockElement, 1> elements{};
	elements[0].Geometry = {{10, 20}, {-5, -10}};
	std::array<sgre::text::TextBlockLine, 1> lines{};
	lines[0].Bounds = {9, 19, 11, 21};
	lines[0].Elements = elements;
	sgre::text::TextBlockGeometrySettings settings;
	settings.Alignment = sgre::text::TextAlignment::Center;
	settings.RenderHeight = 10.5f;
	const auto block = sgre::text::FinalizeTextBlockGeometry(settings, lines, {-100, -100, 100, 100});
	Require(block.CrossAlignmentOffset == -5 && block.Bounds.Left == 9 && block.Bounds.Right == 11 && block.Bounds.Top == 14 && block.Bounds.Bottom == 16, "negative fractional alignment or initial bounds changed");
	Require(elements[0].Geometry.Position.Y == 15, "block alignment did not use truncated signed integer offset");
	Require(lines[0].Bounds.Left == 9 && lines[0].Bounds.Top == 19 && lines[0].Bounds.Right == 11 && lines[0].Bounds.Bottom == 21, "negative metrics were normalized or line bounds shifted");
}

static void TestInvalidGeometryLeavesStorage() {
	std::array<sgre::text::TextBlockElement, 2> elements{};
	elements[0].Geometry = {{1, 2}, {3, 4}};
	elements[1].Geometry = {{5, 6}, {7, std::numeric_limits<float>::infinity()}};
	std::array<sgre::text::TextBlockLine, 1> lines{};
	lines[0].Bounds = {-1, -2, 3, 4};
	lines[0].Elements = elements;
	sgre::text::TextBlockGeometrySettings settings;
	settings.Alignment = sgre::text::TextAlignment::Center;
	RequireFailure([&] {
		static_cast<void>(sgre::text::FinalizeTextBlockGeometry(settings, lines));
	},
	    "glyph");
	Require(elements[0].Geometry.Position.X == 1 && elements[0].Geometry.Position.Y == 2 && lines[0].Bounds.Top == -2 && lines[0].Bounds.Bottom == 4, "failed block preparation partially modified storage");
	elements[1].Geometry.Metrics.Height = 8;
	settings.RenderHeight = std::numeric_limits<float>::max();
	RequireFailure([&] {
		static_cast<void>(sgre::text::FinalizeTextBlockGeometry(settings, lines));
	},
	    "signed integer");
	Require(elements[0].Geometry.Position.Y == 2 && lines[0].Bounds.Bottom == 4, "alignment conversion failure partially modified storage");
	settings.Direction = sgre::text::TextDirection::Vertical;
	settings.Alignment = sgre::text::TextAlignment::End;
	settings.RenderWidth = -2147483648.0f;
	RequireFailure([&] {
		static_cast<void>(sgre::text::FinalizeTextBlockGeometry(settings, {}, {0, 0, 0, 0}));
	},
	    "negate");
	settings.Direction = static_cast<sgre::text::TextDirection>(99);
	RequireFailure([&] {
		static_cast<void>(sgre::text::FinalizeTextBlockGeometry(settings, lines));
	},
	    "settings");
	const std::vector<sgre::text::TextGlyphGeometry> oversized_ruby(1024 * 1024);
	elements[0].Ruby = oversized_ruby;
	settings.Direction = sgre::text::TextDirection::Horizontal;
	settings.RenderHeight = 100;
	RequireFailure([&] {
		static_cast<void>(sgre::text::FinalizeTextBlockGeometry(settings, lines));
	},
	    "ruby count");
	Require(elements[0].Geometry.Position.Y == 2 && lines[0].Bounds.Bottom == 4, "count failure partially modified storage");
}

int main() {
	try {
		TestRetainedHorizontalBlock();
		TestRetainedVerticalBlock();
		TestEmptyAndSignedZero();
		TestFractionalAlignmentAndInitialBounds();
		TestInvalidGeometryLeavesStorage();
		std::cout << "Text block geometry tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << "Text block geometry test failure: " << error.what() << '\n';

		return 1;
	}
}
