#include "sgre/scene/raw_texture_restore.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static void TestRgbChannelsAndPaddedRows() {
	std::array<std::uint8_t, 28> destination;
	destination.fill(99);
	const sgre::scene::RawTextureSurface surface{std::span(destination).subspan(3, 22), 2, 2, 11};
	const std::array<std::uint8_t, 12> rgb{1, 2, 3, 10, 20, 30, 200, 150, 100, 0, 127, 255};
	sgre::scene::RestoreRawTextureRgb(surface, rgb);
	const std::array<std::uint8_t, 28> expected{99, 99, 99, 3, 2, 1, 255, 30, 20, 10, 255, 99, 99, 99, 100, 150, 200, 255, 255, 127, 0, 255, 99, 99, 99, 99, 99, 99};
	Require(destination == expected, "RGB restore reverses channels to BGRA, writes alpha 255 and preserves unaligned row padding and guards");
}

static void TestBgraPreservesChannelAndAlphaBytes() {
	std::array<std::uint8_t, 23> destination;
	destination.fill(71);
	const sgre::scene::RawTextureSurface surface{std::span(destination).subspan(1, 20), 1, 4, 5};
	const std::array<std::uint8_t, 16> bgra{1, 2, 3, 0, 4, 5, 6, 17, 7, 8, 9, 128, 10, 11, 12, 255};
	sgre::scene::RestoreRawTextureBgra(surface, bgra, 1, 4);
	const std::array<std::uint8_t, 23> expected{71, 1, 2, 3, 0, 71, 4, 5, 6, 17, 71, 7, 8, 9, 128, 71, 10, 11, 12, 255, 71, 71, 71};
	Require(destination == expected, "native packed image restoration preserves arbitrary BGRA and alpha bytes without premultiplication or padding writes");
}

static void TestMappedByteSurface() {
	std::array<std::byte, 7> mapped;
	mapped.fill(std::byte{17});
	const sgre::scene::RawTextureSurface surface{mapped, 1, 1, 7};
	const std::array<std::uint8_t, 3> rgb{1, 2, 3};
	sgre::scene::RestoreRawTextureRgb(surface, rgb);
	Require(mapped == std::array<std::byte, 7>{std::byte{3}, std::byte{2}, std::byte{1}, std::byte{255}, std::byte{17}, std::byte{17}, std::byte{17}}, "actual mutable texture byte maps use the same channel/padding contract without casts");
}

static void TestValidationBeforePixelWrites() {
	std::array<std::uint8_t, 16> destination;
	destination.fill(61);
	const auto original = destination;
	const std::array<std::uint8_t, 12> rgb{};
	const std::array<std::uint8_t, 16> bgra{};
	const sgre::scene::RawTextureSurface surface{destination, 2, 2, 8};
	std::size_t rejected = 0;
	auto reject = [&](auto operation) {
		bool failed = false;
		try {
			operation();
		} catch (const std::runtime_error&) {
			failed = true;
		}

		Require(failed && destination == original, "invalid restore input must fail before any pixel mutation");
		++rejected;
	};
	reject([&] {
		sgre::scene::RestoreRawTextureBgra(surface, bgra, 1, 2);
	});
	reject([&] {
		sgre::scene::RestoreRawTextureRgb(surface, std::span(rgb).first(11));
	});
	reject([&] {
		sgre::scene::RestoreRawTextureBgra(surface, std::span(bgra).first(15), 2, 2);
	});
	reject([&] {
		sgre::scene::RestoreRawTextureRgb({destination, 2, 2, 7}, rgb);
	});
	reject([&] {
		sgre::scene::RestoreRawTextureRgb({std::span(destination).first(15), 2, 2, 8}, rgb);
	});
	reject([&] {
		sgre::scene::RestoreRawTextureRgb({destination, 0, 2, 8}, rgb);
	});
	reject([&] {
		sgre::scene::RestoreRawTextureRgb({destination, 0xffffffff, 0xffffffff, 8}, rgb);
	});
	reject([&] {
		sgre::scene::RestoreRawTextureRgb(surface, std::span(destination).first(12));
	});
	reject([&] {
		sgre::scene::RestoreRawTextureBgra(surface, destination, 2, 2);
	});
	Require(rejected == 9, "bounded dimensions, lengths, pitches and aliased mappings are all independently checked");
}

int main() {
	try {
		TestRgbChannelsAndPaddedRows();
		TestBgraPreservesChannelAndAlphaBytes();
		TestMappedByteSurface();
		TestValidationBeforePixelWrites();
		std::cout << "native raw texture padded restore paths passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
