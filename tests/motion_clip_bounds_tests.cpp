#include "sgre/scene/motion_clip_bounds.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using sgre::resource::MotionPoint;
using sgre::scene::MotionBounds;
using sgre::scene::MotionClipBoundsResult;
using sgre::scene::MotionClipCommand;
using sgre::scene::MotionClipGeometry;
using sgre::scene::MotionClipNode;

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

	throw std::runtime_error("expected failure: " + std::string(expected));
}

static MotionBounds Bounds(float minimum_x, float minimum_y, float maximum_x, float maximum_y) {
	MotionBounds bounds;
	bounds.MinimumX = minimum_x;
	bounds.MinimumY = minimum_y;
	bounds.MaximumX = maximum_x;
	bounds.MaximumY = maximum_y;

	return bounds;
}

static bool SameBits(const MotionBounds& left, const MotionBounds& right) {
	return std::bit_cast<std::uint32_t>(left.MinimumX) == std::bit_cast<std::uint32_t>(right.MinimumX) && std::bit_cast<std::uint32_t>(left.MinimumY) == std::bit_cast<std::uint32_t>(right.MinimumY) && std::bit_cast<std::uint32_t>(left.MaximumX) == std::bit_cast<std::uint32_t>(right.MaximumX) && std::bit_cast<std::uint32_t>(left.MaximumY) == std::bit_cast<std::uint32_t>(right.MaximumY);
}

static MotionClipCommand Command(MotionClipGeometry& geometry, std::uint64_t group = 0, std::uint32_t count = 1, bool hidden = false) {
	MotionClipCommand command;
	command.Geometry = &geometry;
	command.ClipGroup = group;
	command.GeometryCount = count;
	command.Hidden = hidden;

	return command;
}

static MotionClipGeometry Cached(const MotionBounds& bounds) {
	MotionClipGeometry geometry;
	geometry.CachedBounds = bounds;
	geometry.Calculated = true;

	return geometry;
}

static void TestLazyCacheAndUnorderedValues() {
	std::array<MotionPoint, 2> points{};
	points[0].X = -0.0f;
	points[0].Y = 0.0f;
	points[1].X = 0.0f;
	points[1].Y = -0.0f;
	MotionClipGeometry geometry;
	geometry.Positions = points;
	const auto bounds = sgre::scene::MeasureMotionGeometryBounds(geometry);
	Require(geometry.Calculated && SameBits(bounds, Bounds(0.0f, -0.0f, 0.0f, -0.0f)), "native equal comparisons did not select the second signed-zero operand");
	points[0].X = -10;
	Require(SameBits(sgre::scene::MeasureMotionGeometryBounds(geometry), bounds), "calculated geometry rescanned changed borrowed positions");
	MotionClipGeometry empty;
	const auto empty_bounds = sgre::scene::MeasureMotionGeometryBounds(empty);
	Require(empty.Calculated && SameBits(empty_bounds, MotionBounds{}), "empty native geometry did not retain sentinel bounds and set its cache flag");
	const auto nan = std::bit_cast<float>(std::uint32_t{0x7fc12345});
	points[0].X = nan;
	points[1].X = 1;
	geometry.Calculated = false;
	geometry.CachedBounds = {};
	Require(sgre::scene::MeasureMotionGeometryBounds(geometry).MinimumX == 1, "native minimum did not replace an earlier NaN with its finite second operand");
	points[0].X = 1;
	points[1].X = nan;
	geometry.Calculated = false;
	geometry.CachedBounds = {};
	Require(std::bit_cast<std::uint32_t>(sgre::scene::MeasureMotionGeometryBounds(geometry).MaximumX) == 0x7fc12345, "native maximum did not preserve its final unordered second-operand bits");
}

static void TestVisibleSameGroupRun() {
	auto first = Cached(Bounds(-2, -3, 4, 5));
	auto second = Cached(Bounds(-4, 0, 8, 10));
	MotionClipGeometry ignored;
	std::array<MotionClipCommand, 6> commands{Command(first, 7), Command(ignored, 99, 1, true), Command(ignored, 99, 0), Command(second, 7), Command(ignored, 99), Command(ignored, 7)};
	const auto result = sgre::scene::MeasureMotionClipRun(commands, 0, 7);
	Require(result.HasBounds && SameBits(result.Bounds, Bounds(-4, -3, 8, 10)) && !ignored.Calculated, "clip run did not skip hidden/empty mismatched groups or stop at the first eligible mismatch");
	Require(!sgre::scene::MeasureMotionClipRun(commands, commands.size(), 7).HasBounds, "end-of-run start produced bounds");
	RequireFailure([&] {
		static_cast<void>(sgre::scene::MeasureMotionClipRun(commands, commands.size() + 1, 7));
	},
	    "command range");
}

