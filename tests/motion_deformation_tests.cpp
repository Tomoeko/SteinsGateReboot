#include "sgre/scene/motion_deformation.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <functional>
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

static void RequireFailure(const std::function<void()>& operation) {
	try {
		operation();
	} catch (const std::runtime_error&) {
		return;
	}
	throw std::runtime_error("invalid deformation was accepted");
}

static void TestNativePointVector() {
	// Synthetic controls and outputs evaluated by original 0x14053d150 instructions.
	const std::array<std::array<std::uint32_t, 8>, 4> controls = {{
	    {0x3e621d5a, 0xbe78d324, 0x3feaa455, 0xbfa617b9, 0x3f9e03c1, 0xbfa17cd1, 0x3f8029b5, 0x3f5f2f57}, // Control row 0
	    {0xbea40369, 0x3eb85977, 0xbfc8610c, 0xbfc00ed1, 0x3f8a18f5, 0xbf81abce, 0x3f8249bc, 0x3fa3b6eb}, // Control row 1
	    {0x3f7d47a3, 0xbe79bfa3, 0x3f3596a4, 0xbffa7e36, 0x3ed09286, 0x3fc0628d, 0x3f370720, 0xbfcaec06}, // Control row 2
	    {0x3fdfaddc, 0xbf864436, 0xbfb3e87e, 0xbf70a281, 0x3f6e4bd8, 0xbfd450a5, 0x3f4de678, 0x3faf2b33}, // Control row 3
	}};
	sgre::resource::MotionBezierPatch patch;
	for (std::size_t row = 0; row < 4; ++row) {
		for (std::size_t column = 0; column < 4; ++column) {
			patch.Controls[row * 4 + column].X = std::bit_cast<float>(controls[row][column * 2]);
			patch.Controls[row * 4 + column].Y = std::bit_cast<float>(controls[row][column * 2 + 1]);
		}
	}
	sgre::resource::MotionPoint coordinate;
	coordinate.X = std::bit_cast<float>(std::uint32_t{0xbde0c30e});
	coordinate.Y = std::bit_cast<float>(std::uint32_t{0x3e9e42e7});
	const auto result = sgre::scene::EvaluateMotionBezierPatch(patch, coordinate);
	Require(std::bit_cast<std::uint32_t>(result.X) == 0x3e8e8037 && std::bit_cast<std::uint32_t>(result.Y) == 0x3f15f054, "native bicubic golden output changed");
}

static void TestPatchInterpolation() {
	const auto identity = sgre::scene::IdentityMotionBezierPatch();
	auto shifted = identity;
	for (auto& point : shifted.Controls) {
		point.X += 1;
		point.Y -= 2;
	}
	const auto half = sgre::scene::InterpolateMotionBezierPatch(identity, shifted, 0.5f);
	Require(half->Controls[0].X == 0.5f && half->Controls[0].Y == -1 && half->Controls[15].X == 1.5f, "patch lane interpolation changed");
	const auto missing = sgre::scene::InterpolateMotionBezierPatch(std::nullopt, shifted, 0.5f);
	Require(missing->Controls[5].X == half->Controls[5].X, "missing patch did not use native identity controls");
	Require(!sgre::scene::InterpolateMotionBezierPatch(std::nullopt, std::nullopt, 0.5f), "two absent patches unexpectedly produced a deformation");
	const auto near_first = sgre::scene::InterpolateMotionBezierPatch(identity, shifted, 0x1p-24f);
	const auto near_last = sgre::scene::InterpolateMotionBezierPatch(identity, shifted, 1.0f - 0x1p-24f);
	Require(near_first->Controls[0].X == 0 && near_last->Controls[0].X == 1, "native epsilon endpoint copies changed");
	const auto boundary = sgre::scene::InterpolateMotionBezierPatch(identity, shifted, 0x1p-23f);
	Require(boundary->Controls[0].X == 0x1p-23f, "native endpoint tolerance became inclusive");
	sgre::resource::EasingCurve easing;
	easing.Segments = {{{0, 0, 0}, {1, 0.5f, 0}}};
	const auto eased = sgre::scene::InterpolateMotionBezierPatch(identity, shifted, 1, &easing);
	Require(eased->Controls[0].X == 0.5f, "mesh easing was not applied before control interpolation");
	sgre::resource::MotionPoint corner;
	corner.X = 1;
	corner.Y = 1;
	const auto result = sgre::scene::EvaluateMotionBezierPatch(identity, corner);
	Require(result.X == 1 && result.Y == 1, "identity patch terminal corner changed");
	RequireFailure([&]() {
		static_cast<void>(sgre::scene::InterpolateMotionBezierPatch(identity, shifted, std::numeric_limits<float>::infinity()));
	});
	shifted.Controls[0].X = std::numeric_limits<float>::quiet_NaN();
	RequireFailure([&]() {
		static_cast<void>(sgre::scene::EvaluateMotionBezierPatch(shifted, corner));
	});
	corner.Y = std::numeric_limits<float>::infinity();
	RequireFailure([&]() {
		static_cast<void>(sgre::scene::EvaluateMotionBezierPatch(identity, corner));
	});
}

