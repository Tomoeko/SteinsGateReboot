#include "sgre/text/cff_glyph_stroke.hpp"
#include "native_glyph_stroke_round_vectors.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <limits>
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

static std::uint32_t ReadWord() {
	std::array<std::uint8_t, 4> bytes{};
	std::cin.read(reinterpret_cast<char*>(bytes.data()), 4);
	Require(bool(std::cin), "native glyph stroke stream extent");

	return std::uint32_t(bytes[0]) | (std::uint32_t(bytes[1]) << 8) | (std::uint32_t(bytes[2]) << 16) | (std::uint32_t(bytes[3]) << 24);
}

static std::uint64_t ReadDoubleWord() {
	const auto low = ReadWord();
	const auto high = ReadWord();

	return std::uint64_t(low) | (std::uint64_t(high) << 32);
}

static double ReadDouble() {
	return std::bit_cast<double>(ReadDoubleWord());
}

static sgre::text::CffGlyphPathPoint ReadPoint() {
	const auto x = ReadDouble();
	const auto y = ReadDouble();

	return {x, y};
}

static void NativeDouble(double actual) {
	const auto expected = ReadDoubleWord();
	const auto actual_word = std::bit_cast<std::uint64_t>(actual);
	Require(actual_word == expected, "native glyph stroke double expected " + std::to_string(expected) + " actual " + std::to_string(actual_word));
}

static void NativeCurves(std::span<const sgre::text::CffGlyphPathCommand> commands) {
	Require(commands.size() == ReadWord(), "native round stroke cubic count");
	for (const auto& command : commands) {
		Require(command.Kind == sgre::text::CffGlyphPathCommandKind::CubicTo, "native round stroke emits actual cubic commands");
		for (const auto point : command.Points) {
			NativeDouble(point.X);
			NativeDouble(point.Y);
		}
	}
}

static sgre::text::StrokeRoundJoinInput ReadRoundInput() {
	sgre::text::StrokeRoundJoinInput input;
	input.Center = ReadPoint();
	input.Start = ReadPoint();
	input.End = ReadPoint();
	input.IncomingNormal = ReadPoint();
	input.IntersectionOffset = ReadPoint();
	input.SignedRadius = ReadDouble();
	input.SignedWidth = ReadDouble();

	return input;
}

static sgre::text::CffGlyphPathCommand PointCommand(sgre::text::CffGlyphPathCommandKind kind, sgre::text::CffGlyphPathPoint point) {
	sgre::text::CffGlyphPathCommand command;
	command.Kind = kind;
	command.Points[0] = point;

	return command;
}

static void NativeSide(std::span<const sgre::text::CffGlyphPathCommand> commands) {
	std::vector<std::uint32_t> tags;
	std::vector<sgre::text::CffGlyphPathPoint> points;
	for (const auto& command : commands) {
		if (command.Kind == sgre::text::CffGlyphPathCommandKind::CubicTo) {
			tags.insert(tags.end(), {3, 3, 1});
			points.insert(points.end(), command.Points.begin(), command.Points.end());
		} else {
			Require(command.Kind == sgre::text::CffGlyphPathCommandKind::MoveTo || command.Kind == sgre::text::CffGlyphPathCommandKind::LineTo, "native line fixture command scope");
			tags.push_back(command.Kind == sgre::text::CffGlyphPathCommandKind::MoveTo ? 0u : 1u);
			points.push_back(command.Points[0]);
		}
	}
	Require(tags.size() == ReadWord(), "native line side atom count");
	for (std::size_t index = 0; index < tags.size(); ++index) {
		Require(tags[index] == ReadWord(), "native line side command order");
		NativeDouble(points[index].X);
		NativeDouble(points[index].Y);
	}
}

