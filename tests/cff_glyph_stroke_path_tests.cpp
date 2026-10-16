#include "sgre/text/cff_glyph_stroke_path.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

static void Require(bool condition, std::string_view diagnostic) {
	if (!condition) {
		throw std::runtime_error(std::string(diagnostic));
	}
}

static std::uint32_t Read32(std::istream& input) {
	std::array<unsigned char, 4> bytes{};
	Require(static_cast<bool>(input.read(reinterpret_cast<char*>(bytes.data()), 4)), "truncated native whole-stroke word");

	return std::uint32_t(bytes[0]) | (std::uint32_t(bytes[1]) << 8) | (std::uint32_t(bytes[2]) << 16) | (std::uint32_t(bytes[3]) << 24);
}

static double ReadDouble(std::istream& input) {
	const auto low = Read32(input);
	const auto high = Read32(input);

	return std::bit_cast<double>(std::uint64_t(low) | (std::uint64_t(high) << 32));
}

static std::size_t PointCount(sgre::text::CffGlyphPathCommandKind kind) {
	if (kind == sgre::text::CffGlyphPathCommandKind::CubicTo) {
		return 3;
	}
	if (kind == sgre::text::CffGlyphPathCommandKind::QuadraticTo) {
		return 2;
	}
	Require(kind == sgre::text::CffGlyphPathCommandKind::MoveTo || kind == sgre::text::CffGlyphPathCommandKind::LineTo, "whole native stroke record contains an unsupported command kind");

	return 1;
}

static sgre::text::CffGlyphPathCommand ReadCommand(std::istream& input) {
	const auto raw = Read32(input);
	Require(raw <= 3, "whole native stroke typed command exceeds its bounded domain");
	sgre::text::CffGlyphPathCommand command;
	command.Kind = static_cast<sgre::text::CffGlyphPathCommandKind>(raw);
	for (auto& point : command.Points) {
		point.X = ReadDouble(input);
		point.Y = ReadDouble(input);
	}

	return command;
}

static void TestNativeOpenRectangle() {
	std::array<sgre::text::CffGlyphPathCommand, 5> commands{};
	const std::array<sgre::text::CffGlyphPathPoint, 5> points{{{2, 2}, {10, 2}, {10, 10}, {2, 10}, {2, 2}}};
	for (std::size_t index = 0; index < commands.size(); ++index) {
		commands[index].Kind = index == 0 ? sgre::text::CffGlyphPathCommandKind::MoveTo : sgre::text::CffGlyphPathCommandKind::LineTo;
		commands[index].Points[0] = points[index];
	}
	const auto result = sgre::text::PrepareCffGlyphStroke(commands, 1);
	Require(result.size() == 16, "native coincident-endpoint open rectangle retains joins and both round endpoint caps");
	Require(result.front().Points[0].X == 2 && result.front().Points[0].Y == 2.5, "native rectangle first side starts at its actual half-width offset");
	Require(result[1].Points[0].X == 9.5 && result[1].Points[0].Y == 2.5, "native inner rectangle join replaces the prior endpoint with its bounded intersection");
	Require(result.back().Kind == sgre::text::CffGlyphPathCommandKind::CubicTo && result.back().Points[2].X == 2 && result.back().Points[2].Y == 2.5, "native start cap closes the assembled geometric contour without changing its source open flag");
	Require(sgre::text::PrepareCffGlyphStroke({}, 4).empty(), "empty glyph path creates no invented stroke geometry");
}

