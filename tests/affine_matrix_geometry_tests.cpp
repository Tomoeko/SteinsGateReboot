#include "sgre/scene/affine_matrix_geometry.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
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

static void CheckIdentityAndPerspectiveWrites() {
	sgre::scene::AffineMatrix matrix;
	Require(sgre::scene::IsAffineMatrixIdentity(matrix), "initialized matrix is identity");
	matrix.Rows[1] = -0.0f;
	Require(sgre::scene::IsAffineMatrixIdentity(matrix), "native identity comparison accepts negative zero");
	matrix.Rows[1] = std::numeric_limits<float>::quiet_NaN();
	Require(!sgre::scene::IsAffineMatrixIdentity(matrix), "native identity comparison rejects unordered values");
	for (std::size_t index = 0; index < matrix.Rows.size(); ++index) {
		matrix.Rows[index] = static_cast<float>(index) + 0.25f;
	}
	const auto before = matrix.Rows;
	sgre::scene::InitializeAffinePerspective(matrix);
	for (std::size_t index = 0; index < 14; ++index) {
		Require(std::bit_cast<std::uint32_t>(matrix.Rows[index]) == std::bit_cast<std::uint32_t>(before[index]), "perspective initialization retains unrelated coefficients");
	}
	Require(matrix.Rows[14] == 1 && matrix.Rows[15] == 0, "perspective changes only homogeneous Z and W");
	Require(sgre::scene::TransformAffineX(matrix, 0, 0, 2) == 7.75f / 2.0f, "homogeneous transform consumes the retained matrix");
}

static void CheckIntegerTranslationAndHomogeneousPoints() {
	sgre::scene::AffineMatrix matrix;
	matrix.Rows[3] = 0.25f;
	matrix.Rows[7] = -0.5f;
	sgre::scene::TranslateAffineMatrixToInteger(matrix, 1.75f, -2.25f, 0.9f);
	Require(matrix.Rows[3] == 1.25f && matrix.Rows[7] == -2.5f && matrix.Rows[11] == 0 && matrix.Rows[15] == 1, "integer translation truncates each displacement before adding its old fractional column");
	sgre::scene::AffineMatrix invalid;
	sgre::scene::TranslateAffineMatrixToInteger(invalid, std::numeric_limits<float>::infinity(), 0, 0);
	const auto indefinite = static_cast<float>(std::numeric_limits<std::int32_t>::min());
	Require(invalid.Rows[3] == indefinite && invalid.Rows[7] == indefinite && invalid.Rows[11] == indefinite && invalid.Rows[15] == indefinite, "invalid CVTTSS2SI displacements use the native indefinite integer word");
	sgre::scene::AffineMatrix perspective;
	sgre::scene::InitializeAffinePerspective(perspective);
	Require(sgre::scene::TransformAffineX(perspective, 10, 20, 5) == 2 && sgre::scene::TransformAffineY(perspective, 10, 20, 5) == 4, "point coordinates divide by the actual fourth-row result");
}

static void CheckCornerOrder() {
	sgre::scene::AffineMatrix matrix;
	matrix.Rows = {2, 3, 0, 7, -1, 4, 0, 11, 0, 0, 1, 0, 0, 0, 0, 1};
	Require(sgre::scene::TransformAffineAreaX(matrix, 5, 6) == std::array<float, 4>{7, 17, 25, 35}, "X area retains origin, width, height and combined corners");
	Require(sgre::scene::TransformAffineAreaY(matrix, 5, 6) == std::array<float, 4>{11, 6, 35, 30}, "Y area is an ordered projection rather than sorted bounds");
	matrix.Rows[12] = 1;
	matrix.Rows[13] = 2;
	Require(sgre::scene::TransformAffineAreaX(matrix, 5, 6) == std::array<float, 4>{7, 17.0f / 6.0f, 25.0f / 13.0f, 35.0f / 18.0f}, "each area corner uses its own homogeneous denominator");
}

int main() {
	try {
		CheckIdentityAndPerspectiveWrites();
		CheckIntegerTranslationAndHomogeneousPoints();
		CheckCornerOrder();
		std::cout << "Three affine matrix geometry fixture groups passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
