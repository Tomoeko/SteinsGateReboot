#include "sgre/text/cff_glyph_stroke_path.hpp"

#include "sgre/text/cff_glyph_stroke_curve.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <span>
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

static std::array<sgre::text::CffGlyphPathCommand, 2> ConicInput(sgre::text::CffGlyphPathPoint start, sgre::text::CffGlyphPathPoint control, sgre::text::CffGlyphPathPoint end) {
	std::array<sgre::text::CffGlyphPathCommand, 2> result{};
	result[0].Kind = sgre::text::CffGlyphPathCommandKind::MoveTo;
	result[0].Points[0] = start;
	result[1].Kind = sgre::text::CffGlyphPathCommandKind::QuadraticTo;
	result[1].Points[0] = control;
	result[1].Points[1] = end;

	return result;
}

static void RequireStrokePathEqual(const std::vector<sgre::text::CffGlyphPathCommand>& actual, const std::vector<sgre::text::CffGlyphPathCommand>& expected, double translate_x = 0, double translate_y = 0) {
	Require(actual.size() == expected.size(), "independent conic contour command count differs");
	for (std::size_t index = 0; index < actual.size(); ++index) {
		Require(actual[index].Kind == expected[index].Kind, "independent conic contour command kind differs");
		for (std::size_t point = 0; point < PointCount(actual[index].Kind); ++point) {
			const auto target_x = expected[index].Points[point].X + translate_x;
			const auto target_y = expected[index].Points[point].Y + translate_y;
			Require(std::isfinite(actual[index].Points[point].X) && std::isfinite(actual[index].Points[point].Y), "conic output point must stay finite");
			Require(std::abs(actual[index].Points[point].X - target_x) < 1e-10 && std::abs(actual[index].Points[point].Y - target_y) < 1e-10, "independent conic contour coordinate differs");
		}
	}
}

static void RequireStrokeInputFailure(std::span<const sgre::text::CffGlyphPathCommand> input, double width, std::string_view diagnostic) {
	try {
		static_cast<void>(sgre::text::PrepareCffGlyphStroke(input, width));
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(diagnostic) != std::string_view::npos, "conic rejection must preserve the exact failing boundary");
		return;
	}
	throw std::runtime_error("conic malformed input unexpectedly reached stroke publication");
}

static void TestDirectConicFlatAndCuspContours() {
	const auto constant = ConicInput({4, 4}, {4, 4}, {4, 4});
	Require(sgre::text::PrepareCffGlyphStroke(constant, 4).empty(), "constant direct conic must not invent a side or cap");
	std::array<sgre::text::CffGlyphPathCommand, 2> line{};
	line[0].Kind = sgre::text::CffGlyphPathCommandKind::MoveTo;
	line[0].Points[0] = {4, 4};
	line[1].Kind = sgre::text::CffGlyphPathCommandKind::LineTo;
	line[1].Points[0] = {12, 4};
	const auto expected = sgre::text::PrepareCffGlyphStroke(line, 4);
	for (const auto control : std::array<sgre::text::CffGlyphPathPoint, 3>{{{4, 4}, {5, 4}, {12, 4}}}) {
		const auto conic = ConicInput({4, 4}, control, {12, 4});
		RequireStrokePathEqual(sgre::text::PrepareCffGlyphStroke(conic, 4), expected);
	}
	const auto reverse = ConicInput({4, 4}, {12, 4}, {8, 4});
	const auto contour = sgre::text::PrepareCffGlyphStroke(reverse, 4);
	Require(contour.size() == 12, "direct reversing conic must retain two real lines and round reversal join");
	Require(contour[1].Kind == sgre::text::CffGlyphPathCommandKind::LineTo && std::abs(contour[1].Points[0].X - 28.0 / 3.0) < 1e-14 && contour[1].Points[0].Y == 6, "direct conic extremum uses actual derivative root rather than the control point");
	Require(std::count_if(contour.begin(), contour.end(), [](const auto& command) {
		return command.Kind == sgre::text::CffGlyphPathCommandKind::CubicTo;
	}) == 6,
	    "native reversal adds two round-join cubics to the four open caps");
}