static void TestNativeDegenerateCubics() {
	std::array<sgre::text::CffGlyphPathCommand, 2> input{};
	input[0].Kind = sgre::text::CffGlyphPathCommandKind::MoveTo;
	input[0].Points[0] = {8, 8};
	input[1].Kind = sgre::text::CffGlyphPathCommandKind::CubicTo;
	input[1].Points = {{{8, 8}, {8, 8}, {8, 8}}};
	Require(sgre::text::PrepareCffGlyphStroke(input, 4).empty(), "native point cubic emits no invented endpoint caps or stroke allocation");
	input[1].Points = {{{8, 8}, {8, 8}, {16, 8}}};
	const auto straight = sgre::text::PrepareCffGlyphStroke(input, 4);
	Require(straight.size() == 7 && straight[1].Kind == sgre::text::CffGlyphPathCommandKind::LineTo && straight[1].Points[0].X == 16 && straight[1].Points[0].Y == 10, "native repeated first controls use actual endpoint LINE fallback before curve normalization");
	input[1].Points = {{{20, 8}, {20, 8}, {8, 8}}};
	Require(sgre::text::PrepareCffGlyphStroke(input, 4).empty(), "native unordered derivative roots leave a flat coincident-end cubic at its unchanged point");
	input[1].Points = {{{8, 8}, {12, 2}, {16, 8}}};
	const auto bent = sgre::text::PrepareCffGlyphStroke(input, 4);
	Require(!bent.empty() && std::any_of(bent.begin(), bent.end(), [](const auto& command) {
		return command.Kind == sgre::text::CffGlyphPathCommandKind::QuadraticTo;
	}),
	    "native nonflat zero-first tangent retains real offset quadratics through its other tangent");
}

static void ReplayNative(const char* path) {
	std::ifstream input(path, std::ios::binary);
	Require(static_cast<bool>(input), "native whole-stroke vector input is unavailable");
	Require(Read32(input) == 0x31505343, "native whole-stroke vector magic differs");
	const auto case_count = Read32(input);
	Require(case_count > 0 && case_count <= 1024, "native whole-stroke case count exceeds its bound");
	std::size_t words = 0;
	for (std::uint32_t case_index = 0; case_index < case_count; ++case_index) {
		const auto width = ReadDouble(input);
		const auto input_count = Read32(input);
		Require(input_count <= 32768, "native whole-stroke input exceeds its command bound");
		std::vector<sgre::text::CffGlyphPathCommand> commands;
		for (std::uint32_t index = 0; index < input_count; ++index) {
			commands.push_back(ReadCommand(input));
		}
		const auto actual = sgre::text::PrepareCffGlyphStroke(commands, width);
		const auto output_count = Read32(input);
		if (actual.size() != output_count) {
			throw std::runtime_error("whole native stroke case " + std::to_string(case_index) + " command count differs: actual=" + std::to_string(actual.size()) + " expected=" + std::to_string(output_count));
		}
		for (std::uint32_t index = 0; index < output_count; ++index) {
			const auto expected = ReadCommand(input);
			Require(actual[index].Kind == expected.Kind, "whole native stroke command kind differs");
			for (std::size_t point = 0; point < PointCount(expected.Kind); ++point) {
				for (std::size_t axis = 0; axis < 2; ++axis) {
					const auto value = axis == 0 ? actual[index].Points[point].X : actual[index].Points[point].Y;
					const auto target = axis == 0 ? expected.Points[point].X : expected.Points[point].Y;
					const auto scale = std::max({1.0, std::abs(value), std::abs(target)});
					if (!std::isfinite(value) || std::abs(value - target) > 16 * std::numeric_limits<double>::epsilon() * scale) {
						throw std::runtime_error("whole native stroke case " + std::to_string(case_index) + " command " + std::to_string(index) + " point " + std::to_string(point) + " axis " + std::to_string(axis) + " exceeds preselected scaled coordinate bound");
					}
					++words;
				}
			}
		}
	}
	Require(input.peek() == std::char_traits<char>::eof(), "native whole-stroke input contains trailing data");
	std::cout << "Native whole stroke cases=" << case_count << " double words=" << words << '\n';
}

int main(int argc, char** argv) {
	try {
		TestNativeOpenRectangle();
		TestNativeDegenerateCubics();
		if (argc == 2) {
			ReplayNative(argv[1]);
		} else {
			Require(argc == 1, "expected optional whole native stroke vector path");
		}
		std::cout << "CFF whole stroke path fixtures passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
