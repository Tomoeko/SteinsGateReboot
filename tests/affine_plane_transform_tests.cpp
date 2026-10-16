#include "sgre/scene/affine_plane_transform.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static std::array<float, 6> Fields(const sgre::scene::LayerAffineTransform& transform) {
	return {transform.XX, transform.YX, transform.XY, transform.YY, transform.TranslationX, transform.TranslationY};
}

static void CheckProjectedUnitDeltas() {
	sgre::scene::AffineMatrix matrix;
	matrix.Rows = {2, 3, 0, 7, -1, 4, 0, 11, 0, 0, 1, 0, 0, 0, 0, 1};
	Require(Fields(sgre::scene::PrepareAffinePlaneTransform(matrix)) == std::array<float, 6>{2, -1, 3, 4, 7, 11}, "ordinary matrix projects its original basis and translation");
	matrix.Rows[12] = 1;
	matrix.Rows[13] = 2;
	Require(Fields(sgre::scene::PrepareAffinePlaneTransform(matrix)) == std::array<float, 6>{-2.5f, -6, 10.0f / 3.0f - 7, -6, 7, 11}, "projective matrix derives each delta from its separately divided unit endpoint");
}

static void CheckInverseAndSingularFallback() {
	sgre::scene::LayerAffineTransform transform;
	transform.XX = 2;
	transform.XY = 3;
	transform.YX = -1;
	transform.YY = 4;
	transform.TranslationX = 7;
	transform.TranslationY = 11;
	const auto inverse = sgre::scene::InvertAffinePlaneTransform(transform);
	Require(Fields(inverse) == std::array<float, 6>{4.0f / 11, 1.0f / 11, -3.0f / 11, 2.0f / 11, -39.0f / 11, -1.0f / 11}, "native inverse retains its original off-diagonal offset selection and individual division");
	transform.YX = 4;
	transform.YY = 6;
	Require(Fields(sgre::scene::InvertAffinePlaneTransform(transform)) == std::array<float, 6>{1, 0, 0, 1, 0, 0}, "singular native inverse publishes identity despite nonzero translation");
}

static void CheckCancellationAndSignedZero() {
	sgre::scene::AffineMatrix matrix;
	matrix.Rows[3] = 16777216.0f;
	const auto projected = sgre::scene::PrepareAffinePlaneTransform(matrix);
	Require(projected.XX == 0 && projected.TranslationX == 16777216.0f, "unit endpoint cancellation preserves native binary32 behavior at a large translation");
	Require(Fields(sgre::scene::InvertAffinePlaneTransform(projected)) == std::array<float, 6>{1, 0, 0, 1, 0, 0}, "collapsed native unit axis follows the singular identity branch");
	sgre::scene::LayerAffineTransform diagonal;
	diagonal.XX = 2;
	diagonal.YY = 3;
	const auto inverse = sgre::scene::InvertAffinePlaneTransform(diagonal);
	Require(std::bit_cast<std::uint32_t>(inverse.XY) == 0x80000000 && std::bit_cast<std::uint32_t>(inverse.YX) == 0x80000000, "native sign-XOR preserves negative zero on off-diagonal inverse terms");
}

static std::uint32_t ReadWord(std::ifstream& stream) {
	std::uint32_t word = 0;
	for (std::uint32_t shift = 0; shift < 32; shift += 8) {
		const auto byte = stream.get();
		if (byte == std::char_traits<char>::eof()) {
			throw std::runtime_error("truncated plane transform stream");
		}
		word |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(byte)) << shift;
	}

	return word;
}

static void CompareNative(const char* plan_path, const char* expected_path) {
	std::ifstream plan(plan_path, std::ios::binary);
	std::ifstream expected(expected_path, std::ios::binary);
	Require(plan && expected && ReadWord(plan) == 0x50414753, "valid original plane transform vectors");
	const auto count = ReadWord(plan);
	Require(count <= 10000, "bounded original plane transform vector count");
	for (std::uint32_t index = 0; index < count; ++index) {
		sgre::scene::AffineMatrix matrix;
		for (auto& value : matrix.Rows) {
			value = std::bit_cast<float>(ReadWord(plan));
		}
		const auto projected = sgre::scene::PrepareAffinePlaneTransform(matrix);
		const auto inverse = sgre::scene::InvertAffinePlaneTransform(projected);
		for (const auto& transform : {projected, inverse}) {
			for (const auto value : Fields(transform)) {
				const auto reference = ReadWord(expected);
				if (std::bit_cast<std::uint32_t>(value) != reference) {
					throw std::runtime_error("plane transform native word mismatch at case " + std::to_string(index));
				}
			}
		}
	}
	Require(plan.peek() == std::char_traits<char>::eof() && expected.peek() == std::char_traits<char>::eof(), "plane transform streams consumed exactly");
	std::cout << "Native " << count << " plane transforms: " << count * 12 << " words, zero differences\n";
}

int main(int argc, char** argv) {
	try {
		Require(argc == 1 || argc == 3, "optional plane transform plan and expected streams");
		CheckProjectedUnitDeltas();
		CheckInverseAndSingularFallback();
		CheckCancellationAndSignedZero();
		std::cout << "Three affine plane transform fixture groups passed\n";
		if (argc == 3) {
			CompareNative(argv[1], argv[2]);
		}
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
