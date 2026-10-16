#include "sgre/text/line_geometry.hpp"

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
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, "line geometry error lost its context");

		return;
	}

	throw std::runtime_error("invalid line geometry was accepted");
}

struct NativeLineElement {
	std::array<std::uint32_t, 4> Input{};
	std::array<std::uint32_t, 2> Position{};
};

struct NativeLineFixture {
	sgre::text::TextDirection Direction = sgre::text::TextDirection::Horizontal;
	bool CenterVertical = false;
	std::int32_t Alignment = 0;
	std::size_t ElementCount = 0;
	// Render width/height, cursor x/y, minimum cross extent, line spacing, and line start.
	std::array<std::uint32_t, 7> Settings{};
	std::array<NativeLineElement, 3> Elements;
	// Leading cross position, cross advance, and next cursor x/y.
	std::array<std::uint32_t, 4> Line{};
};

static void TestOriginalLineGeometry() {
	// Original 0x14002e330 numeric blocks, including its late cursor advance, produced these words.
	const std::array<NativeLineFixture, 2> fixtures = {{
	    {sgre::text::TextDirection::Horizontal, false, 0, 3, {0x42d6ca8c, 0x43240c00, 0x4236a618, 0x42c6d9c6, 0x416aec20, 0x41a3be2f, 0xc002dd0f}, {{{{0xc0a8c22e, 0x420b6052, 0x42207dc8, 0x41f2adad}, {0x41c5cf74, 0x429ca06c}}, {{0x41ee3bfb, 0x4241cd42, 0x422b3d71, 0x422de087}, {0x426f1dfe, 0x42b7d6e4}}, {{0x40f50f07, 0x4028a289, 0x41ce8ac1, 0x41a6fc18}, {0x4216a1e1, 0x42386ab0}}}}, {0x42386ab0, 0x427fbf9e, 0xc002dd0f, 0x43235ccb}},
	    {sgre::text::TextDirection::Vertical, true, 0, 2, {0x43c7993c, 0x42e22e1d, 0x431b52dc, 0x4321904e, 0x41ca9147, 0x40c6cd8f, 0xc0cf29ff}, {{{{0x42430dc9, 0x412a2da3, 0x424ee945, 0x42684be5}, {0x41b7324d, 0xc155d25d}}, {{0x42433cca, 0x42285e23, 0x4207f1ab, 0x4269465a}, {0x41b7904f, 0x4190bc46}}, {}}}, {0x43c7993c, 0x4267c2f7, 0x42c2c43d, 0xc0cf29ff}},
	}};
	for (const auto& fixture : fixtures) {
		sgre::text::TextLineGeometrySettings settings;
		settings.Direction = fixture.Direction;
		settings.Alignment = sgre::text::DecodeTextAlignment(fixture.Alignment);
		settings.CenterVerticalElements = fixture.CenterVertical;
		settings.RenderWidth = std::bit_cast<float>(fixture.Settings[0]);
		settings.RenderHeight = std::bit_cast<float>(fixture.Settings[1]);
		settings.MinimumCrossExtent = std::bit_cast<float>(fixture.Settings[4]);
		settings.LineSpacing = std::bit_cast<float>(fixture.Settings[5]);
		settings.LineStart = std::bit_cast<float>(fixture.Settings[6]);
		settings.AllowCrossOverflow = true;
		sgre::text::TextCursor cursor;
		cursor.X = std::bit_cast<float>(fixture.Settings[2]);
		cursor.Y = std::bit_cast<float>(fixture.Settings[3]);
		std::array<sgre::text::TextCursor, 3> positions;
		std::array<sgre::resource::GlyphSize, 3> metrics;
		for (std::size_t index = 0; index < fixture.ElementCount; ++index) {
			positions[index].X = std::bit_cast<float>(fixture.Elements[index].Input[0]);
			positions[index].Y = std::bit_cast<float>(fixture.Elements[index].Input[1]);
			metrics[index].Width = std::bit_cast<float>(fixture.Elements[index].Input[2]);
			metrics[index].Height = std::bit_cast<float>(fixture.Elements[index].Input[3]);
		}

		const auto line = sgre::text::FinalizeTextLineGeometry(settings, cursor, std::span<const sgre::resource::GlyphSize>(metrics).first(fixture.ElementCount), std::span<sgre::text::TextCursor>(positions).first(fixture.ElementCount));
		Require(line.Accepted && !line.CrossLimitExceeded, "valid original line geometry was discarded");
		const std::array<float, 4> actual = {line.LeadingCrossPosition, line.CrossAdvance, line.NextCursor.X, line.NextCursor.Y};
		for (std::size_t index = 0; index < actual.size(); ++index) {
			Require(std::bit_cast<std::uint32_t>(actual[index]) == fixture.Line[index], "line descriptor or cursor differs from original geometry");
		}
		for (std::size_t index = 0; index < fixture.ElementCount; ++index) {
			Require(std::bit_cast<std::uint32_t>(positions[index].X) == fixture.Elements[index].Position[0] && std::bit_cast<std::uint32_t>(positions[index].Y) == fixture.Elements[index].Position[1], "final line position differs from original geometry");
		}
	}
}