static void TestNativeMatrixAndCombination() {
	// Native 0x140526150, 0x14053c610, and 0x14053c350 evaluated these synthetic inputs.
	const std::array<std::uint32_t, 6> coefficients = {0x3ffd0952, 0xbe5362f5, 0x42acbb5f, 0xbd9b28b1, 0x3fcb67b8, 0xc2ab8682};
	const std::array<std::uint32_t, 6> inverse_words = {0x3f0225a7, 0x3d874100, 0xc218f93d, 0x3cc68de7, 0x3f21e73a, 0x4250957f};
	sgre::resource::MotionMeshMatrix matrix;
	matrix.XX = std::bit_cast<float>(coefficients[0]);
	matrix.XY = std::bit_cast<float>(coefficients[1]);
	matrix.TranslationX = std::bit_cast<float>(coefficients[2]);
	matrix.YX = std::bit_cast<float>(coefficients[3]);
	matrix.YY = std::bit_cast<float>(coefficients[4]);
	matrix.TranslationY = std::bit_cast<float>(coefficients[5]);
	const auto inverse = sgre::scene::InverseMotionMeshMatrix(matrix);
	const std::array<float, 6> actual = {inverse.XX, inverse.XY, inverse.TranslationX, inverse.YX, inverse.YY, inverse.TranslationY};
	for (std::size_t index = 0; index < actual.size(); ++index) {
		Require(std::bit_cast<std::uint32_t>(actual[index]) == inverse_words[index], "native deformation inverse golden changed");
	}
	sgre::resource::MotionPoint point;
	point.X = std::bit_cast<float>(std::uint32_t{0x3f4de71a});
	point.Y = std::bit_cast<float>(std::uint32_t{0x3c9f5692});
	const auto transformed = sgre::scene::TransformMotionMeshPoint(matrix, point);
	Require(std::bit_cast<std::uint32_t>(transformed.X) == 0x42afe764 && std::bit_cast<std::uint32_t>(transformed.Y) == 0xc2ab95e2, "native deformation affine golden changed");
	auto first = sgre::scene::IdentityMotionBezierPatch();
	auto second = first;
	first.Controls[0].X = std::bit_cast<float>(std::uint32_t{0xbf959f30});
	first.Controls[0].Y = std::bit_cast<float>(std::uint32_t{0xbfc2f81f});
	second.Controls[0].X = std::bit_cast<float>(std::uint32_t{0x3f93160a});
	second.Controls[0].Y = std::bit_cast<float>(std::uint32_t{0xbf46662e});
	const auto combined = sgre::scene::CombineMotionBezierPatches(first, second);
	Require(std::bit_cast<std::uint32_t>(combined->Controls[0].X) == 0xbca24980 && std::bit_cast<std::uint32_t>(combined->Controls[0].Y) == 0xc013159b, "native same-degree deformation combination changed");
	Require(!sgre::scene::CombineMotionBezierPatches(std::nullopt, std::nullopt), "absent patch combination produced a warp");
	const auto copied = sgre::scene::CombineMotionBezierPatches(std::nullopt, second);
	Require(copied->Controls[0].X == second.Controls[0].X, "empty patch combination failed to preserve its operand");
	const auto transformed_patch = sgre::scene::TransformMotionBezierPatch(first, matrix);
	Require(std::isfinite(transformed_patch.Controls[15].X), "patch control transform failed");
	matrix.XX = 0;
	matrix.XY = 0;
	RequireFailure([&]() {
		static_cast<void>(sgre::scene::InverseMotionMeshMatrix(matrix));
	});
}

