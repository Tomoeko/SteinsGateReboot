#include "sgre/scene/motion_tessellation.hpp"

#include "sgre/scene/motion_deformation.hpp"

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

static void RequireFailure(const std::function<void()>& operation, std::string_view context) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, "division failure lost its context");

		return;
	}

	throw std::runtime_error("invalid mesh division was accepted");
}

struct NativeDivisionFixture {
	std::array<std::uint32_t, 7> Fields{};
	float Ratio = 0;
	std::array<std::uint32_t, 5> Expected{};
};

static void TestOriginalDivisionCache() {
	// Original 0x140520d80, 0x140554da0, and 0x140554c60 executed these sequential requests.
	// Fields are mesh mode, own/ancestor divisions, own width/height, and ancestor width/height.
	const std::array<NativeDivisionFixture, 9> fixtures = {{
	    {{1, 0, 59, 2798, 2388, 1536, 1643}, 0.5f, {0, 0, 0, 2, 2}},
	    {{0, 59, 29, 3773, 1674, 3883, 1650}, 0.5f, {13, 13, 0x3f000000, 10, 5}},
	    {{0, 113, 10, 3026, 1081, 1882, 957}, 0.5f, {13, 13, 0x3f000000, 10, 5}},
	    {{0, 108, 119, 1444, 601, 4041, 134}, 0.5f, {28, 28, 0x3f000000, 20, 10}},
	    {{0, 25, 59, 777, 1663, 2171, 838}, 0.5f, {28, 28, 0x3f000000, 9, 21}},
	    {{0, 105, 73, 1338, 880, 3158, 1811}, 0.5f, {28, 28, 0x3f000000, 17, 13}},
	    {{0, 86, 55, 3360, 2948, 1107, 2084}, 1.0f, {108, 108, 0x3f800000, 58, 52}},
	    {{0, 92, 16, 2525, 3576, 4073, 3863}, 1.0f, {108, 108, 0x3f800000, 45, 65}},
	    {{0, 124, 0, 1272, 484, 1649, 3818}, 1.5f, {0, 0, 0x3fc00000, 2, 2}},
	}};
	sgre::scene::MotionMeshDivisionCache cache;
	for (const auto& fixture : fixtures) {
		const auto& fields = fixture.Fields;
		std::uint32_t division = 0;
		if (fields[0] == 1) {
			division = sgre::scene::ScaleMotionMeshDivision(fields[1], fixture.Ratio);
		} else {
			sgre::scene::InheritedMotionMeshDivision request;
			request.Width = fields[3];
			request.Height = fields[4];
			request.AncestorWidth = fields[5];
			request.AncestorHeight = fields[6];
			request.AncestorDivision = fields[2];
			request.Ratio = fixture.Ratio;
			division = sgre::scene::ResolveInheritedMotionMeshDivision(request, cache);
		}

		const auto subdivision = sgre::scene::SelectMotionMeshSubdivision(division, fields[3], fields[4]);
		const std::array<std::uint32_t, 5> actual = {division, cache.MinimumDivision, std::bit_cast<std::uint32_t>(cache.Ratio), subdivision.X, subdivision.Y};
		Require(actual == fixture.Expected, "original mesh division cache or subdivision output changed");
	}

	Require(sgre::scene::ScaleMotionMeshDivision(16777217, 1) == 16777216, "division scaling skipped native float rounding");
}

static void TestDivisionSafety() {
	RequireFailure([]() {
		static_cast<void>(sgre::scene::ScaleMotionMeshDivision(20, -1));
	},
	    "ratio");
	RequireFailure([]() {
		static_cast<void>(sgre::scene::ScaleMotionMeshDivision(20, std::numeric_limits<float>::infinity()));
	},
	    "ratio");
	RequireFailure([]() {
		static_cast<void>(sgre::scene::ScaleMotionMeshDivision(0xffffffff, 1));
	},
	    "conversion");
	sgre::scene::InheritedMotionMeshDivision request;
	sgre::scene::MotionMeshDivisionCache cache;
	RequireFailure([&]() {
		static_cast<void>(sgre::scene::ResolveInheritedMotionMeshDivision(request, cache));
	},
	    "rectangle");
	request.Width = request.Height = 10;
	request.AncestorWidth = request.AncestorHeight = 10;
	request.AncestorDivision = 20;
	cache.MinimumDivision = 0xffffffff;
	RequireFailure([&]() {
		static_cast<void>(sgre::scene::ResolveInheritedMotionMeshDivision(request, cache));
	},
	    "cache");
	cache.MinimumDivision = 0;
	request.Width = 0x7ffffffe;
	request.Height = 1;
	RequireFailure([&]() {
		static_cast<void>(sgre::scene::ResolveInheritedMotionMeshDivision(request, cache));
	},
	    "product");
	request.Height = 2;
	RequireFailure([&]() {
		static_cast<void>(sgre::scene::ResolveInheritedMotionMeshDivision(request, cache));
	},
	    "rectangle");
	Require(cache.MinimumDivision == 0 && cache.Ratio == 0, "rejected division request mutated the inherited cache");
}

int main() {
	try {
		TestOriginalDivisionCache();
		TestDivisionSafety();
		std::cout << "Native mesh division scaling, inherited cache maximum/reset, subdivision coupling, and guards passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
