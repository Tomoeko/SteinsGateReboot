#include "sgre/scene/motion_copy_region.hpp"

#include <array>
#include <bit>
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

static void RequireRectangle(const sgre::scene::MotionCopyRegionPlan& plan, const sgre::scene::MotionBounds& expected, std::string_view message) {
	const std::array<float, 4> actual{plan.Rectangle.MinimumX, plan.Rectangle.MinimumY, plan.Rectangle.MaximumX, plan.Rectangle.MaximumY};
	const std::array<float, 4> wanted{expected.MinimumX, expected.MinimumY, expected.MaximumX, expected.MaximumY};
	Require(plan.Action == sgre::scene::MotionCopyRegionAction::Copy, message);
	for (std::size_t index = 0; index < actual.size(); ++index) {
		Require(std::bit_cast<std::uint32_t>(actual[index]) == std::bit_cast<std::uint32_t>(wanted[index]), message);
	}
}

static void RequireFailure(const std::function<void()>& operation, std::string_view context) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, "copied-region error omitted its context");
		return;
	}
	throw std::runtime_error("unsupported copied-region input was accepted");
}

static sgre::scene::MotionCopyRegionInput CenteredInput() {
	sgre::scene::MotionCopyRegionInput input;
	input.Target = {100, 80};
	input.BlendMode = 0x100;
	input.Accumulated = {-0.5f, -0.5f, 0.5f, 0.5f};
	return input;
}

static void TestTransformClipAndWholeTarget() {
	auto input = CenteredInput();
	RequireRectangle(sgre::scene::PrepareMotionCopyRegion(input), {25, 20, 75, 60}, "centered region transform changed");
	input.Clip = {28.25f, 24.5f, 70.125f, 58.75f};
	RequireRectangle(sgre::scene::PrepareMotionCopyRegion(input), {28, 24, 71, 59}, "clip intersection was not rounded outward");
	input.Accumulated = {};
	RequireRectangle(sgre::scene::PrepareMotionCopyRegion(input), {0, 0, 100, 80}, "absent accumulated bounds did not ignore clip and copy the whole target");
	input.Target = {3, 3};
	input.Accumulated = {-1, -1, 1, 1};
	input.Clip = {};
	RequireRectangle(sgre::scene::PrepareMotionCopyRegion(input), {0, 0, 3, 2}, "odd target dimensions lost native integer half-height transform");
	input.Accumulated = {};
	RequireRectangle(sgre::scene::PrepareMotionCopyRegion(input), {0, 0, 3, 3}, "whole target extents incorrectly used integer half-height");
}

static void TestPixelationAndBlur() {
	auto input = CenteredInput();
	input.BlendMode = 0x10c;
	input.BlendParameter = 6;
	RequireRectangle(sgre::scene::PrepareMotionCopyRegion(input), {17, 17, 85, 68}, "positive pixelation did not use ceiled maximum extent cells");
	input.BlendParameter = -8;
	RequireRectangle(sgre::scene::PrepareMotionCopyRegion(input), {24, 16, 80, 64}, "negative pixelation did not use absolute fixed cells");
	input.BlendMode = 0x10d;
	input.BlendParameter = 0.9f;
	RequireRectangle(sgre::scene::PrepareMotionCopyRegion(input), {24, 20, 76, 60}, "horizontal blur did not truncate the step before expansion");
	input.BlendParameter = 5.2f;
	RequireRectangle(sgre::scene::PrepareMotionCopyRegion(input), {25, 19, 75, 61}, "vertical blur step five expanded the wrong axis");
	input.BlendParameter = 9.875f;
	RequireRectangle(sgre::scene::PrepareMotionCopyRegion(input), {25, 14, 75, 66}, "last vertical blur step did not expand by six");
	input.Accumulated = {};
	RequireRectangle(sgre::scene::PrepareMotionCopyRegion(input), {0, 0, 100, 80}, "expanded whole target did not clamp to physical extents");
}

static void TestPreserveAndDisable() {
	auto input = CenteredInput();
	input.Clip = {80, 70, 90, 75};
	Require(sgre::scene::PrepareMotionCopyRegion(input).Action == sgre::scene::MotionCopyRegionAction::Preserve, "empty intersection did not preserve previous copy state");
	input.Clip = {75, 20, 80, 60};
	Require(sgre::scene::PrepareMotionCopyRegion(input).Action == sgre::scene::MotionCopyRegionAction::Preserve, "zero-width copy was accepted");
	input.BlendMode = 0x1f0;
	input.Clip = {};
	RequireRectangle(sgre::scene::PrepareMotionCopyRegion(input), {25, 20, 75, 60}, "unrelated mode flags were not masked");
	input.BlendMode = 0;
	input.Target = {};
	input.Accumulated.MinimumX = std::numeric_limits<float>::quiet_NaN();
	Require(sgre::scene::PrepareMotionCopyRegion(input).Action == sgre::scene::MotionCopyRegionAction::Disable, "ordinary blend did not disable copied texture before inspecting unused fields");
}

static void TestUnsupportedInputs() {
	auto input = CenteredInput();
	input.Target.Width = 0;
	RequireFailure([&]() {
		static_cast<void>(sgre::scene::PrepareMotionCopyRegion(input));
	},
	    "dimensions");
	input = CenteredInput();
	input.Clip.MinimumX = std::numeric_limits<float>::infinity();
	RequireFailure([&]() {
		static_cast<void>(sgre::scene::PrepareMotionCopyRegion(input));
	},
	    "bounds must be finite");
	input = CenteredInput();
	input.View.Coefficients[0] = std::numeric_limits<float>::quiet_NaN();
	RequireFailure([&]() {
		static_cast<void>(sgre::scene::PrepareMotionCopyRegion(input));
	},
	    "affine coefficients");
	input = CenteredInput();
	input.BlendMode = 0x10c;
	input.BlendParameter = 0;
	RequireFailure([&]() {
		static_cast<void>(sgre::scene::PrepareMotionCopyRegion(input));
	},
	    "pixelation amount");
	input.BlendParameter = std::numeric_limits<float>::denorm_min();
	RequireFailure([&]() {
		static_cast<void>(sgre::scene::PrepareMotionCopyRegion(input));
	},
	    "pixelation cell");
	input.BlendMode = 0x10d;
	input.BlendParameter = 10;
	RequireFailure([&]() {
		static_cast<void>(sgre::scene::PrepareMotionCopyRegion(input));
	},
	    "blur step");
	input.BlendParameter = -1;
	RequireFailure([&]() {
		static_cast<void>(sgre::scene::PrepareMotionCopyRegion(input));
	},
	    "blur step");
}

int main() {
	try {
		TestTransformClipAndWholeTarget();
		TestPixelationAndBlur();
		TestPreserveAndDisable();
		TestUnsupportedInputs();
		std::cout << "Copied-region transform, clipping, pixelation, blur, preserve and disable checks passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