static void TestDirectConicOffsetAndTranslation() {
	const std::array<sgre::text::CffGlyphPathPoint, 3> points{{{0, 0}, {4, 0}, {8, 4}}};
	const auto sides = sgre::text::PrepareCffGlyphStrokeQuadratic(points, 1);
	Require(sides.Left.size() == 1 && sides.Right.size() == 1, "orthogonal derivative delta takes native direct-conic unsplit branch");
	const auto unit = 1.0 / std::sqrt(2.0);
	Require(sides.StartNormal.X == 0 && sides.StartNormal.Y == 1 && std::abs(sides.EndNormal.X + unit) < 1e-15 && std::abs(sides.EndNormal.Y - unit) < 1e-15, "direct conic records original entry and completed endpoint normals");
	const auto sum_x = -unit;
	const auto sum_y = 1 + unit;
	const auto denominator = sum_x * sum_x + sum_y * sum_y;
	Require(sides.Left[0].Kind == sgre::text::CffGlyphPathCommandKind::QuadraticTo && std::abs(sides.Left[0].Points[0].X - (4 + sum_x / denominator)) < 1e-15 && std::abs(sides.Left[0].Points[0].Y - sum_y / denominator) < 1e-15, "conic offset keeps the actual quadratic control and normal intersection");
	Require(std::abs(sides.Left[0].Points[1].X - (8 - unit * .5)) < 1e-15 && std::abs(sides.Right[0].Points[1].Y - (4 - unit * .5)) < 1e-15, "paired conic endpoints retain opposite exact half-width offsets");
	const auto input = ConicInput({2, 8}, {8, 0}, {14, 8});
	const auto expected = sgre::text::PrepareCffGlyphStroke(input, 4);
	Require(std::any_of(expected.begin(), expected.end(), [](const auto& command) {
		return command.Kind == sgre::text::CffGlyphPathCommandKind::QuadraticTo;
	}),
	    "curved conic must emit native quadratic geometry");
	Require(std::count_if(expected.begin(), expected.end(), [](const auto& command) {
		return command.Kind == sgre::text::CffGlyphPathCommandKind::CubicTo;
	}) == 4,
	    "simple conic uses cubics only for four original open-cap pieces");
	const auto translated = ConicInput({34, -8}, {40, -16}, {46, -8});
	RequireStrokePathEqual(sgre::text::PrepareCffGlyphStroke(translated, 4), expected, 32, -16);
}

static void TestDirectConicMixedContoursAndOwnedOutput() {
	auto first = ConicInput({2, 8}, {8, 0}, {14, 8});
	const auto second = ConicInput({22, 8}, {28, 16}, {34, 8});
	std::vector<sgre::text::CffGlyphPathCommand> both(first.begin(), first.end());
	both.insert(both.end(), second.begin(), second.end());
	auto expected = sgre::text::PrepareCffGlyphStroke(first, 4);
	const auto tail = sgre::text::PrepareCffGlyphStroke(second, 4);
	expected.insert(expected.end(), tail.begin(), tail.end());
	const auto owned = sgre::text::PrepareCffGlyphStroke(both, 4);
	first = {};
	both.clear();
	both.shrink_to_fit();
	RequireStrokePathEqual(owned, expected);
	Require(std::count_if(owned.begin(), owned.end(), [](const auto& command) {
		return command.Kind == sgre::text::CffGlyphPathCommandKind::MoveTo;
	}) == 2,
	    "each native source contour retains its own move and caps");
	std::array<sgre::text::CffGlyphPathCommand, 4> mixed{};
	mixed[0].Kind = sgre::text::CffGlyphPathCommandKind::MoveTo;
	mixed[0].Points[0] = {0, 8};
	mixed[1].Kind = sgre::text::CffGlyphPathCommandKind::LineTo;
	mixed[1].Points[0] = {4, 8};
	mixed[2].Kind = sgre::text::CffGlyphPathCommandKind::QuadraticTo;
	mixed[2].Points[0] = {8, 0};
	mixed[2].Points[1] = {12, 8};
	mixed[3].Kind = sgre::text::CffGlyphPathCommandKind::CubicTo;
	mixed[3].Points = {{{16, 16}, {20, 16}, {20, 8}}};
	const auto result = sgre::text::PrepareCffGlyphStroke(mixed, 2);
	Require(!result.empty() && result.front().Kind == sgre::text::CffGlyphPathCommandKind::MoveTo && std::all_of(result.begin(), result.end(), [](const auto& command) {
		return command.Kind != sgre::text::CffGlyphPathCommandKind::Close;
	}),
	    "mixed original conic/cubic input retains its native open-path assembly");
}