static void TestRetainedInitializedGameLines() {
	// The initialized game's ctor/append/history-copy/dtor probe produced these synthetic lines.
	// Native capture SHA-256 is recorded in docs/recovery/text_layout.md; no original game file is needed.
	const std::array<std::array<std::uint32_t, 17>, 12> captured = {{
	    {{0x1, 0x3, 0x40400000, 0x429e0000, 0x42480000, 0x41e80000, 0x0, 0x4, 0x41, 0x42b20000, 0x42580000, 0x42, 0x42ce0000, 0x42480000, 0x43, 0x42ee0000, 0x42680000}},
	    {{0x1, 0x3, 0x40400000, 0x429e0000, 0x42480000, 0x41e80000, 0x0, 0x4, 0x41, 0x42b20000, 0x42580000, 0x42, 0x42ce0000, 0x42480000, 0x43, 0x42ee0000, 0x42680000}},
	    {{0x1, 0x3, 0x40400000, 0x429e0000, 0x42480000, 0x41e80000, 0x0, 0x4, 0x41, 0x431f0000, 0x42580000, 0x42, 0x432d0000, 0x42480000, 0x43, 0x433d0000, 0x42680000}},
	    {{0x1, 0x3, 0x40400000, 0x429e0000, 0x42480000, 0x41e80000, 0x0, 0x4, 0x41, 0x431f0000, 0x42580000, 0x42, 0x432d0000, 0x42480000, 0x43, 0x433d0000, 0x42680000}},
	    {{0x1, 0x3, 0x40400000, 0x429e0000, 0x42480000, 0x41e80000, 0x0, 0x4, 0x41, 0x41a00000, 0x42580000, 0x42, 0x42080000, 0x42480000, 0x43, 0x42480000, 0x42680000}},
	    {{0x1, 0x3, 0x40400000, 0x429e0000, 0x42480000, 0x41e80000, 0x0, 0x4, 0x41, 0x41a00000, 0x42580000, 0x42, 0x42080000, 0x42480000, 0x43, 0x42480000, 0x42680000}},
	    {{0x1, 0x3, 0x43010000, 0x40400000, 0x43480000, 0x41a80000, 0x0, 0x4, 0x41, 0x43160000, 0x42b80000, 0x42, 0x43160000, 0x42e40000, 0x43, 0x43160000, 0x430c0000}},
	    {{0x1, 0x3, 0x43010000, 0x40400000, 0x43480000, 0x41a80000, 0x0, 0x4, 0x41, 0x430e0000, 0x42b80000, 0x42, 0x430e0000, 0x42e40000, 0x43, 0x430e0000, 0x430c0000}},
	    {{0x1, 0x3, 0x43010000, 0x40400000, 0x43480000, 0x41a80000, 0x0, 0x4, 0x41, 0x43160000, 0x43060000, 0x42, 0x43160000, 0x431c0000, 0x43, 0x43160000, 0x43360000}},
	    {{0x1, 0x3, 0x43010000, 0x40400000, 0x43480000, 0x41a80000, 0x0, 0x4, 0x41, 0x430e0000, 0x43060000, 0x42, 0x430e0000, 0x431c0000, 0x43, 0x430e0000, 0x43360000}},
	    {{0x1, 0x3, 0x43010000, 0x40400000, 0x43480000, 0x41a80000, 0x0, 0x4, 0x41, 0x43160000, 0x42480000, 0x42, 0x43160000, 0x42900000, 0x43, 0x43160000, 0x42c40000}},
	    {{0x1, 0x3, 0x43010000, 0x40400000, 0x43480000, 0x41a80000, 0x0, 0x4, 0x41, 0x430e0000, 0x42480000, 0x42, 0x430e0000, 0x42900000, 0x43, 0x430e0000, 0x42c40000}},
	}};
	for (std::size_t case_id = 0; case_id < captured.size(); ++case_id) {
		sgre::text::TextLineGeometrySettings settings;
		settings.Direction = case_id < 6 ? sgre::text::TextDirection::Horizontal : sgre::text::TextDirection::Vertical;
		settings.Alignment = sgre::text::DecodeTextAlignment(static_cast<std::int32_t>((case_id % 6) / 2));
		settings.CenterVerticalElements = case_id % 2 != 0;
		settings.RenderWidth = 200;
		settings.RenderHeight = 200;
		settings.MinimumCrossExtent = 16;
		settings.LineSpacing = 5;
		settings.LineStart = 3;
		sgre::text::TextCursor cursor;
		cursor.X = case_id < 6 ? 20 : 150;
		cursor.Y = 50;
		const std::array<sgre::resource::GlyphSize, 3> metrics{{{12, 20}, {14, 24}, {9, 16}}};
		std::array<sgre::text::TextCursor, 3> positions;
		for (std::size_t element = 0; element < metrics.size(); ++element) {
			const auto placement = sgre::text::PlaceMeasuredTextElement(settings.Direction, cursor, metrics[element], 2);
			positions[element] = placement.Position;
			cursor = placement.NextCursor;
		}

		const auto line = sgre::text::FinalizeTextLineGeometry(settings, cursor, metrics, positions);
		const std::array<std::uint32_t, 6> geometry{line.Accepted ? 1u : 0u, std::bit_cast<std::uint32_t>(line.NextCursor.X), std::bit_cast<std::uint32_t>(line.NextCursor.Y), std::bit_cast<std::uint32_t>(line.LeadingCrossPosition), std::bit_cast<std::uint32_t>(line.CrossAdvance), line.CrossLimitExceeded ? 1u : 0u};
		const std::array<std::size_t, 6> geometry_indices{0, 2, 3, 4, 5, 6};
		for (std::size_t word = 0; word < geometry.size(); ++word) {
			Require(geometry[word] == captured[case_id][geometry_indices[word]], "line result differs from initialized-game copied history");
		}

		Require(captured[case_id][1] == metrics.size() && captured[case_id][7] == 4, "retained native copy/string-length invariant was altered");
		for (std::size_t element = 0; element < metrics.size(); ++element) {
			Require(captured[case_id][8 + element * 3] == 65 + element, "retained native glyph copy was altered");
			Require(std::bit_cast<std::uint32_t>(positions[element].X) == captured[case_id][9 + element * 3] && std::bit_cast<std::uint32_t>(positions[element].Y) == captured[case_id][10 + element * 3], "position differs from initialized-game copied element");
		}
	}
}

