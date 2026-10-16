#include "sgre/scene/motion_bezier_inverse.hpp"
#include "sgre/scene/motion_bezier_lattice.hpp"

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

template <typename Operation>
static void RequireFailure(Operation operation) {
	try {
		operation();
	} catch (const std::runtime_error&) {
		return;
	}
	throw std::runtime_error("unsupported or malformed Bezier input was accepted");
}

static std::uint32_t ReadWord(std::istream& input) {
	std::array<char, 4> bytes{};
	input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
	Require(static_cast<bool>(input), "native degree5/6 vector is truncated");
	return static_cast<std::uint8_t>(bytes[0]) | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[1])) << 8) | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[2])) << 16) | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[3])) << 24);
}

static void TestNativeBasisAndInverse(const char* path) {
	std::ifstream input(path, std::ios::binary);
	Require(input.is_open() && ReadWord(input) == 0x3148424d, "native degree5/6 input requires MBH1 header");
	const auto bases = ReadWord(input);
	const auto grids = ReadWord(input);
	Require(bases == 54 && grids == 128, "native degree5/6 fixture case inventory changed");
	std::size_t compared = 0;
	for (std::uint32_t index = 0; index < bases; ++index) {
		const auto degree = ReadWord(input);
		Require(degree >= 1 && degree <= 6, "native basis degree exceeds exact recovered domain");
		const auto coordinate = std::bit_cast<float>(ReadWord(input));
		const auto actual = sgre::scene::MotionBezierBasisWeights(degree, coordinate);
		for (std::uint32_t entry = 0; entry <= degree; ++entry) {
			const auto expected = ReadWord(input);
			Require(std::bit_cast<std::uint32_t>(actual[entry]) == expected, "native cached basis word differs in case " + std::to_string(index) + " entry " + std::to_string(entry));
			++compared;
		}
		for (std::size_t entry = degree + 1; entry < actual.size(); ++entry) {
			Require(actual[entry] == 0, "unused bounded basis lanes must stay zero");
		}
	}
	for (std::uint32_t index = 0; index < grids; ++index) {
		const auto degree = ReadWord(input);
		const auto count = ReadWord(input);
		Require((degree & 15) >= 1 && (degree & 15) <= 6 && (degree >> 4) >= 1 && (degree >> 4) <= 6 && ((degree & 15) <= 2 || (degree >> 4) <= 2), "native high-degree grid identity changed");
		const auto width = (degree & 15) + 1;
		const auto height = (degree >> 4) + 1;
		Require(count == width * height && count <= 28, "native high-degree grid count differs from its exact bounded axes");
		std::array<float, 7> horizontal{};
		std::array<float, 7> vertical{};
		for (auto& knot : std::span(horizontal).first(width)) {
			knot = std::bit_cast<float>(ReadWord(input));
		}
		for (auto& knot : std::span(vertical).first(height)) {
			knot = std::bit_cast<float>(ReadWord(input));
		}
		std::vector<sgre::resource::MotionPoint> points(count);
		for (auto& point : points) {
			point.X = std::bit_cast<float>(ReadWord(input));
			point.Y = std::bit_cast<float>(ReadWord(input));
		}
		sgre::scene::ConvertMotionBezierSamplesToControls(points, static_cast<std::uint8_t>(degree), std::span(horizontal).first(width), std::span(vertical).first(height));
		for (std::size_t entry = 0; entry < points.size(); ++entry) {
			const auto x = ReadWord(input);
			const auto y = ReadWord(input);
			Require(std::bit_cast<std::uint32_t>(points[entry].X) == x && std::bit_cast<std::uint32_t>(points[entry].Y) == y, "native high-degree inverse word differs in grid " + std::to_string(index) + " point " + std::to_string(entry));
			compared += 2;
		}
	}
	Require(input.peek() == std::char_traits<char>::eof(), "native high-degree input has trailing bytes");
	std::cout << "Native mixed linear/quadratic inverse: " << bases << " bases/" << grids << " grids/" << compared << " exact words\n";
}

static void TestIdentityAndAffineSurfaces() {
	for (const auto degree : {std::uint8_t{0x44}, std::uint8_t{0x35}, std::uint8_t{0x53}, std::uint8_t{0x36}, std::uint8_t{0x63}}) {
		auto lattice = sgre::scene::IdentityMotionBezierLattice(degree);
		for (auto& point : lattice.Controls) {
			point.X = point.X * 2 + 3;
			point.Y = point.Y * 3 - 7;
		}
		for (const auto input : {sgre::resource::MotionPoint{0, 0}, sgre::resource::MotionPoint{1, 1}, sgre::resource::MotionPoint{0.2f, 0.7f}, sgre::resource::MotionPoint{-0.125f, 1.125f}}) {
			const auto actual = sgre::scene::EvaluateMotionBezierLattice(lattice, input);
			Require(std::abs(actual.X - (input.X * 2 + 3)) < 0.00002f && std::abs(actual.Y - (input.Y * 3 - 7)) < 0.00002f, "genuine general-degree row/column point evaluation must preserve affine controls and extrapolation");
		}
		const auto scale = sgre::scene::MeasureMotionBezierLatticeScale(lattice);
		Require(std::abs(scale - std::sqrt(6.5f)) < 0.00001f, "general-degree scale must fit actual controls against the same identity lattice");
		sgre::scene::MotionMeshSubdivision subdivision;
		subdivision.X = 2;
		subdivision.Y = 2;
		const sgre::scene::MotionDeformationWindow window{0, 0, 1, 1};
		std::array<sgre::resource::MotionPoint, 9> points{};
		sgre::scene::EvaluateMotionBezierLatticeGrid(lattice, subdivision, window, points);
		for (std::size_t index = 0; index < points.size(); ++index) {
			const auto x = static_cast<float>(index % 3) / 2;
			const auto y = static_cast<float>(index / 3) / 2;
			Require(std::abs(points[index].X - (x * 2 + 3)) < 0.00002f && std::abs(points[index].Y - (y * 3 - 7)) < 0.00002f, "cached general-degree surface must preserve actual grid positions");
		}
	}
}

static void TestMalformedInputs() {
	for (const auto degree : {0u, 7u, 15u}) {
		RequireFailure([&] {
			static_cast<void>(sgre::scene::MotionBezierBasisWeights(degree, 0.5f));
		});
	}
	RequireFailure([] {
		static_cast<void>(sgre::scene::MotionBezierBasisWeights(5, std::numeric_limits<float>::quiet_NaN()));
	});
	auto points = sgre::scene::IdentityMotionBezierLattice(0x63).Controls;
	const auto original = points;
	const std::array<float, 7> repeated{0, 0.125f, 0.25f, 0.5f, 0.5f, 0.75f, 1};
	RequireFailure([&] {
		sgre::scene::ConvertMotionBezierSamplesToControls(points, 0x63, {}, repeated);
	});
	for (std::size_t index = 0; index < points.size(); ++index) {
		Require(points[index].X == original[index].X && points[index].Y == original[index].Y, "malformed knots must reject before caller controls change");
	}
}

int main(int argc, char** argv) {
	try {
		Require(argc <= 2, "usage: motion_bezier_inverse12_tests [native-vectors.bin]");
		TestIdentityAndAffineSurfaces();
		TestMalformedInputs();
		if (argc == 2) {
			TestNativeBasisAndInverse(argv[1]);
		}
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
