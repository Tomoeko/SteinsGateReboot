#include "sgre/scene/motion_scissor.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

using sgre::scene::MotionBounds;
using sgre::scene::MotionScissorMatrix;
using sgre::scene::MotionScissorViewport;
using sgre::scene::PrepareMotionScissorUpdate;
using sgre::scene::TransformMotionScissorBounds;

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

	throw std::runtime_error("expected scissor preparation failure");
}

static MotionBounds Bounds(float minimum_x, float minimum_y, float maximum_x, float maximum_y) {
	MotionBounds bounds;
	bounds.MinimumX = minimum_x;
	bounds.MinimumY = minimum_y;
	bounds.MaximumX = maximum_x;
	bounds.MaximumY = maximum_y;

	return bounds;
}

static MotionScissorViewport Viewport(std::int32_t width, std::int32_t height) {
	MotionScissorViewport viewport;
	viewport.Width = width;
	viewport.Height = height;

	return viewport;
}

static void TestFourCornersAndOddViewport() {
	const MotionScissorMatrix identity;
	const auto odd = TransformMotionScissorBounds(Bounds(-1, -1, 1, 1), identity, identity, Viewport(5, 3));
	Require(odd.MinimumX == -0.5f && odd.MaximumX == 4.5f && odd.MinimumY == 0 && odd.MaximumY == 2, "odd viewport dimensions lost native integer-half mapping");
	MotionScissorMatrix rotation;
	rotation.Coefficients[0] = 0;
	rotation.Coefficients[1] = -1;
	rotation.Coefficients[4] = 1;
	rotation.Coefficients[5] = 0;
	const auto rotated = TransformMotionScissorBounds(Bounds(-1, -2, 3, 4), identity, rotation, Viewport(100, 80));
	Require(rotated.MinimumX == -150 && rotated.MaximumX == 150 && rotated.MinimumY == -80 && rotated.MaximumY == 80, "rectangle transform failed to include all rotated corners");
	const auto reversed = TransformMotionScissorBounds(Bounds(3, 4, -1, -2), identity, rotation, Viewport(100, 80));
	Require(reversed.MinimumX == rotated.MinimumX && reversed.MinimumY == rotated.MinimumY && reversed.MaximumX == rotated.MaximumX && reversed.MaximumY == rotated.MaximumY, "reversed input bounds changed the native four-corner envelope");
}

static void TestAffineCompositionAndPreflight() {
	MotionScissorMatrix view;
	view.Coefficients[0] = 2;
	view.Coefficients[5] = 3;
	MotionScissorMatrix model;
	model.Coefficients[3] = 0.25f;
	model.Coefficients[7] = -0.5f;
	const auto transformed = TransformMotionScissorBounds(Bounds(0, 0, 0, 0), view, model, Viewport(100, 80));
	Require(transformed.MinimumX == 75 && transformed.MaximumX == 75 && transformed.MinimumY == 100 && transformed.MaximumY == 100, "view/model affine translation order changed");
	RequireFailure([&] {
		static_cast<void>(TransformMotionScissorBounds(Bounds(0, 0, 0, 0), view, model, Viewport(0, 80)));
	},
	    "positive physical");
	RequireFailure([&] {
		static_cast<void>(TransformMotionScissorBounds(Bounds(0, 0, 0, 0), view, model, Viewport(100, -1)));
	},
	    "positive physical");
	RequireFailure([&] {
		static_cast<void>(TransformMotionScissorBounds(Bounds(std::numeric_limits<float>::quiet_NaN(), 0, 0, 0), view, model, Viewport(100, 80)));
	},
	    "finite rectangle");
	model.Coefficients[10] = std::numeric_limits<float>::infinity();
	RequireFailure([&] {
		static_cast<void>(TransformMotionScissorBounds(Bounds(0, 0, 0, 0), view, model, Viewport(100, 80)));
	},
	    "finite affine");
	model.Coefficients[10] = 1;
	model.Coefficients[0] = std::numeric_limits<float>::max();
	RequireFailure([&] {
		static_cast<void>(TransformMotionScissorBounds(Bounds(0, 0, 0, 0), view, model, Viewport(100, 80)));
	},
	    "finite affine");
}