static void TestCrossLimitsAndOwnership() {
	sgre::text::TextLineGeometrySettings settings;
	settings.RenderWidth = 100;
	settings.RenderHeight = 50;
	settings.LineSpacing = 3;
	settings.LineStart = 5;
	sgre::text::TextCursor cursor;
	cursor.X = 20;
	cursor.Y = 40;
	std::array<sgre::resource::GlyphSize, 1> metrics;
	metrics[0].Width = 10;
	metrics[0].Height = 10;
	std::array<sgre::text::TextCursor, 1> positions;
	positions[0].X = 7;
	positions[0].Y = 30;
	const auto horizontal = sgre::text::FinalizeTextLineGeometry(settings, cursor, metrics, positions);
	Require(horizontal.Accepted && !horizontal.CrossLimitExceeded && horizontal.NextCursor.X == 5 && horizontal.NextCursor.Y == 53 && positions[0].Y == 40, "exact horizontal cross limit or next-line spacing changed");
	cursor.Y = 41;
	const auto before = positions[0];
	const auto rejected = sgre::text::FinalizeTextLineGeometry(settings, cursor, metrics, positions);
	Require(!rejected.Accepted && rejected.CrossLimitExceeded && rejected.NextCursor.X == cursor.X && rejected.NextCursor.Y == cursor.Y && positions[0].X == before.X && positions[0].Y == before.Y, "rejected line changed owner geometry or cursor");
	settings.AllowCrossOverflow = true;
	const auto allowed = sgre::text::FinalizeTextLineGeometry(settings, cursor, metrics, positions);
	Require(allowed.Accepted && allowed.CrossLimitExceeded, "cross-overflow permission discarded the line");

	settings.Direction = sgre::text::TextDirection::Vertical;
	settings.AllowCrossOverflow = false;
	cursor.X = 10;
	cursor.Y = 20;
	const auto vertical = sgre::text::FinalizeTextLineGeometry(settings, cursor, metrics, positions);
	Require(vertical.Accepted && !vertical.CrossLimitExceeded && vertical.NextCursor.X == -3 && vertical.NextCursor.Y == 5, "exact vertical cross boundary or column advance changed");
	cursor.X = 9;
	Require(!sgre::text::FinalizeTextLineGeometry(settings, cursor, metrics, positions).Accepted, "vertical cross underflow was accepted");
	settings.RenderWidth = 0;
	Require(sgre::text::FinalizeTextLineGeometry(settings, cursor, metrics, positions).Accepted, "unlimited vertical cross extent was rejected");
}