static void TestDirectConicInputBoundaries() {
	auto input = ConicInput({2, 8}, {8, 0}, {14, 8});
	RequireStrokeInputFailure(input, 0, "positive width");
	RequireStrokeInputFailure(input, std::numeric_limits<double>::quiet_NaN(), "positive width");
	input[1].Points[1].Y = std::numeric_limits<double>::infinity();
	RequireStrokeInputFailure(input, 4, "unbounded or nonfinite translated point");
	input[1].Points[1].Y = 1048577;
	RequireStrokeInputFailure(input, 4, "unbounded or nonfinite translated point");
	input[1].Points[1].Y = 8;
	input[1].Points[0].X = std::numeric_limits<double>::quiet_NaN();
	RequireStrokeInputFailure(input, 4, "unbounded or nonfinite translated point");
	input = ConicInput({2, 8}, {8, 0}, {14, 8});
	input[0].Kind = sgre::text::CffGlyphPathCommandKind::QuadraticTo;
	RequireStrokeInputFailure(input, 4, "begin at its actual contour move");
	input[0].Kind = sgre::text::CffGlyphPathCommandKind::MoveTo;
	input[1].Kind = sgre::text::CffGlyphPathCommandKind::Close;
	RequireStrokeInputFailure(input, 4, "native open flags");
}

static void TestDirectConicRetainsBothCurvatureBoundaries() {
	const std::array<sgre::text::CffGlyphPathPoint, 3> points{{{4, 12}, {6, 0}, {8, 12}}};
	const auto sides = sgre::text::PrepareCffGlyphStrokeQuadratic(points, 1);
	Require(sides.Left.size() == sides.Right.size(), "direct conic curvature intervals require paired sides");
	// For this parabola |Q'|^2 = 16 + (48t-24)^2 and |Q' cross Q''| = 192.
	// Radius .5 reaches the two unit offset-curvature boundaries at these parameters.
	const auto derivative_y = std::sqrt(std::cbrt(96.0 * 96.0) - 16);
	for (const auto parameter : std::array<double, 2>{{(24 - derivative_y) / 48, (24 + derivative_y) / 48}}) {
		const auto x = 4 + 4 * parameter;
		const auto y = 12 - 24 * parameter + 24 * parameter * parameter;
		bool found = false;
		for (std::size_t index = 0; index < sides.Left.size(); ++index) {
			const auto center_x = (sides.Left[index].Points[1].X + sides.Right[index].Points[1].X) * .5;
			const auto center_y = (sides.Left[index].Points[1].Y + sides.Right[index].Points[1].Y) * .5;
			found = found || (std::abs(center_x - x) < 1e-10 && std::abs(center_y - y) < 1e-10);
		}
		Require(found, "direct conic must preserve each original global curvature boundary, including the second interval");
	}
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
			if (actual[index].Kind != expected.Kind) {
				throw std::runtime_error("whole native stroke command kind differs: case=" + std::to_string(case_index) + " command=" + std::to_string(index) + " actual=" + std::to_string(static_cast<int>(actual[index].Kind)) + " expected=" + std::to_string(static_cast<int>(expected.Kind)) + " width=" + std::to_string(width) + " input_count=" + std::to_string(commands.size()) + " output_count=" + std::to_string(actual.size()));
			}
			for (std::size_t point = 0; point < PointCount(expected.Kind); ++point) {
				for (std::size_t axis = 0; axis < 2; ++axis) {
					const auto value = axis == 0 ? actual[index].Points[point].X : actual[index].Points[point].Y;
					const auto target = axis == 0 ? expected.Points[point].X : expected.Points[point].Y;
					const auto scale = std::max({1.0, std::abs(value), std::abs(target)});
					if (!std::isfinite(value) || std::abs(value - target) > 16 * std::numeric_limits<double>::epsilon() * scale) {
						throw std::runtime_error("whole native stroke case " + std::to_string(case_index) + " command " + std::to_string(index) + " point " + std::to_string(point) + " axis " + std::to_string(axis) + " exceeds preselected scaled coordinate bound: actual_word=" + std::to_string(std::bit_cast<std::uint64_t>(value)) + " expected_word=" + std::to_string(std::bit_cast<std::uint64_t>(target)) + " difference_word=" + std::to_string(std::bit_cast<std::uint64_t>(value - target)) + " bound_word=" + std::to_string(std::bit_cast<std::uint64_t>(16 * std::numeric_limits<double>::epsilon() * scale)));
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
		TestDirectConicFlatAndCuspContours();
		TestDirectConicOffsetAndTranslation();
		TestDirectConicMixedContoursAndOwnedOutput();
		TestDirectConicInputBoundaries();
		TestDirectConicRetainsBothCurvatureBoundaries();
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