static void TestNativeReplay() {
	const auto round_count = ReadWord();
	Require(round_count <= 1024, "bounded native round cases");
	for (std::uint32_t index = 0; index < round_count; ++index) {
		NativeCurves(sgre::text::PrepareRoundStrokeJoin(ReadRoundInput()));
	}
	const auto cap_count = ReadWord();
	Require(cap_count <= 1024, "bounded native cap cases");
	for (std::uint32_t index = 0; index < cap_count; ++index) {
		const auto center = ReadPoint();
		const auto start = ReadPoint();
		const auto end = ReadPoint();
		NativeCurves(sgre::text::PrepareRoundStrokeCap(center, start, end));
	}
	const auto normal_count = ReadWord();
	Require(normal_count <= 1024, "bounded native normal cases");
	for (std::uint32_t index = 0; index < normal_count; ++index) {
		const auto start = ReadPoint();
		const auto end = ReadPoint();
		const auto normal = sgre::text::PrepareStrokeLineNormal(start, end);
		Require(bool(normal) == (ReadWord() != 0), "native short-line admission");
		if (normal) {
			NativeDouble(normal->X);
			NativeDouble(normal->Y);
		}
	}
	const auto line_count = ReadWord();
	Require(line_count <= 1024, "bounded native line-join cases");
	for (std::uint32_t index = 0; index < line_count; ++index) {
		const auto center = ReadPoint();
		const auto end = ReadPoint();
		const auto incoming = ReadPoint();
		const auto width = ReadDouble();
		sgre::text::StrokeContourSides sides;
		for (auto* side : {&sides.Left, &sides.Right}) {
			side->push_back(PointCommand(sgre::text::CffGlyphPathCommandKind::MoveTo, ReadPoint()));
			side->push_back(PointCommand(sgre::text::CffGlyphPathCommandKind::LineTo, ReadPoint()));
		}
		Require(sgre::text::AppendStrokeLineSegment(sides, center, end, incoming, width) == (ReadWord() != 0), "native line continuation acceptance");
		NativeSide(sides.Left);
		NativeSide(sides.Right);
	}
}

static void TestRetainedOriginalRoundVectors() {
	for (const auto& expected : kNativeStrokeRoundVectors) {
		const auto point = [&](std::size_t offset) {
			return sgre::text::CffGlyphPathPoint{std::bit_cast<double>(expected.Inputs[offset]), std::bit_cast<double>(expected.Inputs[offset + 1])};
		};
		sgre::text::StrokeRoundJoinInput input;
		input.Center = point(0);
		input.Start = point(2);
		input.End = point(4);
		input.IncomingNormal = point(6);
		input.IntersectionOffset = point(8);
		input.SignedRadius = std::bit_cast<double>(expected.Inputs[10]);
		input.SignedWidth = std::bit_cast<double>(expected.Inputs[11]);
		const auto commands = sgre::text::PrepareRoundStrokeJoin(input);
		Require(commands.size() == expected.CubicCount, "retained original round branch count");
		for (std::size_t index = 0; index < commands.size(); ++index) {
			Require(commands[index].Kind == sgre::text::CffGlyphPathCommandKind::CubicTo, "round output owns its actual cubic");
			for (std::size_t point_index = 0; point_index < 3; ++point_index) {
				const auto& actual = commands[index].Points[point_index];
				const auto& value = expected.Points[index * 3 + point_index];
				Require(std::bit_cast<std::uint64_t>(actual.X) == value[0] && std::bit_cast<std::uint64_t>(actual.Y) == value[1], "retained original round double words");
			}
		}
	}
}

