#include "sgre/scene/indicator_placement.hpp"

#include <array>
#include <bit>
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

struct PlacementOperation {
	std::string_view Name;
	float X;
	float Y;
};

static void TestOriginalPlacementSequence() {
	static constexpr std::array<PlacementOperation, 20> operations{{
	    {"measure", 182.5625f, -71.25f},
	    {"coord", -28.4375f, -17.25f},
	    {"right", -226.5f, 104.1875f},
	    {"bottom", 236.1875f, 20.75f},
	    {"visible", 49.6875f, -125.6875f},
	    {"resize", 133.625f, 45.1875f},
	    {"left", 108.0625f, 220.0625f},
	    {"bottom", 175.5f, -46.3125f},
	    {"resize", -177.375f, 50.0f},
	    {"top", -77.6875f, -195.8125f},
	    {"resize", 65.75f, 105.3125f},
	    {"top", -97.875f, 141.0f},
	    {"right", 28.6875f, 115.6875f},
	    {"top", -232.75f, -237.5625f},
	    {"detach", -106.875f, -109.8125f},
	    {"resize", -109.625f, -160.6875f},
	    {"bottom", 5.125f, 175.3125f},
	    {"right", 67.6875f, -150.8125f},
	    {"bottom", 33.4375f, -8.75f},
	    {"right", 83.8125f, -66.9375f},
	}};
	static constexpr std::array<std::array<std::uint32_t, 8>, 20> expected{{
	    {{0x0u, 0x0u, 0x43369000u, 0xc28e8000u, 0x43369000u, 0xc28e8000u, 0x0u, 0x0u}},
	    {{0xc1e38000u, 0xc18a0000u, 0x431a2000u, 0xc2b10000u, 0x43369000u, 0xc28e8000u, 0x0u, 0x0u}},
	    {{0xc3cc8800u, 0xc18a0000u, 0xc3628000u, 0xc2b10000u, 0x43369000u, 0xc28e8000u, 0x0u, 0x0u}},
	    {{0xc3cc8800u, 0x4399b800u, 0xc3628000u, 0x436c3000u, 0x43369000u, 0xc28e8000u, 0x0u, 0x0u}},
	    {{0xc3cc8800u, 0x4399b800u, 0xc3628000u, 0x436c3000u, 0x43369000u, 0xc28e8000u, 0x1u, 0x0u}},
	    {{0xc3cc8800u, 0x4399b800u, 0xc3628000u, 0x436c3000u, 0x43369000u, 0xc28e8000u, 0x1u, 0x0u}},
	    {{0x42d82000u, 0x4399b800u, 0x43915000u, 0x436c3000u, 0x43369000u, 0xc28e8000u, 0x1u, 0x0u}},
	    {{0x42d82000u, 0x4376c000u, 0x43915000u, 0x432f8000u, 0x43369000u, 0xc28e8000u, 0x1u, 0x0u}},
	    {{0x42d82000u, 0x4376c000u, 0x43915000u, 0x432f8000u, 0x43369000u, 0xc28e8000u, 0x1u, 0x0u}},
	    {{0x42d82000u, 0xc29b6000u, 0x43915000u, 0xc314f000u, 0x43369000u, 0xc28e8000u, 0x1u, 0x0u}},
	    {{0x42d82000u, 0xc29b6000u, 0x43915000u, 0xc314f000u, 0x43369000u, 0xc28e8000u, 0x1u, 0x0u}},
	    {{0x42d82000u, 0xc2c3c000u, 0x43915000u, 0xc3292000u, 0x43369000u, 0xc28e8000u, 0x1u, 0x0u}},
	    {{0xc319e000u, 0xc2c3c000u, 0x41e58000u, 0xc3292000u, 0x43369000u, 0xc28e8000u, 0x1u, 0x0u}},
	    {{0xc319e000u, 0xc368c000u, 0x41e58000u, 0xc3980000u, 0x43369000u, 0xc28e8000u, 0x1u, 0x0u}},
	    {{0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x1u}},
	    {{0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x1u}},
	    {{0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x1u}},
	    {{0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x1u}},
	    {{0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x1u}},
	    {{0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x1u}},
	}};

	sgre::scene::IndicatorPlacement placement;
	Require(!placement.Visible() && !placement.IsDetached() && placement.Size() == sgre::scene::LayerPoint{} && placement.Bounds() == sgre::scene::LayerClip{0, 0, 0, 0}, "native Indicator must begin hidden with zero coordinate and measured size");
	for (std::size_t index = 0; index < operations.size(); ++index) {
		const auto& operation = operations[index];
		if (operation.Name == "measure") {
			placement.UpdateMeasuredSize({operation.X, operation.Y});
		} else if (operation.Name == "coord") {
			placement.SetCoordinate({operation.X, operation.Y});
		} else if (operation.Name == "left") {
			placement.SetEdge(sgre::scene::IndicatorEdge::Left, operation.X);
		} else if (operation.Name == "top") {
			placement.SetEdge(sgre::scene::IndicatorEdge::Top, operation.X);
		} else if (operation.Name == "right") {
			placement.SetEdge(sgre::scene::IndicatorEdge::Right, operation.X);
		} else if (operation.Name == "bottom") {
			placement.SetEdge(sgre::scene::IndicatorEdge::Bottom, operation.X);
		} else if (operation.Name == "visible") {
			placement.SetVisible(operation.X >= 0);
		} else if (operation.Name == "resize") {
			placement.RequestSize({operation.X, operation.Y});
		} else {
			placement.Detach();
		}

		const auto bounds = placement.Bounds();
		const auto size = placement.Size();
		const std::array<std::uint32_t, 8> actual{std::bit_cast<std::uint32_t>(bounds.Left), std::bit_cast<std::uint32_t>(bounds.Top), std::bit_cast<std::uint32_t>(bounds.Right), std::bit_cast<std::uint32_t>(bounds.Bottom), std::bit_cast<std::uint32_t>(size.X), std::bit_cast<std::uint32_t>(size.Y), static_cast<std::uint32_t>(placement.Visible()), static_cast<std::uint32_t>(placement.IsDetached())};
		Require(actual == expected[index], "Indicator placement snapshot differs from unchanged native virtual dispatch");
	}
}

