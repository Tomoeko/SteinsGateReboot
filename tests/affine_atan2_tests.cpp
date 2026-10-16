#include "sgre/scene/affine_atan2.hpp"

#include <bit>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static std::uint32_t AngleWord(float y, float x) {
	return std::bit_cast<std::uint32_t>(sgre::scene::PrepareAffineAngle(y, x));
}

static void CheckSignedAxes() {
	Require(AngleWord(0.0f, 1) == 0 && AngleWord(-0.0f, 1) == 0x80000000u, "positive-X axis preserves signed Y zero");
	Require(AngleWord(0.0f, -1) == 0x40490fdbu && AngleWord(-0.0f, -1) == 0xc0490fdbu, "negative-X axis publishes signed native pi");
	Require(AngleWord(1, 0) == 0x3fc90fdbu && AngleWord(-1, 0) == 0xbfc90fdbu, "vertical axes preserve signed native half-pi");
	Require(AngleWord(-0.0f, -0.0f) == 0xc0490fdbu && AngleWord(-0.0f, 0.0f) == 0x80000000u, "both-zero cases use native sign bits rather than ordered floating comparisons");
}

static void CheckQuadrantsAndHomogeneousRatio() {
	Require(AngleWord(1, 1) == 0x3f490fdbu && AngleWord(-1, 1) == 0xbf490fdbu, "equal-magnitude positive-X quadrants use native quarter-pi");
	Require(AngleWord(1, -1) == 0x4016cbe4u && AngleWord(-1, -1) == 0xc016cbe4u, "equal-magnitude negative-X quadrants reflect native three-quarter-pi");
	Require(AngleWord(1, 2) == AngleWord(128, 256), "native binary64 reduction preserves an exactly scaled input ratio");
	Require(sgre::scene::PrepareAffineAngle(1, 2) > 0.46f && sgre::scene::PrepareAffineAngle(1, 2) < 0.47f, "table/polynomial reduction preserves the ordinary geometric angle");
}

static void CheckFiniteExtremeRatios() {
	const auto smallest = std::bit_cast<float>(std::uint32_t{1});
	const auto largest = std::numeric_limits<float>::max();
	Require(AngleWord(smallest, largest) == 0 && AngleWord(-smallest, largest) == 0x80000000u, "extreme underflow ratios preserve signed zero");
	Require(AngleWord(largest, smallest) == 0x3fc90fdbu && AngleWord(smallest, -largest) == 0x40490fdbu, "extreme finite ratios use native quadrant constants");
	Require(AngleWord(std::numeric_limits<float>::min(), 4194304) == 2, "tiny positive-X ratios round to genuine binary32 subnormal output");
	Require(AngleWord(smallest, smallest) == 0x3f490fdbu, "subnormal inputs retain their ratio in binary64 conversion");
}

static void CheckUnsupportedNonfiniteInput() {
	try {
		static_cast<void>(sgre::scene::PrepareAffineAngle(std::numeric_limits<float>::infinity(), 1));
	} catch (const std::runtime_error& error) {
		Require(std::string(error.what()).find("finite") != std::string::npos, "unrecovered math-exception behavior has an explicit error");

		return;
	}

	throw std::runtime_error("unsupported infinite native angle was accepted");
}

static std::uint32_t ReadWord(std::ifstream& input) {
	std::uint32_t result = 0;
	for (std::uint32_t shift = 0; shift < 32; shift += 8) {
		const auto byte = input.get();
		if (byte == std::char_traits<char>::eof()) {
			throw std::runtime_error("truncated affine angle corpus");
		}

		result |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(byte)) << shift;
	}

	return result;
}

static void CompareNative(const char* plan_path, const char* expected_path) {
	std::ifstream plan(plan_path, std::ios::binary);
	std::ifstream expected(expected_path, std::ios::binary);
	Require(ReadWord(plan) == 0x41414753u, "invalid native angle corpus magic");
	const auto count = ReadWord(plan);
	Require(count <= 1000000, "native angle corpus exceeds its case limit");
	for (std::uint32_t index = 0; index < count; ++index) {
		const auto y = std::bit_cast<float>(ReadWord(plan));
		const auto x = std::bit_cast<float>(ReadWord(plan));
		const auto result = AngleWord(y, x);
		const auto native = ReadWord(expected);
		if (result != native) {
			throw std::runtime_error("native affine angle differs at case " + std::to_string(index) + ": y=" + std::to_string(std::bit_cast<std::uint32_t>(y)) + ", x=" + std::to_string(std::bit_cast<std::uint32_t>(x)) + ", expected=" + std::to_string(native) + ", got=" + std::to_string(result));
		}
	}

	Require(plan.get() == std::char_traits<char>::eof() && expected.get() == std::char_traits<char>::eof(), "affine angle corpus has trailing bytes");
	std::cout << "Native affine angle: " << count << " words, zero differences\n";
}

int main(int argc, char** argv) {
	try {
		CheckSignedAxes();
		CheckQuadrantsAndHomogeneousRatio();
		CheckFiniteExtremeRatios();
		CheckUnsupportedNonfiniteInput();
		std::cout << "Four affine angle groups passed\n";
		if (argc == 3) {
			CompareNative(argv[1], argv[2]);
		} else if (argc != 1) {
			throw std::runtime_error("affine_atan2_tests [NATIVE_PLAN NATIVE_EXPECTED]");
		}
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
