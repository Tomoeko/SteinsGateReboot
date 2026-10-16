#include "sgre/scene/motion_masked_blend.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static sgre::scene::MotionMaskedBlendInput PreparedInput(float width, float height, float scale_x, float scale_y, float amount) {
	sgre::scene::MotionMaskedBlendInput input;
	input.TargetBounds.MinimumX = 0;
	input.TargetBounds.MinimumY = 0;
	input.TargetBounds.MaximumX = width;
	input.TargetBounds.MaximumY = height;
	input.AxisScale.X = scale_x;
	input.AxisScale.Y = scale_y;
	input.Amount = amount;

	return input;
}

static void TestEqualAxisPairs() {
	std::array<sgre::scene::MotionMaskedBlendPass, 8> passes{};
	const auto count = sgre::scene::PrepareMotionMaskedBlendPasses(PreparedInput(100, 9, 1, 1, 0.18f), passes);
	Require(count == 4, "equal scales must use the larger extent for both axes");
	Require(passes[0].Axis == sgre::scene::MotionMaskedBlendAxis::Horizontal && passes[1].Axis == sgre::scene::MotionMaskedBlendAxis::Vertical && passes[2].Axis == sgre::scene::MotionMaskedBlendAxis::Horizontal && passes[3].Axis == sgre::scene::MotionMaskedBlendAxis::Vertical, "equal scales must interleave horizontal and vertical");
	Require(passes[0].Step == 9 && passes[1].Step == 9 && passes[2].Step == 9 && passes[3].Step == 9, "equal scales must restart the largest fitting step after every pair");
	Require(passes[0].BlendParameter == 4 && passes[1].BlendParameter == 9 && passes[2].BlendParameter == 4 && passes[3].BlendParameter == 9, "equal-scale native selectors differ");
	const auto residual = sgre::scene::PrepareMotionMaskedBlendPasses(PreparedInput(100, 80, 1, 1, 0.08f), passes);
	Require(residual == 4 && passes[0].Step == 6 && passes[2].Step == 2 && passes[1].BlendParameter == 8 && passes[3].BlendParameter == 6, "residual paired steps differ");
}

static void TestUnequalAxisScans() {
	std::array<sgre::scene::MotionMaskedBlendPass, 8> passes{};
	const auto count = sgre::scene::PrepareMotionMaskedBlendPasses(PreparedInput(100, 9, 1, 2, 0.18f), passes);
	Require(count == 4, "unequal scales must calculate separate radii");
	Require(passes[0].Step == 9 && passes[1].Step == 6 && passes[2].Step == 3 && passes[3].Step == 3, "unequal axis scan must visit lower steps before restarting");
	Require(passes[0].BlendParameter == 4 && passes[1].BlendParameter == 3 && passes[2].BlendParameter == 2 && passes[3].BlendParameter == 7, "unequal-scale native selectors differ");
	Require(passes[0].Axis == sgre::scene::MotionMaskedBlendAxis::Horizontal && passes[1].Axis == sgre::scene::MotionMaskedBlendAxis::Horizontal && passes[2].Axis == sgre::scene::MotionMaskedBlendAxis::Horizontal && passes[3].Axis == sgre::scene::MotionMaskedBlendAxis::Vertical, "all horizontal passes must precede vertical passes when scales differ");
	const auto repeated = sgre::scene::PrepareMotionMaskedBlendPasses(PreparedInput(100, 9, 1, 0, 0.22f), passes);
	Require(repeated == 6 && passes[0].Step == 9 && passes[1].Step == 6 && passes[2].Step == 3 && passes[3].Step == 2 && passes[4].Step == 1 && passes[5].Step == 1, "unequal axis complete scan must subtract 21 before restarting");
}

static void TestRoundingAndNoPasses() {
	std::array<sgre::scene::MotionMaskedBlendPass, 2> passes{};
	Require(sgre::scene::PrepareMotionMaskedBlendPasses(PreparedInput(1, 1, 1, 1, 0.49999997f), passes) == 2, "float32 addition rounds the value just below one half to one");
	Require(sgre::scene::PrepareMotionMaskedBlendPasses(PreparedInput(1, 1, 1, 1, 0.4999999f), passes) == 0, "radius rounding threshold differs");
	Require(sgre::scene::PrepareMotionMaskedBlendPasses(PreparedInput(1, 1, 1, 1, -0.5f), passes) == 0, "nonpositive native integer radius must produce no passes");
	Require(sgre::scene::PrepareMotionMaskedBlendPasses(PreparedInput(100, 80, 0, -0.0f, 1), passes) == 0, "signed zero scales compare equal and produce no radius");
	Require(sgre::scene::PrepareMotionMaskedBlendPasses(PreparedInput(1e20f, 1e20f, 1, 1, 1e20f), passes) == 0, "overflowing finite intermediate must preserve the original invalid conversion's inert result");
	Require(sgre::scene::PrepareMotionMaskedBlendPasses(PreparedInput(1, 1, 1, 1, 2147483648.0f), passes) == 0, "out-of-range CVTT radius must produce no passes");
}

static void TestPreflightBoundAndCapacity() {
	std::array<sgre::scene::MotionMaskedBlendPass, 3> passes{};
	for (auto& pass : passes) {
		pass.Step = 777;
		pass.BlendParameter = 99;
	}

	bool rejected = false;
	try {
		static_cast<void>(sgre::scene::PrepareMotionMaskedBlendPasses(PreparedInput(100, 80, 1, 1, 0.18f), passes));
	} catch (const std::runtime_error&) {
		rejected = true;
	}

	Require(rejected && passes[0].Step == 777 && passes[2].BlendParameter == 99, "insufficient output must reject before any writes");
	rejected = false;
	try {
		static_cast<void>(sgre::scene::PrepareMotionMaskedBlendPasses(PreparedInput(1000000, 1, 1, 1, 1), passes));
	} catch (const std::runtime_error&) {
		rejected = true;
	}

	Require(rejected && passes[0].Step == 777, "large positive work must reject without iterating the full radius");
	rejected = false;
	try {
		static_cast<void>(sgre::scene::PrepareMotionMaskedBlendPasses(PreparedInput(1, 1, std::numeric_limits<float>::quiet_NaN(), 1, 1), passes));
	} catch (const std::runtime_error&) {
		rejected = true;
	}

	Require(rejected && passes[0].Step == 777, "nonfinite input must reject before writes");
	Require(sgre::scene::PrepareMotionMaskedBlendPasses(PreparedInput(1, 1, 1, 1, 0), {}) == 0, "empty output is valid for zero passes");
}

int main() {
	try {
		TestEqualAxisPairs();
		TestUnequalAxisScans();
		TestRoundingAndNoPasses();
		TestPreflightBoundAndCapacity();
		std::cout << "motion masked blend tests passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}

	return 0;
}