static void TestPreflightSafety() {
	sgre::text::TextLineGeometrySettings settings;
	settings.Alignment = sgre::text::TextAlignment::End;
	settings.RenderWidth = 100;
	sgre::text::TextCursor cursor;
	std::array<sgre::resource::GlyphSize, 2> metrics;
	std::array<sgre::text::TextCursor, 2> positions;
	RequireFailure([&] {
		static_cast<void>(sgre::text::FinalizeTextLineGeometry(settings, cursor, metrics, std::span<sgre::text::TextCursor>(positions).first(1)));
	},
	    "counts differ");
	settings.LineSpacing = std::numeric_limits<float>::infinity();
	RequireFailure([&] {
		static_cast<void>(sgre::text::FinalizeTextLineGeometry(settings, cursor, metrics, positions));
	},
	    "nonfinite");
	settings.LineSpacing = 0;
	positions[0].X = 1;
	positions[1].Y = std::numeric_limits<float>::max();
	metrics[1].Height = std::numeric_limits<float>::max();
	RequireFailure([&] {
		static_cast<void>(sgre::text::FinalizeTextLineGeometry(settings, cursor, metrics, positions));
	},
	    "position overflows");
	Require(positions[0].X == 1 && positions[0].Y == 0, "failed line preflight changed earlier owner positions");
}

int main() {
	try {
		TestOriginalLineGeometry();
		TestRetainedInitializedGameLines();
		TestCrossLimitsAndOwnership();
		TestPreflightSafety();
		std::cout << "native line geometry, cross limits, cursor advance, and owner-buffer checks passed\n";
	} catch (const std::exception& error) {
		std::cerr << "text line geometry tests: " << error.what() << '\n';

		return 1;
	}

	return 0;
}
