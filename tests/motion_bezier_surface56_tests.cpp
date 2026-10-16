#include "sgre/scene/motion_bezier_lattice.hpp"

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

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static std::uint32_t ReadWord(std::istream& input) {
	std::array<char, 4> bytes{};
	input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
	Require(static_cast<bool>(input), "native surface vector is truncated");
	return static_cast<std::uint8_t>(bytes[0]) | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[1])) << 8) | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[2])) << 16) | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[3])) << 24);
}

static float ReadFloat(std::istream& input) {
	return std::bit_cast<float>(ReadWord(input));
}

static sgre::resource::MotionPoint ReadPoint(std::istream& input) {
	const auto x = ReadFloat(input);
	const auto y = ReadFloat(input);
	return {x, y};
}

static void RequireWord(float actual, std::uint32_t expected, std::string_view operation, std::uint32_t record, std::size_t entry) {
	if (std::bit_cast<std::uint32_t>(actual) != expected) {
		throw std::runtime_error(std::string(operation) + " differs in record " + std::to_string(record) + " entry " + std::to_string(entry) + " actualBits=" + std::to_string(std::bit_cast<std::uint32_t>(actual)) + " expectedBits=" + std::to_string(expected));
	}
}

static void TestNativeSurfaces(const char* path) {
	std::ifstream input(path, std::ios::binary);
	Require(input.is_open() && ReadWord(input) == 0x3153424d, "native surface vector requires MBS1 header");
	const auto records = ReadWord(input);
	Require(records == 40, "native surface vector inventory changed");
	std::size_t compared = 0;
	std::size_t exact_scales = 0;
	float maximum_scale_error = 0;
	for (std::uint32_t record = 0; record < records; ++record) {
		const auto degree = ReadWord(input);
		const auto count = ReadWord(input);
		const auto points = ReadWord(input);
		Require(degree == 0x44 || degree == 0x35 || degree == 0x53 || degree == 0x36 || degree == 0x63, "native surface degree differs from recovered inventory");
		Require(count == ((degree & 15) + 1) * ((degree >> 4) + 1) && count <= 28 && points == 9, "native surface vector exceeds exact bounded control/point inventory");
		sgre::resource::MotionBezierLattice lattice;
		lattice.EncodedDegree = static_cast<std::uint8_t>(degree);
		lattice.Controls.reserve(count);
		for (std::uint32_t index = 0; index < count; ++index) {
			lattice.Controls.push_back(ReadPoint(input));
		}
		for (std::uint32_t index = 0; index < points; ++index) {
			const auto coordinate = ReadPoint(input);
			const auto actual = sgre::scene::EvaluateMotionBezierLattice(lattice, coordinate);
			RequireWord(actual.X, ReadWord(input), "direct point X", record, index);
			RequireWord(actual.Y, ReadWord(input), "direct point Y", record, index);
			compared += 2;
		}
		sgre::scene::MotionDeformationWindow window;
		window.Left = ReadFloat(input);
		window.Top = ReadFloat(input);
		window.Right = ReadFloat(input);
		window.Bottom = ReadFloat(input);
		sgre::scene::MotionMeshSubdivision subdivision;
		subdivision.X = ReadWord(input);
		subdivision.Y = ReadWord(input);
		const auto grid_count = ReadWord(input);
		Require(subdivision.X == 3 && subdivision.Y == 2 && grid_count == 12, "native cached-grid vector exceeds exact authored subdivision");
		std::array<sgre::resource::MotionPoint, 12> grid{};
		sgre::scene::EvaluateMotionBezierLatticeGrid(lattice, subdivision, window, grid);
		for (std::size_t index = 0; index < grid.size(); ++index) {
			RequireWord(grid[index].X, ReadWord(input), "cached grid X", record, index);
			RequireWord(grid[index].Y, ReadWord(input), "cached grid Y", record, index);
			compared += 2;
		}
		const auto scale = sgre::scene::MeasureMotionBezierLatticeScale(lattice);
		const auto scale_word = ReadWord(input);
		const auto expected_scale = std::bit_cast<float>(scale_word);
		const auto scale_error = std::abs(scale - expected_scale) / std::max(1.0f, std::abs(expected_scale));
		// Preserve the existing affine acceptance: platform libm replaces original CRT trigonometry.
		Require(std::isfinite(scale) && scale_error <= 8.0f * std::numeric_limits<float>::epsilon(), "fitted scale differs in record " + std::to_string(record));
		exact_scales += std::bit_cast<std::uint32_t>(scale) == scale_word ? 1 : 0;
		maximum_scale_error = std::max(maximum_scale_error, scale_error);
		++compared;
	}
	Require(input.peek() == std::char_traits<char>::eof(), "native surface vector has trailing bytes");
	std::cout << "Native general surfaces: " << records << " lattices/360 direct points/480 cached-grid points/40 scales/" << compared << " compared words; exact scale words=" << exact_scales << " maximum scale relative/absolute error=" << maximum_scale_error << '\n';
}

int main(int argc, char** argv) {
	try {
		Require(argc == 2, "usage: motion_bezier_surface56_tests native-vectors.bin");
		TestNativeSurfaces(argv[1]);
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