static void TestRoundedCacheAndRawSubmission() {
	const auto initial = PrepareMotionScissorUpdate(Bounds(1.9f, -1.9f, 9.9f, -9.9f), Bounds(0, 0, 0, 0));
	Require(initial.Submission && initial.Submission->MinimumX == 1 && initial.Submission->MinimumY == -1 && initial.Submission->MaximumX == 9 && initial.Submission->MaximumY == -9, "scissor submission used rounded coordinates instead of truncating raw values");
	Require(initial.Rounded.MinimumX == 2 && initial.Rounded.MinimumY == -2 && initial.Rounded.MaximumX == 10 && initial.Rounded.MaximumY == -10, "scissor cache did not round ties away from zero");
	const auto suppressed = PrepareMotionScissorUpdate(Bounds(2.1f, -2.1f, 10.1f, -10.1f), initial.Rounded);
	Require(!suppressed.Submission && suppressed.Raw.MinimumX == 2.1f && suppressed.Rounded.MaximumX == 10, "unchanged rounded cache failed to suppress a changed truncated rectangle");
	const auto retry = PrepareMotionScissorUpdate(initial.Raw, Bounds(0, 0, 0, 0));
	Require(retry.Submission && retry.Submission->MinimumX == 1, "failed submission could not be retried with the previous applied rounded cache");
	const auto ties = PrepareMotionScissorUpdate(Bounds(0.5f, -0.5f, 1.5f, -1.5f), Bounds(0, 0, 0, 0));
	Require(ties.Rounded.MinimumX == 1 && ties.Rounded.MinimumY == -1 && ties.Rounded.MaximumX == 2 && ties.Rounded.MaximumY == -2, "scissor rounding changed the native halfway rule");
	const auto zeros = PrepareMotionScissorUpdate(Bounds(-0.25f, 0.25f, -0.0f, 0), Bounds(0, -0.0f, 0, 0));
	Require(!zeros.Submission && std::bit_cast<std::uint32_t>(zeros.Rounded.MinimumX) == 0x80000000u && std::bit_cast<std::uint32_t>(zeros.Rounded.MaximumX) == 0x80000000u, "signed-zero rounding or ordered-zero cache equality changed");
}

static void TestNonfiniteAndIntegerIndefinite() {
	for (const auto bits : std::array<std::uint32_t, 4>{0x7fc00001u, 0xffc12345u, 0x7f800001u, 0xff800001u}) {
		const auto value = std::bit_cast<float>(bits);
		const auto update = PrepareMotionScissorUpdate(Bounds(value, 0, 1, 1), Bounds(value, 0, 1, 1));
		Require(update.Submission && std::bit_cast<std::uint32_t>(update.Rounded.MinimumX) == bits && update.Submission->MinimumX == std::numeric_limits<std::int32_t>::min(), "unordered scissor cache failed to preserve the raw NaN word and force integer-indefinite submission");
	}

	const auto infinity = std::numeric_limits<float>::infinity();
	const auto unchanged = PrepareMotionScissorUpdate(Bounds(infinity, -infinity, 1, 1), Bounds(infinity, -infinity, 1, 1));
	Require(!unchanged.Submission, "equal infinities failed the native ordered cache comparison");
	const auto overflow = PrepareMotionScissorUpdate(Bounds(2147483648.0f, -2147483648.0f, infinity, -infinity), Bounds(0, 0, 0, 0));
	Require(overflow.Submission && overflow.Submission->MinimumX == std::numeric_limits<std::int32_t>::min() && overflow.Submission->MinimumY == std::numeric_limits<std::int32_t>::min() && overflow.Submission->MaximumX == std::numeric_limits<std::int32_t>::min() && overflow.Submission->MaximumY == std::numeric_limits<std::int32_t>::min(), "scissor out-of-range conversion did not preserve CVTTSS2SI's indefinite word");
	const auto limits = PrepareMotionScissorUpdate(Bounds(2147483520.0f, -2147483520.0f, 8388607.5f, -8388607.5f), Bounds(0, 0, 0, 0));
	Require(limits.Submission && limits.Submission->MinimumX == 2147483520 && limits.Submission->MinimumY == -2147483520 && limits.Rounded.MaximumX == 8388608.0f && limits.Rounded.MaximumY == -8388608.0f, "scissor integer boundary or round-to-integral float threshold changed");
}

int main() {
	try {
		TestFourCornersAndOddViewport();
		TestAffineCompositionAndPreflight();
		TestRoundedCacheAndRawSubmission();
		TestNonfiniteAndIntegerIndefinite();
		std::cout << "motion scissor fixtures passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