static void TestChainIntersectionAndMembers() {
	auto outer = Cached(Bounds(-10, -10, 10, 10));
	auto a = Cached(Bounds(-4, -3, 4, 5));
	auto b = Cached(Bounds(1, -2, 12, 8));
	MotionClipGeometry ignored;
	std::array<MotionClipCommand, 3> members{Command(a, 0, 0), Command(b), Command(ignored, 0, 1, true)};
	std::array<MotionClipNode, 3> nodes{};
	nodes[0].ModeFlags = 2;
	nodes[0].Parent = 1;
	nodes[1].ModeFlags = 4;
	nodes[1].Members = members;
	nodes[1].Parent = 2;
	nodes[2].Command = Command(outer);
	const auto result = sgre::scene::MeasureMotionClipChain(nodes, 0);
	Require(result.HasBounds && SameBits(result.Bounds, Bounds(-4, -3, 10, 8)) && !ignored.Calculated, "clip chain did not union visible members including zero-count members before parent intersection");
	members[0].Hidden = true;
	members[1].Hidden = true;
	nodes[1].Parent.reset();
	Require(!sgre::scene::MeasureMotionClipChain(nodes, 1).HasBounds, "empty visible member union produced a chain bound");
	members[0].Hidden = false;
	const auto nan = std::bit_cast<float>(std::uint32_t{0x7fc12345});
	a.CachedBounds = Bounds(nan, 0, nan, 1);
	Require(!sgre::scene::MeasureMotionClipChain(nodes, 1).HasBounds, "native COMISS/JB accepted unordered member-union bounds");
	nodes[1].ModeFlags = 0;
	nodes[1].Command = Command(a);
	Require(sgre::scene::MeasureMotionClipChain(nodes, 1).HasBounds, "ordinary clip geometry incorrectly used the union validity guard");
	a.CachedBounds = Bounds(-4, -3, 4, 5);

	nodes[0].ModeFlags = 0;
	nodes[0].Command = Command(a);
	nodes[0].Parent = 2;
	nodes[2].Command = Command(b);
	Require(SameBits(sgre::scene::MeasureMotionClipChain(nodes, 0).Bounds, Bounds(1, -2, 4, 5)), "ordinary clip-node intersection differs");
	b.CachedBounds = Bounds(20, 20, 30, 30);
	const auto disjoint = sgre::scene::MeasureMotionClipChain(nodes, 0);
	Require(disjoint.HasBounds && disjoint.Bounds.MaximumX < disjoint.Bounds.MinimumX, "native chain hid a disjoint intersection instead of preserving its result flag");
	nodes[0].Parent = 0;
	RequireFailure([&] {
		static_cast<void>(sgre::scene::MeasureMotionClipChain(nodes, 0));
	},
	    "cycle");
	nodes[0].Parent = 99;
	RequireFailure([&] {
		static_cast<void>(sgre::scene::MeasureMotionClipChain(nodes, 0));
	},
	    "invalid parent");
}

static std::uint32_t ReadWord(std::ifstream& input) {
	std::array<char, 4> bytes{};
	input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
	Require(input.good(), "bounded motion clip oracle is truncated");

	return static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[0])) | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[1])) << 8) | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[2])) << 16) | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[3])) << 24);
}

static MotionBounds ReadBounds(std::ifstream& input) {
	MotionBounds result;
	result.MinimumX = std::bit_cast<float>(ReadWord(input));
	result.MinimumY = std::bit_cast<float>(ReadWord(input));
	result.MaximumX = std::bit_cast<float>(ReadWord(input));
	result.MaximumY = std::bit_cast<float>(ReadWord(input));

	return result;
}

