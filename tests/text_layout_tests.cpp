#include "sgre/text/layout.hpp"

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

static void RequireFailure(const std::function<void()>& operation, std::string_view context) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, "text layout failure lost its context");

		return;
	}

	throw std::runtime_error("invalid text layout input was accepted");
}

struct NativePlacementFixture {
	sgre::text::TextDirection Direction = sgre::text::TextDirection::Horizontal;
	std::array<std::uint32_t, 5> Inputs{};
	std::array<std::uint32_t, 4> Output{};
};

static void TestOriginalElementPlacement() {
	// Complete original 0x14002dc40 append executions produced these position/cursor words.
	const std::array<NativePlacementFixture, 6> fixtures = {{
	    {sgre::text::TextDirection::Horizontal, {0xc608b90e, 0x456cbdac, 0x404443b4, 0x42bc5bc0, 0x41f30fd1}, {0xc608b90e, 0x4566dace, 0xc6083342, 0x456cbdac}},
	    {sgre::text::TextDirection::Vertical, {0xc5cfa908, 0x432e1410, 0x41a5f581, 0x424f2987, 0x41d88eb1}, {0xc5cfa908, 0x432e1410, 0xc5cfa908, 0x437cf048}},
	    {sgre::text::TextDirection::Horizontal, {0xc61b31ab, 0xc6199107, 0x433c3ea4, 0x4274c208, 0x4247562f}, {0xc61b31ab, 0xc61a85c9, 0xc617795a, 0xc6199107}},
	    {sgre::text::TextDirection::Vertical, {0x44a8d64d, 0xc603a828, 0x4206e5c2, 0x42f1a969, 0xc109f874}, {0x44a8d64d, 0xc603a828, 0x44a8d64d, 0xc601e753}},
	    {sgre::text::TextDirection::Vertical, {0x44bd3be2, 0xc31987c9, 0x42d101d7, 0x431cc3f8, 0x423157f7}, {0x44bd3be2, 0xc31987c9, 0x44bd3be2, 0x423e48b3}},
	    {sgre::text::TextDirection::Vertical, {0xc50e9a82, 0x45bb5fee, 0x41dfe1b9, 0x41a9246a, 0x4197429f}, {0xc50e9a82, 0x45bb5fee, 0xc50e9a82, 0x45bca055}},
	}};
	for (const auto& fixture : fixtures) {
		sgre::text::TextCursor cursor;
		cursor.X = std::bit_cast<float>(fixture.Inputs[0]);
		cursor.Y = std::bit_cast<float>(fixture.Inputs[1]);
		sgre::resource::GlyphSize metrics;
		metrics.Width = std::bit_cast<float>(fixture.Inputs[2]);
		metrics.Height = std::bit_cast<float>(fixture.Inputs[3]);
		const auto placement = sgre::text::PlaceMeasuredTextElement(fixture.Direction, cursor, metrics, std::bit_cast<float>(fixture.Inputs[4]));
		const std::array<float, 4> actual = {placement.Position.X, placement.Position.Y, placement.NextCursor.X, placement.NextCursor.Y};
		for (std::size_t index = 0; index < actual.size(); ++index) {
			Require(std::bit_cast<std::uint32_t>(actual[index]) == fixture.Output[index], "measured text placement differs from original instructions");
		}
	}
}

struct NativeAlignmentFixture {
	std::int32_t Mode = 0;
	std::uint32_t Extent = 0;
	std::uint32_t Cursor = 0;
	std::uint32_t Output = 0;
};

static void TestOriginalLineAlignment() {
	// Original line-finalization arithmetic at 0x14002e8c5 uses these integer-truncated offsets.
	const std::array<NativeAlignmentFixture, 6> fixtures = {{
	    {0, 0xc92541ad, 0xca3de230, 0x499491c0},
	    {1, 0xcacc056a, 0x4aee0c39, 0xcb5d08d2},
	    {-1, 0xca529047, 0x4996b4ad, 0},
	    {0, 0xc9372533, 0x4a09b68e, 0xc9b77fd8},
	    {1, 0x48f1c6c3, 0xcb03410d, 0x4b0acf43},
	    {0, 0x4a9fd0d2, 0xcac627dc, 0x4ab2fc56},
	}};
	for (const auto& fixture : fixtures) {
		const auto actual = sgre::text::TextLineAlignmentOffset(sgre::text::DecodeTextAlignment(fixture.Mode), std::bit_cast<float>(fixture.Extent), std::bit_cast<float>(fixture.Cursor));
		Require(std::bit_cast<std::uint32_t>(actual) == fixture.Output, "text line alignment differs from original arithmetic");
	}

	Require(sgre::text::TextLineAlignmentOffset(sgre::text::TextAlignment::Center, 10, 13) == -1, "center alignment rounded a negative half instead of truncating");
	Require(sgre::text::TextLineAlignmentOffset(sgre::text::TextAlignment::End, 10.5f, 13.25f) == -2, "end alignment retained fractional offset");
	Require(sgre::text::DecodeTextAlignment(2) == sgre::text::TextAlignment::Start && sgre::text::DecodeTextAlignment(7) == sgre::text::TextAlignment::Start, "native unknown alignment did not select start");
}

