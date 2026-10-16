#include "sgre/scene/raw_image.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <class Operation>
static void RequireError(Operation operation) {
	try {
		operation();
	} catch (const std::runtime_error&) {
		return;
	}

	throw std::runtime_error("expected bounded raw image rejection");
}

static void TestZeroedConstructionAndPackedFill() {
	sgre::scene::RawImage image(3, 2);
	Require(image.Width() == 3 && image.Height() == 2 && image.IsUpdated() && image.BgraPixels().size() == 24, "native construction retains dimensions, exact four-byte extent and initial update flag");
	Require(std::ranges::all_of(image.BgraPixels(), [](auto byte) {
		return byte == 0;
	}),
	    "native RawImage construction zeroes every pixel channel");
	image.ClearUpdated();
	image.Fill({1, 0, 1, 2}, 0x12345678);
	const std::vector<std::uint8_t> expected{0, 0, 0, 0, 0x56, 0x34, 0x12, 0x78, 0, 0, 0, 0, 0, 0, 0, 0, 0x56, 0x34, 0x12, 0x78, 0, 0, 0, 0};
	Require(std::ranges::equal(image.BgraPixels(), expected) && image.IsUpdated(), "RRGGBBAA fill writes only the selected BGRA rectangle and restores update state");
}

static void TestAlphaPreservingFillAndSerialization() {
	sgre::scene::RawImage image(2, 1);
	const std::array<std::uint8_t, 8> rgba{1, 2, 3, 0, 10, 20, 30, 64};
	image.RestorePixels(rgba);
	image.ClearUpdated();
	image.Fill({0, 0, 2, 1}, 0xaabbccdd, true);
	Require(image.StorePixels(true) == std::vector<std::uint8_t>{1, 2, 3, 0, 0xaa, 0xbb, 0xcc, 64}, "opaque-only fill preserves transparent pixels and existing nonzero alpha while replacing RGB");
	Require(image.StorePixels(false) == std::vector<std::uint8_t>{1, 2, 3, 0xaa, 0xbb, 0xcc} && image.IsUpdated(), "RGB storage omits alpha without changing the image's update flag");
	image.ClearUpdated();
	image.Fill({0, 0, 1, 1}, 0xffffffff, true);
	Require(image.IsUpdated() && image.StorePixels(true)[3] == 0, "native opaque-only no-op pixels still set the update flag");
	const std::array<std::uint8_t, 6> rgb{7, 8, 9, 250, 128, 0};
	image.RestorePixels(rgb);
	Require(image.StorePixels(true) == std::vector<std::uint8_t>{7, 8, 9, 255, 250, 128, 0, 255}, "RGB import expands alpha to 255 and serialization restores semantic channel ordering");
}

static void TestAliasedInputAndFailureAtomicity() {
	sgre::scene::RawImage image(1, 1);
	const std::array<std::uint8_t, 4> rgba{1, 2, 3, 4};
	image.RestorePixels(rgba);
	const auto alias = image.BgraPixels();
	image.RestorePixels(alias);
	Require(image.StorePixels(true) == std::vector<std::uint8_t>{3, 2, 1, 4}, "aliased input is snapshotted before native RGBA-to-BGRA conversion");
	image.ClearUpdated();
	const auto before = image.StorePixels(true);
	RequireError([&] {
		image.RestorePixels(std::span(rgba).first(2));
	});
	RequireError([&] {
		image.Fill({0, 0, 0, 1}, 0);
	});
	RequireError([&] {
		image.Fill({0xffffffff, 0, 1, 1}, 0);
	});
	RequireError([&] {
		image.Fill({0, 0, 1, 2}, 0);
	});
	Require(before == image.StorePixels(true) && !image.IsUpdated(), "invalid input lengths/rectangles leave both pixels and update state unchanged");
	RequireError([] {
		sgre::scene::RawImage invalid(0, 1);
	});
	RequireError([] {
		sgre::scene::RawImage invalid(0xffffffff, 0xffffffff);
	});
}

static void TestNativeBackgroundFilters() {
	sgre::scene::RawImage image(3, 1);
	image.Fill({0, 0, 3, 1}, 0xffffffff);
	image.ClearUpdated();
	image.BoxBlur(1, 0);
	Require(image.StorePixels(true) == std::vector<std::uint8_t>{255, 255, 255, 255, 254, 254, 254, 254, 255, 255, 255, 255} && image.IsUpdated(), "native blur uses a truncated reciprocal, including a one-level loss for a three-pixel white window");
	image.ClearUpdated();
	image.FillAlpha();
	Require(image.StorePixels(true)[7] == 255 && image.IsUpdated(), "fillAlpha replaces every alpha byte while retaining filtered RGB");
	const std::array<std::uint8_t, 12> input{0, 128, 255, 17, 128, 255, 0, 98, 255, 0, 128, 254};
	image.RestorePixels(input);
	image.ClearUpdated();
	image.AdjustLevel({32, 224, 255, 0, 0, 255});
	Require(image.StorePixels(true) == std::vector<std::uint8_t>{32, 255, 255, 17, 128, 255, 0, 98, 224, 255, 128, 254} && image.IsUpdated(), "RGB endpoint tables preserve native unsigned ramp division, signed clipping and every alpha byte");
	image.ClearUpdated();
	const auto before = image.StorePixels(true);
	RequireError([&] {
		image.BoxBlur(2, 0);
	});
	RequireError([&] {
		image.BoxBlur(0, 1);
	});
	RequireError([&] {
		image.BoxBlur(0, 0, 0);
	});
	RequireError([&] {
		image.BoxBlur(0, 0, 0xffffffff);
	});
	Require(before == image.StorePixels(true) && !image.IsUpdated(), "blur rejects unsupported extents/work before modifying pixels or update state");
	image.BoxBlur(0, 0, 2);
	Require(before == image.StorePixels(true) && image.IsUpdated(), "zero-radius iterations retain every channel while marking updated");
}

int main() {
	try {
		TestNativeBackgroundFilters();
		TestZeroedConstructionAndPackedFill();
		TestAlphaPreservingFillAndSerialization();
		TestAliasedInputAndFailureAtomicity();
		std::cout << "native RawImage zero/fill/restore/store paths passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
