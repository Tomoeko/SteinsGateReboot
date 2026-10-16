#include "sgre/scene/motion_copy_target.hpp"

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

static void RequireFailure(const std::function<void()>& operation, std::string_view context) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, "copied-target error omitted its context");

		return;
	}

	throw std::runtime_error("invalid copied-target dimensions were accepted");
}

static void TestReuseBoundaries() {
	const sgre::scene::MotionCopiedTargetSize current{16, 32};
	Require(!sgre::scene::PrepareMotionCopiedTargetUpdate(current, {1, 2, 9, 18}).Replace, "exact half extents did not keep the copied target");
	Require(!sgre::scene::PrepareMotionCopiedTargetUpdate(current, {1, 2, 17, 34}).Replace, "exact current extents did not keep the copied target");
	const auto smaller = sgre::scene::PrepareMotionCopiedTargetUpdate(current, {1, 2, 8.75f, 18});
	Require(smaller.Replace && smaller.Size.Width == 8 && smaller.Size.Height == 16, "smaller copied extent was not replaced and ceiled");
	const auto larger = sgre::scene::PrepareMotionCopiedTargetUpdate(current, {1, 2, 17.25f, 34});
	Require(larger.Replace && larger.Size.Width == 17 && larger.Size.Height == 32, "larger copied extent was not replaced and ceiled");
	Require(sgre::scene::PrepareMotionCopiedTargetUpdate(current, {1, 2, 9, 17.75f}).Replace, "height-only shrink did not replace the target");
	Require(sgre::scene::PrepareMotionCopiedTargetUpdate(current, {1, 2, 9, 34.25f}).Replace, "height-only growth did not replace the target");
}

static void TestInitialAndSequentialUpdate() {
	const auto first = sgre::scene::PrepareMotionCopiedTargetUpdate({}, {1, 2, 1.25f, 2.75f});
	Require(first.Replace && first.Size.Width == 1 && first.Size.Height == 1, "initial copied target did not receive positive ceiled dimensions");
	const auto second = sgre::scene::PrepareMotionCopiedTargetUpdate(first.Size, {1, 2, 1.5f, 2.5f});
	Require(!second.Replace && second.Size.Width == 1 && second.Size.Height == 1, "sequential half-size update needlessly replaced the target");
}

static void TestUnsupportedInputs() {
	RequireFailure([]() {
		static_cast<void>(sgre::scene::PrepareMotionCopiedTargetUpdate({-1, 1}, {0, 0, 1, 1}));
	},
	    "nonnegative");
	RequireFailure([]() {
		static_cast<void>(sgre::scene::PrepareMotionCopiedTargetUpdate({}, {0, 0, 0, 1}));
	},
	    "positive finite extents");
	RequireFailure([]() {
		static_cast<void>(sgre::scene::PrepareMotionCopiedTargetUpdate({}, {0, 0, std::numeric_limits<float>::infinity(), 1}));
	},
	    "rectangle must be finite");
	RequireFailure([]() {
		static_cast<void>(sgre::scene::PrepareMotionCopiedTargetUpdate({}, {0, 0, 2147483648.0f, 1}));
	},
	    "signed dimensions");
}

int main() {
	try {
		TestReuseBoundaries();
		TestInitialAndSequentialUpdate();
		TestUnsupportedInputs();
		std::cout << "Native copied-target half/full reuse boundaries and dimension quantization passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