static void TestNativeReplay(const char* input_path, const char* output_path) {
	std::ifstream input(input_path, std::ios::binary);
	std::ifstream expected(output_path, std::ios::binary);
	Require(input.is_open() && expected.is_open(), "cannot open bounded motion clip oracle");
	for (std::size_t fixture = 0; fixture < 3000; ++fixture) {
		const auto operation = ReadWord(input);
		const auto geometry_count = ReadWord(input);
		const auto command_count = ReadWord(input);
		const auto node_count = ReadWord(input);
		const auto start = ReadWord(input);
		const auto group = ReadWord(input);
		const auto first = ReadWord(input);
		const auto selected = ReadWord(input);
		Require(operation < 3 && geometry_count > 0 && geometry_count <= 16 && command_count <= 16 && node_count <= 8 && start <= command_count && first <= node_count && selected < geometry_count, "motion clip oracle header exceeds its fixture bounds");
		std::vector<MotionClipGeometry> geometries(geometry_count);
		std::vector<std::vector<MotionPoint>> positions(geometry_count);
		for (std::uint32_t index = 0; index < geometry_count; ++index) {
			geometries[index].Calculated = ReadWord(input) != 0;
			geometries[index].CachedBounds = ReadBounds(input);
			const auto count = ReadWord(input);
			Require(count <= 8, "motion clip oracle position count exceeds its fixture bound");
			positions[index].resize(count);
			for (auto& point : positions[index]) {
				point.X = std::bit_cast<float>(ReadWord(input));
				point.Y = std::bit_cast<float>(ReadWord(input));
			}

			geometries[index].Positions = positions[index];
		}

		std::vector<MotionClipCommand> commands;
		commands.reserve(command_count);
		for (std::uint32_t index = 0; index < command_count; ++index) {
			const auto geometry = ReadWord(input);
			const auto command_group = ReadWord(input);
			const auto count = ReadWord(input);
			const auto hidden = ReadWord(input);
			Require(geometry < geometry_count && hidden <= 1, "motion clip oracle command exceeds its fixture bounds");
			commands.push_back(Command(geometries[geometry], command_group, count, hidden != 0));
		}

		std::vector<MotionClipNode> nodes(node_count);
		std::vector<std::vector<MotionClipCommand>> members(node_count);
		for (std::uint32_t index = 0; index < node_count; ++index) {
			const auto command = ReadWord(input);
			const auto flags = ReadWord(input);
			const auto parent = ReadWord(input);
			const auto count = ReadWord(input);
			Require(command < command_count && flags < 8 && parent <= node_count && count <= 16, "motion clip oracle node exceeds its fixture bounds");
			nodes[index].Command = commands[command];
			nodes[index].ModeFlags = static_cast<std::uint8_t>(flags);
			if (parent) {
				nodes[index].Parent = parent - 1;
			}

			for (std::uint32_t member = 0; member < count; ++member) {
				const auto identifier = ReadWord(input);
				Require(identifier < command_count, "motion clip oracle member exceeds its fixture bounds");
				members[index].push_back(commands[identifier]);
			}

			nodes[index].Members = members[index];
		}

		MotionClipBoundsResult actual;
		if (operation == 0) {
			actual.Bounds = sgre::scene::MeasureMotionGeometryBounds(geometries[selected]);
			actual.HasBounds = true;
		} else if (operation == 1) {
			actual = sgre::scene::MeasureMotionClipRun(commands, start, group);
		} else {
			const auto root = first ? std::optional<std::uint32_t>(first - 1) : std::nullopt;
			actual = sgre::scene::MeasureMotionClipChain(nodes, root);
		}

		Require(actual.HasBounds == (ReadWord(expected) != 0), "native motion clip result flag differs at fixture " + std::to_string(fixture));
		Require(SameBits(actual.Bounds, ReadBounds(expected)), "native motion clip result words differ at fixture " + std::to_string(fixture));
		for (const auto& geometry : geometries) {
			Require(geometry.Calculated == (ReadWord(expected) != 0) && SameBits(geometry.CachedBounds, ReadBounds(expected)), "native motion clip cached state differs at fixture " + std::to_string(fixture));
		}
	}

	Require(input.peek() == std::ifstream::traits_type::eof() && expected.peek() == std::ifstream::traits_type::eof(), "motion clip oracle has trailing data");
}

int main(int argc, char** argv) {
	try {
		Require(argc == 1 || argc == 3, "usage: motion_clip_bounds_tests [native_input native_output]");
		TestLazyCacheAndUnorderedValues();
		TestVisibleSameGroupRun();
		TestChainIntersectionAndMembers();
		if (argc == 3) {
			TestNativeReplay(argv[1], argv[2]);
		}

		std::cout << "motion clip bounds fixtures passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