static void TestNativeDeformationSpaceApplication() {
	// Original 0x140526150, 0x140554020, and 0x140554850 executed this complete synthetic warp.
	const std::array<std::uint32_t, 6> coefficients = {0x3f8301d8, 0xbe5b3d84, 0xc0dbd7a0, 0xbe3fc91a, 0x3f0b64d0, 0x3ef6f5ff};
	const std::array<std::array<std::uint32_t, 8>, 4> controls = {{
	    {0xbfe90f59, 0x3fb540dc, 0x3d382065, 0xbfa8f33c, 0xbfa9b2a7, 0xbfb79f9f, 0xbe1a91df, 0xbfbe81e5},
	    {0xbf1209d3, 0x3fe6584a, 0x3f998f31, 0xbf9ab291, 0x3ffb7a23, 0xbeb3302b, 0xbffd0a1f, 0xbd82abd6},
	    {0x3f9b1446, 0xbf53f85d, 0xbfb53d7d, 0x3fafd433, 0xbf82d7b7, 0x3f6504b9, 0xbe54c6de, 0xbeb08d15},
	    {0xbf57ba60, 0xbfb73384, 0x3ea1f8c1, 0x3fabec0b, 0xbed666d4, 0xbf9fd3d9, 0xbeac53b5, 0xbf85b2b0},
	}};
	const std::array<std::uint32_t, 6> input = {0xc022c5c5, 0x401cb33e, 0x3fa8f010, 0x401c1d40, 0x401d7a11, 0x406caa85};
	const std::array<std::uint32_t, 6> expected = {0xc80e6d7e, 0x46993710, 0xc999427c, 0xc7cffc40, 0xcaccef34, 0xc9857b20};
	sgre::resource::MotionMeshMatrix space;
	space.XX = std::bit_cast<float>(coefficients[0]);
	space.XY = std::bit_cast<float>(coefficients[1]);
	space.TranslationX = std::bit_cast<float>(coefficients[2]);
	space.YX = std::bit_cast<float>(coefficients[3]);
	space.YY = std::bit_cast<float>(coefficients[4]);
	space.TranslationY = std::bit_cast<float>(coefficients[5]);
	sgre::resource::MotionBezierPatch patch;
	for (std::size_t row = 0; row < 4; ++row) {
		for (std::size_t column = 0; column < 4; ++column) {
			patch.Controls[row * 4 + column].X = std::bit_cast<float>(controls[row][column * 2]);
			patch.Controls[row * 4 + column].Y = std::bit_cast<float>(controls[row][column * 2 + 1]);
		}
	}

	auto deformation = sgre::scene::PrepareMotionDeformation(patch, space);
	std::array<sgre::resource::MotionPoint, 3> points;
	for (std::size_t index = 0; index < points.size(); ++index) {
		points[index].X = std::bit_cast<float>(input[index * 2]);
		points[index].Y = std::bit_cast<float>(input[index * 2 + 1]);
	}

	sgre::scene::ApplyMotionDeformation(deformation, points);
	for (std::size_t index = 0; index < points.size(); ++index) {
		Require(std::bit_cast<std::uint32_t>(points[index].X) == expected[index * 2] && std::bit_cast<std::uint32_t>(points[index].Y) == expected[index * 2 + 1], "native world-space deformation output changed");
	}

	const auto first_before = points[0];
	points[2].X = std::numeric_limits<float>::infinity();
	RequireFailure([&]() {
		sgre::scene::ApplyMotionDeformation(deformation, points);
	});
	Require(points[0].X == first_before.X && points[0].Y == first_before.Y, "invalid deformation input modified earlier positions before validation");
	deformation.WorldToPatch.XX = std::numeric_limits<float>::quiet_NaN();
	RequireFailure([&]() {
		sgre::scene::ApplyMotionDeformation(deformation, {});
	});
}

int main() {
	try {
		TestNativePointVector();
		TestPatchInterpolation();
		TestNativeMatrixAndCombination();
		TestNativeDeformationSpaceApplication();
		std::cout << "Native bicubic point math, patch interpolation, identity defaults, and safety checks passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
