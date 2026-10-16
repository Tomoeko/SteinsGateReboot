#include "sgre/scene/raw_texture_color.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

static void Require(bool value, std::string_view message) {
	if (!value) {
		throw std::runtime_error(std::string(message));
	}
}

static void TestOrdinaryAndUnsignedColorQuantization() {
	Require(sgre::scene::PackRawTextureColor({0, 0.5f, 1}) == 0x007fff00, "native component multiplication truncates half intensity to 127");
	Require(sgre::scene::PackRawTextureColor({-0.5f, 2, -0.0001f}) == 0xffff0000, "native unsigned low-word clamp makes negative integral components 255 while negative fractions truncate 0");
	Require(sgre::scene::PackRawTextureColor({1, 1, 1}) == 0xffffff00, "raw packedRGB reserves its low byte for draw opacity");
}

static void TestNativeIndefiniteAndModuloWordConversions() {
	const auto positive_infinity = std::bit_cast<float>(std::uint32_t{0x7f800000});
	const auto negative_infinity = std::bit_cast<float>(std::uint32_t{0xff800000});
	const auto nan = std::bit_cast<float>(std::uint32_t{0x7fc00001});
	Require(sgre::scene::PackRawTextureColor({positive_infinity, negative_infinity, nan}) == 0, "64-bit integer-indefinite conversion has a zero low DWORD");
	const auto multiple_of_word = std::bit_cast<float>(std::uint32_t{0x4f800000});
	const auto negative_multiple = std::bit_cast<float>(std::uint32_t{0xcf800000});
	Require(sgre::scene::PackRawTextureColor({multiple_of_word, negative_multiple, -0x1p63f}) == 0, "native conversion compares the low DWORD after 64-bit truncation, including modulo-word and overflow values");
}

static void TestDrawOpacityAndSemanticChannels() {
	Require(sgre::scene::PrepareRawTextureDrawColor(0x12345600, 0xabcdef78) == 0x78123456, "draw color combines only low opacity and transforms native RRGGBBAA into AARRGGBB");
	Require(sgre::scene::PrepareRawTextureDrawColor(0xffffff00, 0xffffffff) == 0xffffffff, "default white/full opacity preserves the native unmodulated-texture flag input");
	Require(sgre::scene::PrepareRawTextureDrawColor(0x12345609, 2) == 0x0b123456, "original draw ORs an existing low byte instead of replacing it");
}

int main() {
	try {
		TestOrdinaryAndUnsignedColorQuantization();
		TestNativeIndefiniteAndModuloWordConversions();
		TestDrawOpacityAndSemanticChannels();
		std::cout << "native raw texture color quantization and draw packing passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