static void TestExtentBoundaryAndLineMetrics() {
	sgre::text::TextCursor cursor;
	cursor.X = 90;
	cursor.Y = 80;
	sgre::resource::GlyphSize size;
	size.Width = 10;
	size.Height = 20;
	Require(sgre::text::TextElementReachesLineLimit(sgre::text::TextDirection::Horizontal, cursor, size, 100, false), "exact horizontal line limit did not enter overflow handling");
	Require(sgre::text::TextElementReachesLineLimit(sgre::text::TextDirection::Vertical, cursor, size, 100, false), "exact vertical line limit did not enter overflow handling");
	Require(!sgre::text::TextElementReachesLineLimit(sgre::text::TextDirection::Horizontal, cursor, size, 101, false), "fitting text entered overflow handling");
	Require(!sgre::text::TextElementReachesLineLimit(sgre::text::TextDirection::Horizontal, cursor, size, 1, true), "explicit overflow permission was ignored");
	Require(!sgre::text::TextElementReachesLineLimit(sgre::text::TextDirection::Vertical, cursor, size, 0, false), "unlimited line extent was treated as a zero-sized line");
	const auto placement = sgre::text::PlaceMeasuredTextElement(sgre::text::TextDirection::Horizontal, cursor, size, 3);
	Require(placement.Position.X == 90 && placement.Position.Y == 60 && placement.NextCursor.X == 103 && placement.NextCursor.Y == 80, "horizontal measured placement or trailing pitch changed");
	const auto vertical = sgre::text::PlaceMeasuredTextElement(sgre::text::TextDirection::Vertical, cursor, size, 3);
	Require(vertical.Position.X == 90 && vertical.Position.Y == 80 && vertical.NextCursor.X == 90 && vertical.NextCursor.Y == 103, "vertical measured placement changed");
	std::array<sgre::resource::GlyphSize, 2> elements;
	elements[0].Width = 5;
	elements[0].Height = 10;
	elements[1].Width = 20;
	elements[1].Height = 3;
	Require(sgre::text::TextLineCrossExtent(sgre::text::TextDirection::Horizontal, elements, 2) == 10, "horizontal line cross extent did not use maximum nominal height");
	Require(sgre::text::TextLineCrossExtent(sgre::text::TextDirection::Vertical, elements, 2) == 20, "vertical line cross extent did not use maximum nominal width");
	Require(sgre::text::TextLineCrossExtent(sgre::text::TextDirection::Horizontal, elements, 30) == 30, "minimum line size was ignored");
	Require(std::bit_cast<std::uint32_t>(sgre::text::TextLineCrossExtent(sgre::text::TextDirection::Horizontal, {}, -0.0f)) == 0x80000000, "native MAXSS equal-value operand order changed");
}

static void TestSafety() {
	sgre::text::TextCursor cursor;
	sgre::resource::GlyphSize size;
	RequireFailure([&] {
		static_cast<void>(sgre::text::PlaceMeasuredTextElement(static_cast<sgre::text::TextDirection>(7), cursor, size, 0));
	},
	    "direction");
	RequireFailure([&] {
		static_cast<void>(sgre::text::PlaceMeasuredTextElement(sgre::text::TextDirection::Horizontal, cursor, size, std::numeric_limits<float>::infinity()));
	},
	    "pitch");
	cursor.X = std::numeric_limits<float>::max();
	size.Width = cursor.X;
	RequireFailure([&] {
		static_cast<void>(sgre::text::PlaceMeasuredTextElement(sgre::text::TextDirection::Horizontal, cursor, size, 0));
	},
	    "overflows");
	RequireFailure([&] {
		static_cast<void>(sgre::text::TextElementReachesLineLimit(sgre::text::TextDirection::Horizontal, cursor, size, 100, false));
	},
	    "overflows");
	RequireFailure([] {
		static_cast<void>(sgre::text::TextLineAlignmentOffset(sgre::text::TextAlignment::End, 2147483648.0f, 0));
	},
	    "native integer");
	RequireFailure([] {
		static_cast<void>(sgre::text::TextLineAlignmentOffset(static_cast<sgre::text::TextAlignment>(7), 10, 0));
	},
	    "mode");
	RequireFailure([] {
		static_cast<void>(sgre::text::TextLineCrossExtent(sgre::text::TextDirection::Horizontal, {}, std::numeric_limits<float>::quiet_NaN()));
	},
	    "nonfinite");
}

int main() {
	try {
		TestOriginalElementPlacement();
		TestOriginalLineAlignment();
		TestExtentBoundaryAndLineMetrics();
		TestSafety();
		std::cout << "native measured text placement, limits, alignment, and safety checks passed\n";
	} catch (const std::exception& error) {
		std::cerr << "text layout tests: " << error.what() << '\n';

		return 1;
	}

	return 0;
}