static void TestFixedIntrinsicSizeAndInertDetachedView() {
	sgre::scene::IndicatorPlacement placement;
	placement.UpdateMeasuredSize({12, 8});
	placement.SetCoordinate({4, 5});
	placement.SetVisible(true);
	placement.RequestSize({100, 200});
	Require(placement.Size() == sgre::scene::LayerPoint{12, 8} && placement.Bounds() == sgre::scene::LayerClip{4, 5, 16, 13}, "ordinary resize requests must not replace native text-measured extents");
	placement.SetEdge(sgre::scene::IndicatorEdge::Right, 20);
	placement.SetEdge(sgre::scene::IndicatorEdge::Bottom, 30);
	Require(placement.Bounds() == sgre::scene::LayerClip{8, 22, 20, 30}, "right/bottom setters must reposition rather than resize the Indicator");
	placement.Detach();
	placement.SetCoordinate({std::numeric_limits<float>::quiet_NaN(), 2});
	placement.UpdateMeasuredSize({1, std::numeric_limits<float>::infinity()});
	placement.SetVisible(true);
	placement.SetEdge(static_cast<sgre::scene::IndicatorEdge>(255), 0);
	Require(placement.IsDetached() && !placement.Visible() && placement.Size() == sgre::scene::LayerPoint{} && placement.Bounds() == sgre::scene::LayerClip{0, 0, 0, 0}, "detached retained views must remain inert with native zero/false getters");
}

static void TestFiniteSafetyRejectionPreservesState() {
	sgre::scene::IndicatorPlacement placement;
	placement.UpdateMeasuredSize({2, 3});
	placement.SetCoordinate({4, 5});
	bool rejected = false;
	try {
		placement.SetEdge(sgre::scene::IndicatorEdge::Left, std::numeric_limits<float>::infinity());
	} catch (const std::runtime_error&) {
		rejected = true;
	}

	Require(rejected && placement.Bounds() == sgre::scene::LayerClip{4, 5, 6, 8}, "authored finite placement rejection must preserve prior live state");
}

int main() {
	try {
		TestOriginalPlacementSequence();
		TestFixedIntrinsicSizeAndInertDetachedView();
		TestFiniteSafetyRejectionPreservesState();
		std::cout << "Indicator native placement and detach fixtures passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
