#include "sgre/scene/affine_matrix.hpp"

#include <array>
#include <bit>
#include <cmath>
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

static void CheckNativeGeometry() {
	sgre::scene::AffineMatrix matrix;
	sgre::scene::TranslateAffineMatrix(matrix, 10, 20, 30);
	sgre::scene::ScaleAffineMatrix(matrix, 2, 3, 4);
	Require(matrix.Rows[3] == 10 && matrix.Rows[7] == 20 && matrix.Rows[11] == 30 && matrix.Rows[0] == 2 && matrix.Rows[5] == 3 && matrix.Rows[10] == 4, "native translation precedes scaling without scaling its translation column");
	matrix.Rows[12] = 5;
	matrix.Rows[13] = 6;
	matrix.Rows[14] = 7;
	sgre::scene::ScaleAffineMatrix(matrix, 8, 9, 10);
	Require(matrix.Rows[12] == 5 && matrix.Rows[13] == 6 && matrix.Rows[14] == 7, "native scaling retains the entire fourth row");
	sgre::scene::FlipAffineMatrix(matrix, sgre::scene::AffineAxis::Y);
	Require(matrix.Rows[13] == -6 && matrix.Rows[5] == -27, "native flipping includes the fourth row");

	sgre::scene::AffineMatrix slanted;
	sgre::scene::SlantAffineMatrix(slanted, 2, 3);
	Require(slanted.Rows[0] == 1 && slanted.Rows[1] == 2 && slanted.Rows[4] == 3 && slanted.Rows[5] == 1, "slant uses old row coefficients for both updates");
}

static void CheckMultiplyOrderAndAlias() {
	sgre::scene::AffineMatrix translation;
	sgre::scene::TranslateAffineMatrix(translation, 10, 20);
	sgre::scene::AffineMatrix scaling;
	sgre::scene::ScaleAffineMatrix(scaling, 2, 3);
	const auto first = sgre::scene::MultiplyAffineMatrices(translation, scaling);
	const auto reversed = sgre::scene::MultiplyAffineMatrices(scaling, translation);
	Require(first.Rows[3] == 10 && first.Rows[7] == 20 && reversed.Rows[3] == 20 && reversed.Rows[7] == 60, "matrix composition preserves recovered noncommutative order");
	translation = sgre::scene::MultiplyAffineMatrices(translation, translation);
	Require(translation.Rows[3] == 20 && translation.Rows[7] == 40 && translation.Rows[15] == 1, "self multiplication snapshots both original operands");
}

static void CheckLayerAndImageOrder() {
	sgre::scene::AffineLayerParameterState parameters;
	parameters.PositionX = 10;
	parameters.PositionY = 20;
	parameters.PositionZ = 123456;
	parameters.CenterX = 2;
	parameters.CenterY = 3;
	parameters.ZoomX = 2;
	parameters.ZoomY = 3;
	parameters.SlantX = 1;
	parameters.FlipY = true;
	parameters.OffsetX = 4;
	parameters.OffsetY = 5;
	parameters.OffsetZ = 9876;
	parameters.OriginZ = -3456;
	sgre::scene::AffineMatrix matrix;
	sgre::scene::ApplyAffineLayerMatrix(matrix, parameters, sgre::scene::LayerPoint{-3, 7});
	Require(matrix.Rows[0] == 2 && matrix.Rows[1] == -2 && matrix.Rows[3] == 3 && matrix.Rows[4] == 0 && matrix.Rows[5] == -3 && matrix.Rows[7] == 6, "linked position, pivot scale, slant, flip and offset preserve native call order");
	Require(matrix.Rows[11] == 0 && matrix.Rows[10] == 1, "calcMatrix leaves depth policy to the separate outer task");
	parameters.ImageX = 4;
	parameters.ImageY = 5;
	parameters.ImageZoom = 2;
	sgre::scene::AffineMatrix image;
	sgre::scene::ApplyAffineImageMatrix(image, parameters);
	Require(image.Rows[0] == 2 && image.Rows[5] == 2 && image.Rows[3] == 4 && image.Rows[7] == 5, "image transform has separate translation and uniform scale");
}

static void CheckNativeRotation() {
	sgre::scene::AffineMatrix matrix;
	sgre::scene::RotateAffineMatrix(matrix, 90);
	const std::array<std::uint32_t, 16> native{0xb33bbd2e, 0xbf800000, 0, 0, 0x3f800000, 0xb33bbd2e, 0, 0, 0, 0x80000000, 0x3f800000, 0, 0, 0, 0, 0x3f800000};
	for (std::size_t index = 0; index < native.size(); ++index) {
		Require(std::bit_cast<std::uint32_t>(matrix.Rows[index]) == native[index], "unchanged SSE2 native90-degree rotation words");
	}
}

static void CheckUnrecoveredRotationDomain() {
	sgre::scene::AffineMatrix matrix;
	const auto initial = matrix.Rows;
	try {
		sgre::scene::RotateAffineMatrix(matrix, std::numeric_limits<float>::infinity());
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find("parity is not recovered") != std::string_view::npos && matrix.Rows == initial, "unknown rotation domain fails before mutating the matrix");

		return;
	}

	throw std::runtime_error("nonfinite native rotation remains explicit unsupported behavior");
}

int main() {
	try {
		CheckNativeGeometry();
		CheckMultiplyOrderAndAlias();
		CheckLayerAndImageOrder();
		CheckNativeRotation();
		CheckUnrecoveredRotationDomain();
		std::cout << "Five affine matrix fixture groups passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