static void TestNativeCapAndLineDecisions() {
	const auto cap = sgre::text::PrepareRoundStrokeCap({0, 0}, {0, 1}, {0, -1});
	Require(cap.size() == 2 && cap[0].Points[2].X == 1 && cap[0].Points[2].Y == 0 && cap[1].Points[2].X == 0 && cap[1].Points[2].Y == -1, "native cap2 forward ordered semicircle");
	Require(std::bit_cast<std::uint64_t>(cap[0].Points[0].X) == 0x3fe1ac5111532e37ULL, "native cap constant remains truncated independently of join90");
	const auto normal = sgre::text::PrepareStrokeLineNormal({0, 0}, {10, 0});
	Require(normal && std::bit_cast<std::uint64_t>(normal->X) == 0x8000000000000000ULL && normal->Y == 1, "native line normal preserves signed zero");
	Require(!sgre::text::PrepareStrokeLineNormal({0, 0}, {1e-11, 0}), "native squared epsilon skips tiny line");
	Require(bool(sgre::text::PrepareStrokeLineNormal({0, 0}, {1e-10, 0})), "native equality at squared epsilon remains admitted");
	for (const auto width : {1., 3.}) {
		sgre::text::StrokeContourSides sides;
		sides.Left = {PointCommand(sgre::text::CffGlyphPathCommandKind::MoveTo, {-10, width * .5}), PointCommand(sgre::text::CffGlyphPathCommandKind::LineTo, {0, width * .5})};
		sides.Right = {PointCommand(sgre::text::CffGlyphPathCommandKind::MoveTo, {-10, -width * .5}), PointCommand(sgre::text::CffGlyphPathCommandKind::LineTo, {0, -width * .5})};
		Require(sgre::text::AppendStrokeLineSegment(sides, {0, 0}, {-10, 0}, {-0., 1}, width), "native reversal executes actual round and inner-center continuation");
		Require(sides.Left.size() == 5 && sides.Right.size() == 5 && sides.Right[2].Kind == sgre::text::CffGlyphPathCommandKind::CubicTo && sides.Right[3].Kind == sgre::text::CffGlyphPathCommandKind::CubicTo, "exact opposed normals remain a two-cubic outer arc and explicit inner center");
	}
}

static void TestCurveInnerJoinKeepsNativeCenter() {
	for (const auto direction : {-1., 1.}) {
		sgre::text::StrokeContourSides sides;
		sides.Left = {PointCommand(sgre::text::CffGlyphPathCommandKind::MoveTo, {-10, .5}), PointCommand(sgre::text::CffGlyphPathCommandKind::LineTo, {0, .5})};
		sides.Right = {PointCommand(sgre::text::CffGlyphPathCommandKind::MoveTo, {-10, -.5}), PointCommand(sgre::text::CffGlyphPathCommandKind::LineTo, {0, -.5})};
		auto line_sides = sides;
		sgre::text::StrokeSegmentJoinInput input;
		input.Center = {0, 0};
		input.NextTangentEnd = {0, direction * 10};
		input.IncomingNormal = {-0., 1};
		input.OutgoingNormal = {-direction, 0};
		input.Width = 1;
		sgre::text::AppendStrokeSegmentJoin(sides, input);
		const auto& inner = direction > 0 ? sides.Left : sides.Right;
		Require(inner.size() == 4, "native curve join retains the previous endpoint then appends both inner lines");
		Require(inner[1].Points[0].X == 0 && inner[1].Points[0].Y == direction * .5, "curve entry cannot apply LINE previous-endpoint clipping");
		Require(inner[2].Kind == sgre::text::CffGlyphPathCommandKind::LineTo && inner[2].Points[0].X == 0 && inner[2].Points[0].Y == 0, "native433A1E appends the actual center on either inner side");
		Require(inner[3].Kind == sgre::text::CffGlyphPathCommandKind::LineTo && inner[3].Points[0].X == -.5 && inner[3].Points[0].Y == 0, "native433A6F appends center minus the actual signed outgoing offset");
		Require(sgre::text::AppendStrokeLineSegment(line_sides, input.Center, input.NextTangentEnd, input.IncomingNormal, input.Width), "same geometry remains an actual LINE continuation");
		const auto& line_inner = direction > 0 ? line_sides.Left : line_sides.Right;
		Require(line_inner.size() == 3 && line_inner[1].Points[0].X == -.5 && line_inner[1].Points[0].Y == direction * .5, "LINE retains its separately measured intersection endpoint replacement");
	}
}

int main(int argc, char** argv) {
	try {
		if (argc == 2 && std::string_view(argv[1]) == "--native") {
			TestNativeReplay();
		} else {
			TestRetainedOriginalRoundVectors();
			TestNativeCapAndLineDecisions();
			TestCurveInnerJoinKeepsNativeCenter();
		}
		std::cout << "glyph stroke round/line kernels passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
