#include "sgre/text/vector_glyph_draw.hpp"

#include "glyph_atlas_owner_fixture.hpp"
#include "text_glyph_drawing_fixture.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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
	throw std::runtime_error("text glyph fixture expected failure: " + std::string(expected));
}

static constexpr std::array<std::array<std::uint32_t, 29>, 31> kNativeRectangles{{{0x3f800000u, 0x40000000u, 0x40800000u, 0x40c00000u, 0xc0500000u, 0xc0200000u, 0xff0000ffu, 0x00000400u, 0x00000400u, 0xc0500000u, 0xc0200000u, 0xff0000ffu, 0x3a800000u, 0x3b000000u, 0xc0500000u, 0x3fc00000u, 0xff0000ffu, 0x3a800000u, 0x3bc00000u, 0xbe800000u, 0xc0200000u, 0xff0000ffu, 0x3b800000u, 0x3b000000u, 0xbe800000u, 0x3fc00000u, 0xff0000ffu, 0x3b800000u, 0x3bc00000u},
    {0x3f800000u, 0x40000000u, 0x40800000u, 0x40c00000u, 0xc0480000u, 0xc0100000u, 0x804020ffu, 0x00000100u, 0x00000200u, 0xc0480000u, 0xc0100000u, 0xff204080u, 0x3b800000u, 0x3b800000u, 0xc0480000u, 0x3fe00000u, 0xff204080u, 0x3b800000u, 0x3c400000u, 0xbe000000u, 0xc0100000u, 0xff204080u, 0x3c800000u, 0x3b800000u, 0xbe000000u, 0x3fe00000u, 0xff204080u, 0x3c800000u, 0x3c400000u},
    {0x40000000u, 0x40400000u, 0x40c00000u, 0x41000000u, 0xc0080000u, 0xbfa00000u, 0x20408080u, 0x00000100u, 0x00000200u, 0xc0080000u, 0xbfa00000u, 0x80804020u, 0x3c000000u, 0x3bc00000u, 0xc0080000u, 0x40700000u, 0x80804020u, 0x3c000000u, 0x3c800000u, 0x3ff00000u, 0xbfa00000u, 0x80804020u, 0x3cc00000u, 0x3bc00000u, 0x3ff00000u, 0x40700000u, 0x80804020u, 0x3cc00000u, 0x3c800000u},
    {0x3f800000u, 0x40000000u, 0x40800000u, 0x40c00000u, 0xc0400000u, 0xc0000000u, 0x20408080u, 0x000000ffu, 0x0000007fu, 0xc0400000u, 0xc0000000u, 0x80804020u, 0x3b808081u, 0x3c810204u, 0xc0400000u, 0x40000000u, 0x80804020u, 0x3b808081u, 0x3d418306u, 0x00000000u, 0xc0000000u, 0x80804020u, 0x3c808081u, 0x3c810204u, 0x00000000u, 0x40000000u, 0x80804020u, 0x3c808081u, 0x3d418306u},
    {0x40000000u, 0x40400000u, 0x40c00000u, 0x41000000u, 0xc0000000u, 0xbf800000u, 0x000000ffu, 0x000000ffu, 0x0000007fu, 0xc0000000u, 0xbf800000u, 0xff000000u, 0x3c008081u, 0x3cc18306u, 0xc0000000u, 0x40800000u, 0xff000000u, 0x3c008081u, 0x3d810204u, 0x40000000u, 0xbf800000u, 0xff000000u, 0x3cc0c0c1u, 0x3cc18306u, 0x40000000u, 0x40800000u, 0xff000000u, 0x3cc0c0c1u, 0x3d810204u},
    {0x40400000u, 0x40800000u, 0x41000000u, 0x41200000u, 0xbf800000u, 0x00000000u, 0xffffffffu, 0x000000ffu, 0x0000007fu, 0xbf800000u, 0x00000000u, 0xffffffffu, 0x3c40c0c1u, 0x3d010204u, 0xbf800000u, 0x40c00000u, 0xffffffffu, 0x3c40c0c1u, 0x3da14285u, 0x40800000u, 0x00000000u, 0xffffffffu, 0x3d008081u, 0x3d010204u, 0x40800000u, 0x40c00000u, 0xffffffffu, 0x3d008081u, 0x3da14285u},
    {0x3f800000u, 0x40000000u, 0x40800000u, 0x40c00000u, 0xc0380000u, 0xbfe00000u, 0x000000ffu, 0x0000000du, 0x00000007u, 0xc0380000u, 0xbfe00000u, 0xff000000u, 0x3d9d89d9u, 0x3e924925u, 0xc0380000u, 0x40100000u, 0xff000000u, 0x3d9d89d9u, 0x3f5b6db7u, 0x3e000000u, 0xbfe00000u, 0xff000000u, 0x3e9d89d9u, 0x3e924925u, 0x3e000000u, 0x40100000u, 0xff000000u, 0x3e9d89d9u, 0x3f5b6db7u},
    {0x3f800000u, 0x40000000u, 0x40800000u, 0x40c00000u, 0xc0300000u, 0xbfc00000u, 0xffffffffu, 0x00000400u, 0x00000400u, 0xc0300000u, 0xbfc00000u, 0xffffffffu, 0x3a800000u, 0x3b000000u, 0xc0300000u, 0x40200000u, 0xffffffffu, 0x3a800000u, 0x3bc00000u, 0x3e800000u, 0xbfc00000u, 0xffffffffu, 0x3b800000u, 0x3b000000u, 0x3e800000u, 0x40200000u, 0xffffffffu, 0x3b800000u, 0x3bc00000u},
    {0x40000000u, 0x40400000u, 0x40c00000u, 0x41000000u, 0xbfe00000u, 0xbf000000u, 0x00ff00ffu, 0x00000400u, 0x00000400u, 0xbfe00000u, 0xbf000000u, 0xff00ff00u, 0x3b000000u, 0x3b400000u, 0xbfe00000u, 0x40900000u, 0xff00ff00u, 0x3b000000u, 0x3c000000u, 0x40100000u, 0xbf000000u, 0xff00ff00u, 0x3bc00000u, 0x3b400000u, 0x40100000u, 0x40900000u, 0xff00ff00u, 0x3bc00000u, 0x3c000000u},
    {0x3f800000u, 0x40000000u, 0x40800000u, 0x40c00000u, 0xc0280000u, 0xbfa00000u, 0x00ff00ffu, 0x00000100u, 0x00000200u, 0xc0280000u, 0xbfa00000u, 0xff00ff00u, 0x3b800000u, 0x3b800000u, 0xc0280000u, 0x40300000u, 0xff00ff00u, 0x3b800000u, 0x3c400000u, 0x3ec00000u, 0xbfa00000u, 0xff00ff00u, 0x3c800000u, 0x3b800000u, 0x3ec00000u, 0x40300000u, 0xff00ff00u, 0x3c800000u, 0x3c400000u},
    {0x40000000u, 0x40400000u, 0x40c00000u, 0x41000000u, 0xbfd00000u, 0xbe800000u, 0x0000ffffu, 0x00000100u, 0x00000200u, 0xbfd00000u, 0xbe800000u, 0xffff0000u, 0x3c000000u, 0x3bc00000u, 0xbfd00000u, 0x40980000u, 0xffff0000u, 0x3c000000u, 0x3c800000u, 0x40180000u, 0xbe800000u, 0xffff0000u, 0x3cc00000u, 0x3bc00000u, 0x40180000u, 0x40980000u, 0xffff0000u, 0x3cc00000u, 0x3c800000u},
    {0x40400000u, 0x40800000u, 0x41000000u, 0x41200000u, 0xbf200000u, 0x3f400000u, 0x12345678u, 0x00000100u, 0x00000200u, 0xbf200000u, 0x3f400000u, 0x78563412u, 0x3c400000u, 0x3c000000u, 0xbf200000u, 0x40d80000u, 0x78563412u, 0x3c400000u, 0x3ca00000u, 0x408c0000u, 0x3f400000u, 0x78563412u, 0x3d000000u, 0x3c000000u, 0x408c0000u, 0x40d80000u, 0x78563412u, 0x3d000000u, 0x3ca00000u},
    {0x3f800000u, 0x40000000u, 0x40800000u, 0x40c00000u, 0xc0200000u, 0xbf800000u, 0x0000ffffu, 0x000000ffu, 0x0000007fu, 0xc0200000u, 0xbf800000u, 0xffff0000u, 0x3b808081u, 0x3c810204u, 0xc0200000u, 0x40400000u, 0xffff0000u, 0x3b808081u, 0x3d418306u, 0x3f000000u, 0xbf800000u, 0xffff0000u, 0x3c808081u, 0x3c810204u, 0x3f000000u, 0x40400000u, 0xffff0000u, 0x3c808081u, 0x3d418306u},
    {0x3f800000u, 0x40000000u, 0x40800000u, 0x40c00000u, 0xc0180000u, 0xbf400000u, 0x12345678u, 0x0000000du, 0x00000007u, 0xc0180000u, 0xbf400000u, 0x78563412u, 0x3d9d89d9u, 0x3e924925u, 0xc0180000u, 0x40500000u, 0x78563412u, 0x3d9d89d9u, 0x3f5b6db7u, 0x3f200000u, 0xbf400000u, 0x78563412u, 0x3e9d89d9u, 0x3e924925u, 0x3f200000u, 0x40500000u, 0x78563412u, 0x3e9d89d9u, 0x3f5b6db7u},
    {0x40000000u, 0x40400000u, 0x40c00000u, 0x41000000u, 0xbfb00000u, 0x3e800000u, 0xff0000ffu, 0x0000000du, 0x00000007u, 0xbfb00000u, 0x3e800000u, 0xff0000ffu, 0x3e1d89d9u, 0x3edb6db7u, 0xbfb00000u, 0x40a80000u, 0xff0000ffu, 0x3e1d89d9u, 0x3f924925u, 0x40280000u, 0x3e800000u, 0xff0000ffu, 0x3eec4ec5u, 0x3edb6db7u, 0x40280000u, 0x40a80000u, 0xff0000ffu, 0x3eec4ec5u, 0x3f924925u},
    {0x3f800000u, 0x40000000u, 0x40800000u, 0x40c00000u, 0xc0100000u, 0xbf000000u, 0xff0000ffu, 0x00000400u, 0x00000400u, 0xc0100000u, 0xbf000000u, 0xff0000ffu, 0x3a800000u, 0x3b000000u, 0xc0100000u, 0x40600000u, 0xff0000ffu, 0x3a800000u, 0x3bc00000u, 0x3f400000u, 0xbf000000u, 0xff0000ffu, 0x3b800000u, 0x3b000000u, 0x3f400000u, 0x40600000u, 0xff0000ffu, 0x3b800000u, 0x3bc00000u},
    {0x40000000u, 0x40400000u, 0x40c00000u, 0x41000000u, 0xbfa00000u, 0x3f000000u, 0x804020ffu, 0x00000400u, 0x00000400u, 0xbfa00000u, 0x3f000000u, 0xff204080u, 0x3b000000u, 0x3b400000u, 0xbfa00000u, 0x40b00000u, 0xff204080u, 0x3b000000u, 0x3c000000u, 0x40300000u, 0x3f000000u, 0xff204080u, 0x3bc00000u, 0x3b400000u, 0x40300000u, 0x40b00000u, 0xff204080u, 0x3bc00000u, 0x3c000000u},
    {0x40400000u, 0x40800000u, 0x41000000u, 0x41200000u, 0xbe800000u, 0x3fc00000u, 0x20408080u, 0x00000400u, 0x00000400u, 0xbe800000u, 0x3fc00000u, 0x80804020u, 0x3b400000u, 0x3b800000u, 0xbe800000u, 0x40f00000u, 0x80804020u, 0x3b400000u, 0x3c200000u, 0x40980000u, 0x3fc00000u, 0x80804020u, 0x3c000000u, 0x3b800000u, 0x40980000u, 0x40f00000u, 0x80804020u, 0x3c000000u, 0x3c200000u},
    {0x3f800000u, 0x40000000u, 0x40800000u, 0x40c00000u, 0xc0080000u, 0xbe800000u, 0x804020ffu, 0x00000100u, 0x00000200u, 0xc0080000u, 0xbe800000u, 0xff204080u, 0x3b800000u, 0x3b800000u, 0xc0080000u, 0x40700000u, 0xff204080u, 0x3b800000u, 0x3c400000u, 0x3f600000u, 0xbe800000u, 0xff204080u, 0x3c800000u, 0x3b800000u, 0x3f600000u, 0x40700000u, 0xff204080u, 0x3c800000u, 0x3c400000u},
    {0x3f800000u, 0x40000000u, 0x40800000u, 0x40c00000u, 0xc0000000u, 0x00000000u, 0x20408080u, 0x000000ffu, 0x0000007fu, 0xc0000000u, 0x00000000u, 0x80804020u, 0x3b808081u, 0x3c810204u, 0xc0000000u, 0x40800000u, 0x80804020u, 0x3b808081u, 0x3d418306u, 0x3f800000u, 0x00000000u, 0x80804020u, 0x3c808081u, 0x3c810204u, 0x3f800000u, 0x40800000u, 0x80804020u, 0x3c808081u, 0x3d418306u},
    {0x40000000u, 0x40400000u, 0x40c00000u, 0x41000000u, 0xbf800000u, 0x3f800000u, 0x000000ffu, 0x000000ffu, 0x0000007fu, 0xbf800000u, 0x3f800000u, 0xff000000u, 0x3c008081u, 0x3cc18306u, 0xbf800000u, 0x40c00000u, 0xff000000u, 0x3c008081u, 0x3d810204u, 0x40400000u, 0x3f800000u, 0xff000000u, 0x3cc0c0c1u, 0x3cc18306u, 0x40400000u, 0x40c00000u, 0xff000000u, 0x3cc0c0c1u, 0x3d810204u},
    {0x3f800000u, 0x40000000u, 0x40800000u, 0x40c00000u, 0xbff00000u, 0x3e800000u, 0x000000ffu, 0x0000000du, 0x00000007u, 0xbff00000u, 0x3e800000u, 0xff000000u, 0x3d9d89d9u, 0x3e924925u, 0xbff00000u, 0x40880000u, 0xff000000u, 0x3d9d89d9u, 0x3f5b6db7u, 0x3f900000u, 0x3e800000u, 0xff000000u, 0x3e9d89d9u, 0x3e924925u, 0x3f900000u, 0x40880000u, 0xff000000u, 0x3e9d89d9u, 0x3f5b6db7u},
    {0x40000000u, 0x40400000u, 0x40c00000u, 0x41000000u, 0xbf600000u, 0x3fa00000u, 0xffffffffu, 0x0000000du, 0x00000007u, 0xbf600000u, 0x3fa00000u, 0xffffffffu, 0x3e1d89d9u, 0x3edb6db7u, 0xbf600000u, 0x40c80000u, 0xffffffffu, 0x3e1d89d9u, 0x3f924925u, 0x40480000u, 0x3fa00000u, 0xffffffffu, 0x3eec4ec5u, 0x3edb6db7u, 0x40480000u, 0x40c80000u, 0xffffffffu, 0x3eec4ec5u, 0x3f924925u},
    {0x40400000u, 0x40800000u, 0x41000000u, 0x41200000u, 0x3e000000u, 0x40100000u, 0x00ff00ffu, 0x0000000du, 0x00000007u, 0x3e000000u, 0x40100000u, 0xff00ff00u, 0x3e6c4ec5u, 0x3f124925u, 0x3e000000u, 0x41040000u, 0xff00ff00u, 0x3e6c4ec5u, 0x3fb6db6eu, 0x40a40000u, 0x40100000u, 0xff00ff00u, 0x3f1d89d9u, 0x3f124925u, 0x40a40000u, 0x41040000u, 0xff00ff00u, 0x3f1d89d9u, 0x3fb6db6eu},
    {0x3f800000u, 0x40000000u, 0x40800000u, 0x40c00000u, 0xbfe00000u, 0x3f000000u, 0xffffffffu, 0x00000400u, 0x00000400u, 0xbfe00000u, 0x3f000000u, 0xffffffffu, 0x3a800000u, 0x3b000000u, 0xbfe00000u, 0x40900000u, 0xffffffffu, 0x3a800000u, 0x3bc00000u, 0x3fa00000u, 0x3f000000u, 0xffffffffu, 0x3b800000u, 0x3b000000u, 0x3fa00000u, 0x40900000u, 0xffffffffu, 0x3b800000u, 0x3bc00000u},
    {0x3f800000u, 0x40000000u, 0x40800000u, 0x40c00000u, 0xbfd00000u, 0x3f400000u, 0x00ff00ffu, 0x00000100u, 0x00000200u, 0xbfd00000u, 0x3f400000u, 0xff00ff00u, 0x3b800000u, 0x3b800000u, 0xbfd00000u, 0x40980000u, 0xff00ff00u, 0x3b800000u, 0x3c400000u, 0x3fb00000u, 0x3f400000u, 0xff00ff00u, 0x3c800000u, 0x3b800000u, 0x3fb00000u, 0x40980000u, 0xff00ff00u, 0x3c800000u, 0x3c400000u},
    {0x40000000u, 0x40400000u, 0x40c00000u, 0x41000000u, 0xbf200000u, 0x3fe00000u, 0x0000ffffu, 0x00000100u, 0x00000200u, 0xbf200000u, 0x3fe00000u, 0xffff0000u, 0x3c000000u, 0x3bc00000u, 0xbf200000u, 0x40d80000u, 0xffff0000u, 0x3c000000u, 0x3c800000u, 0x40580000u, 0x3fe00000u, 0xffff0000u, 0x3cc00000u, 0x3bc00000u, 0x40580000u, 0x40d80000u, 0xffff0000u, 0x3cc00000u, 0x3c800000u},
    {0x3f800000u, 0x40000000u, 0x40800000u, 0x40c00000u, 0xbfc00000u, 0x3f800000u, 0x0000ffffu, 0x000000ffu, 0x0000007fu, 0xbfc00000u, 0x3f800000u, 0xffff0000u, 0x3b808081u, 0x3c810204u, 0xbfc00000u, 0x40a00000u, 0xffff0000u, 0x3b808081u, 0x3d418306u, 0x3fc00000u, 0x3f800000u, 0xffff0000u, 0x3c808081u, 0x3c810204u, 0x3fc00000u, 0x40a00000u, 0xffff0000u, 0x3c808081u, 0x3d418306u},
    {0x40000000u, 0x40400000u, 0x40c00000u, 0x41000000u, 0xbf000000u, 0x40000000u, 0x12345678u, 0x000000ffu, 0x0000007fu, 0xbf000000u, 0x40000000u, 0x78563412u, 0x3c008081u, 0x3cc18306u, 0xbf000000u, 0x40e00000u, 0x78563412u, 0x3c008081u, 0x3d810204u, 0x40600000u, 0x40000000u, 0x78563412u, 0x3cc0c0c1u, 0x3cc18306u, 0x40600000u, 0x40e00000u, 0x78563412u, 0x3cc0c0c1u, 0x3d810204u},
    {0x40400000u, 0x40800000u, 0x41000000u, 0x41200000u, 0x3f000000u, 0x40400000u, 0xff0000ffu, 0x000000ffu, 0x0000007fu, 0x3f000000u, 0x40400000u, 0xff0000ffu, 0x3c40c0c1u, 0x3d010204u, 0x3f000000u, 0x41100000u, 0xff0000ffu, 0x3c40c0c1u, 0x3da14285u, 0x40b00000u, 0x40400000u, 0xff0000ffu, 0x3d008081u, 0x3d010204u, 0x40b00000u, 0x41100000u, 0xff0000ffu, 0x3d008081u, 0x3da14285u},
    {0x3f800000u, 0x40000000u, 0x40800000u, 0x40c00000u, 0xbfb00000u, 0x3fa00000u, 0x12345678u, 0x0000000du, 0x00000007u, 0xbfb00000u, 0x3fa00000u, 0x78563412u, 0x3d9d89d9u, 0x3e924925u, 0xbfb00000u, 0x40a80000u, 0x78563412u, 0x3d9d89d9u, 0x3f5b6db7u, 0x3fd00000u, 0x3fa00000u, 0x78563412u, 0x3e9d89d9u, 0x3e924925u, 0x3fd00000u, 0x40a80000u, 0x78563412u, 0x3e9d89d9u, 0x3f5b6db7u}}};

static void TestOriginalFormat4PrimitiveWords() {
	for (const auto& row : kNativeRectangles) {
		sgre::text::VectorGlyphDrawRectangle rectangle;
		rectangle.SourcePixels = {std::bit_cast<float>(row[0]), std::bit_cast<float>(row[1]), std::bit_cast<float>(row[2]), std::bit_cast<float>(row[3])};
		rectangle.DestinationPixels.Left = std::bit_cast<float>(row[4]);
		rectangle.DestinationPixels.Top = std::bit_cast<float>(row[5]);
		rectangle.ColorRgba = row[6];
		const auto vertices = sgre::text::PrepareVectorGlyphVertices(rectangle, row[7], row[8]);
		for (std::size_t index = 0; index < vertices.size(); ++index) {
			const auto& vertex = vertices[index];
			const std::array<std::uint32_t, 5> actual{std::bit_cast<std::uint32_t>(vertex.Position.X), std::bit_cast<std::uint32_t>(vertex.Position.Y), (vertex.Color >> 8) | (vertex.Color << 24), std::bit_cast<std::uint32_t>(vertex.Uv.X), std::bit_cast<std::uint32_t>(vertex.Uv.Y)};
			for (std::size_t word = 0; word < actual.size(); ++word) {
				Require(actual[word] == row[9 + index * 5 + word], "original086c50 geometry/color/UV word differs");
			}
		}
	}
}

static void TestRealFontPreparationDrawAndCacheRetirement() {
	text_atlas_test::Backend backend;
	sgre::scene::TextureCache cache([](auto, const auto&) -> sgre::render::SpriteTextureHandle {
		throw std::runtime_error("vector font fixture must not request bitmap textures");
	},
	    [](auto) {
	    });
	std::vector<sgre::text::VectorGlyphDrawRectangle> observed;
	std::vector<std::vector<sgre::text::VectorGlyphDrawRectangle>> batches;
	std::size_t calls = 0;
	text_drawing_test::Fixture* active = nullptr;
	bool reenter = false;
	text_drawing_test::Fixture fixture(cache, backend, [&](auto& atlas, auto rectangles, const auto&) {
		++calls;
		Require(atlas.Resource() == &atlas.Texture().Resource() && atlas.State().Texture->Dirty && backend.Observed.Uploads == 0, "done prepares actual CPU atlas; drawing callback receives exact live texture without a fake upload");
		observed.assign(rectangles.begin(), rectangles.end());
		batches.emplace_back(rectangles.begin(), rectangles.end());
		if (reenter) {
			RequireFailure([&] {
				active->Call("clear");
			},
			    "during actual glyph");
		}
	});
	active = &fixture;
	fixture.Render();
	Require(backend.Observed.Creates == 1 && backend.Observed.Uploads == 0 && fixture.Text.Context(fixture.Instance).Content().ElementCount() == 2, "actual done must prepare one atlas for two retained shaped elements");
	const auto find_calls = fixture.FindCalls;
	fixture.Draw();
	Require(calls == 1 && observed.size() == 2 && fixture.FindCalls == find_calls, "ordinary draw must reuse actual private glyph/font caches and retain native element order: callbacks=" + std::to_string(calls) + " rectangles=" + std::to_string(observed.size()) + " finds=" + std::to_string(fixture.FindCalls) + "/" + std::to_string(find_calls) + " paused=" + std::to_string(fixture.Text.ScaleView(fixture.Instance).TaskState().DrawPaused) + " elements=" + std::to_string(fixture.Text.Context(fixture.Instance).Content().ElementCount()));
	Require(observed[0].ColorRgba == 0x804020ff && observed[1].DestinationPixels.Left - observed[0].DestinationPixels.Left == 8, "captured style and actual shaped advance did not reach native rectangle queue");
	fixture.Call("setCoord", {10.0f, 20.0f});
	fixture.Call("setClipMode", {std::int32_t{1}});
	fixture.Call("setClipOffset", {3.0f, -4.0f});
	const auto clip = fixture.Text.Context(fixture.Instance).State().ClippingBounds;
	Require(clip.Left == 13 && clip.Top == 16 && clip.Right == 29 && clip.Bottom == 24, "native populated mode1 must use actual content left/top/right and fixed-view height");
	fixture.Call("setClipMode", {std::int32_t{0}});
	fixture.Call("setCoord", {0.0f, 0.0f});
	fixture.Call("setShowCount", {std::int32_t{1}});
	fixture.Call("setOffsetPos", {2.25f, -1.5f});
	fixture.Draw();
	Require(observed.size() == 1 && observed[0].DestinationPixels.Left == 2.25f, "shown count and native bearing plus actual task offset must affect drawing: rectangles=" + std::to_string(observed.size()) + " left=" + (observed.empty() ? std::string("absent") : std::to_string(observed[0].DestinationPixels.Left)));
	fixture.Call("setShowCount", {std::int32_t{0}});
	fixture.Draw();
	Require(calls == 2, "zero shown count must not manufacture atlas draws");
	fixture.Call("setShowCount", {std::int32_t{2}});
	fixture.Call("clearFont");
	RequireFailure([&] {
		fixture.Draw();
	},
	    "actual prepared atlas");
	fixture.Call("done");
	static_cast<void>(fixture.Vm.CollectGarbage());
	reenter = true;
	fixture.Draw();
	Require(calls == 3 && observed.size() == 2 && backend.Observed.Creates == 1, "cleared FontInfo caches must repopulate against the same retained atlas before actual draw");
	fixture.Call("clear");
	fixture.Draw();
	Require(calls == 3, "real empty lifecycle must not issue glyph records after clear");
	fixture.Call("setFont", {fixture.Properties({{"edge", true}, {"edgesize", std::int32_t{1}}})});
	Require(std::get<bool>(fixture.Call("render", {std::string("B"), std::int32_t{0}, std::int32_t{0}, std::int32_t{0}, false})), "stroked fixture must own its genuine content before preparation");
	fixture.Call("done");
	fixture.Draw();
	Require(calls == 5 && batches.size() == 5 && batches[3].size() == 1 && batches[4].size() == 1, "real positive-edge Text done/draw must publish separate edge then fill queues");
	const auto& edge = batches[3][0];
	const auto& fill = batches[4][0];
	Require(edge.SourcePixels.Right - edge.SourcePixels.Left == 10 && edge.SourcePixels.Bottom - edge.SourcePixels.Top == 10 && fill.SourcePixels.Right - fill.SourcePixels.Left == 8 && fill.SourcePixels.Bottom - fill.SourcePixels.Top == 8, "edge1 preparation must preserve its padded native rectangle beside the actual size8 fill");
	Require(edge.DestinationPixels.Left == fill.DestinationPixels.Left - 1 && edge.DestinationPixels.Top == fill.DestinationPixels.Top - 1 && backend.Observed.Creates == 1 && backend.Observed.Uploads == 0, "edge bearing and draw order must use the existing live text atlas without per-pass resources or draw-time uploads");
}

static void TestActualRenderedCountGetterAndRetirement() {
	text_atlas_test::Backend backend;
	sgre::scene::TextureCache cache([](auto, const auto&) -> sgre::render::SpriteTextureHandle {
		throw std::runtime_error("render-count fixture must use its actual vector provider");
	},
	    [](auto) {
	    });
	text_drawing_test::Fixture* active = nullptr;
	std::int32_t drawing_count = -1;
	std::size_t callbacks = 0;
	text_drawing_test::Fixture fixture(cache, backend, [&](auto&, auto, const auto&) {
		++callbacks;
		drawing_count = std::get<std::int32_t>(active->Call("getRenderCount"));
	});
	active = &fixture;
	Require(std::get<std::int32_t>(fixture.Call("getRenderCount")) == 0, "new actual TextRender must expose its native zero rendered count");
	Require(std::get<bool>(fixture.Call("render", {std::string("BB"), std::int32_t{0}, std::int32_t{0}, std::int32_t{0}, false})), "count fixture must render two genuine owned shaped units");
	Require(std::get<std::int32_t>(fixture.Call("getRenderCount")) == 2 && fixture.Text.Context(fixture.Instance).State().ShownElementCount == 0, "getRenderCount reads native parser count before done, independently of shown count");
	fixture.Call("done");
	Require(std::get<std::int32_t>(fixture.Call("getRenderCount", {std::string("ignored native extra operand")})) == 2, "done and extra getter operands must leave the retained native count unchanged");
	fixture.Draw();
	Require(callbacks == 1 && drawing_count == 2, "native count getter remains readable during actual glyph drawing without mutation or new font preparation");
	fixture.Call("setShowCount", {std::int32_t{0}});
	Require(std::get<std::int32_t>(fixture.Call("getRenderCount")) == 2, "reveal count cannot replace the actual rendered count");
	fixture.Call("clear");
	Require(std::get<std::int32_t>(fixture.Call("getRenderCount")) == 0, "real content clear resets the count getter");
	fixture.Render("B");
	Require(std::get<std::int32_t>(fixture.Call("getRenderCount")) == 1, "cleared actual parser can produce a fresh retained count");
	fixture.Layers.ReleaseInstance(fixture.Layer);
	Require(std::get<std::int32_t>(fixture.Call("getRenderCount")) == 0, "parent-first task retirement must make the retained TextRender getter inert");
	fixture.Text.ReleaseInstance(fixture.Instance);
	RequireFailure([&] {
		static_cast<void>(fixture.Call("getRenderCount"));
	},
	    "live native wrapper");
}

static void TestActualKeyWaitGetterAndRetirement() {
	text_atlas_test::Backend backend;
	sgre::scene::TextureCache cache([](auto, const auto&) -> sgre::render::SpriteTextureHandle {
		throw std::runtime_error("key-wait fixture must use its actual vector provider");
	},
	    [](auto) {
	    });
	text_drawing_test::Fixture* active = nullptr;
	std::size_t draws = 0;
	text_drawing_test::Fixture fixture(cache, backend, [&](auto&, auto, const auto&) {
		++draws;
		const auto snapshot = active->Call("getKeyWait");
		Require(active->Vm.Type(snapshot) == sgre::script::SquirrelValueType::Array && active->Vm.Length(snapshot) == 0, "key-wait reads remain valid during actual glyph drawing");
	});
	active = &fixture;
	const auto empty = fixture.Keep(fixture.Call("getKeyWait"));
	Require(fixture.Vm.Type(empty) == sgre::script::SquirrelValueType::Array && fixture.Vm.Length(empty) == 0, "live unrendered core returns a real empty key-wait array");
	fixture.Vm.AppendArray(empty, std::int32_t{99});
	const auto next = fixture.Keep(fixture.Call("getKeyWait", {std::string("ignored native extra operand")}));
	Require(next != empty && fixture.Vm.Length(next) == 0 && fixture.Vm.Length(empty) == 1, "each getter must create an independent array and ignore native extra operands");
	Require(std::get<bool>(fixture.Call("render", {std::string("BB"), std::int32_t{0}, std::int32_t{3}, std::int32_t{0}, false})), "key-wait fixture must own two genuine shaped content elements");
	const auto& content = fixture.Text.Context(fixture.Instance).Content();
	Require(content.RevealBoundaries().size() == 1 && content.RevealBoundaries()[0].FirstElement == 2 && content.RevealBoundaries()[0].Time == 0, "actual single Render owns the native terminal boundary before done");
	Require(fixture.Vm.Length(fixture.Call("getKeyWait")) == 0, "terminal-only native boundary cannot manufacture a key-wait record before done");
	fixture.Call("done");
	fixture.Call("setTimeScale", {2.5f});
	Require(fixture.Vm.Length(fixture.Call("getKeyWait")) == 0, "done/time-scale changes preserve terminal exclusion");
	fixture.Draw();
	Require(draws == 1, "key-wait fixture must actually consume its completed glyph owner");
	fixture.Call("clear");
	Require(content.RevealBoundaries().empty() && fixture.Vm.Length(fixture.Call("getKeyWait")) == 0, "real clear retires the boundary owner without invalidating returned arrays");
	fixture.Render("B");
	fixture.Layers.ReleaseInstance(fixture.Layer);
	Require(std::holds_alternative<std::monostate>(fixture.Call("getKeyWait")), "detached native Text wrapper returns null rather than an empty key-wait array");
	Require(fixture.Vm.Length(empty) == 1 && fixture.Vm.Length(next) == 0, "copied query arrays survive actual source task retirement");
	fixture.Text.ReleaseInstance(fixture.Instance);
	RequireFailure([&] {
		static_cast<void>(fixture.Call("getKeyWait"));
	},
	    "live native wrapper");
}

static void TestActualRevealQueries() {
	text_atlas_test::Backend backend;
	sgre::scene::TextureCache cache([](auto, const auto&) -> sgre::render::SpriteTextureHandle {
		throw std::runtime_error("reveal-query fixture must use its actual vector provider");
	},
	    [](auto) {
	    });
	text_drawing_test::Fixture fixture(cache, backend, [](auto&, auto, const auto&) {
	});
	Require(std::get<float>(fixture.Call("getRenderDelay")) == 0 && std::get<std::int32_t>(fixture.Call("calcShowCount", {std::int32_t{100}})) == 0, "constructed real text core has no reveal duration or query entries");
	Require(std::get<bool>(fixture.Call("render", {std::string("BBB"), std::int32_t{0}, std::int32_t{3}, std::int32_t{0}, false})), "query fixture must produce actual three-element reveal times");
	const std::array<std::int32_t, 7> times{-1, 0, 2, 3, 5, 6, 9};
	const std::array<std::int32_t, 7> expected{0, 1, 1, 2, 2, 3, 3};
	for (const auto done : {false, true}) {
		if (done) {
			fixture.Call("done");
		}
		Require(std::get<float>(fixture.Call("getRenderDelay")) == 9, "native retained duration is available before and after done");
		for (std::size_t index = 0; index < times.size(); ++index) {
			Require(std::get<std::int32_t>(fixture.Call("calcShowCount", {times[index]})) == expected[index], "actual reveal query must match original elapsed-time thresholds before and after done");
		}
	}
	fixture.Call("setTimeScale", {2.0f});
	Require(std::get<float>(fixture.Call("getRenderDelay", {std::string("ignored")})) == 18 && std::get<std::int32_t>(fixture.Call("calcShowCount", {std::int32_t{6}})) == 2, "queries use current time scale without mutating retained element times");
	Require(std::get<std::int32_t>(fixture.Call("calcShowCount")) == 1 && std::get<std::int32_t>(fixture.Call("calcShowCount", {std::string("invalid"), std::int32_t{100}})) == 1, "native missing/invalid integer conversion stays zero and ignores extra operands");
	fixture.Call("setShowCount", {std::int32_t{0}});
	Require(std::get<std::int32_t>(fixture.Call("calcShowCount", {std::int32_t{100}})) == 3 && fixture.Text.Context(fixture.Instance).State().ShownElementCount == 0, "calcShowCount queries independently of actual shown-count mutation");
	fixture.Call("clear");
	Require(std::get<float>(fixture.Call("getRenderDelay")) == 0 && std::get<std::int32_t>(fixture.Call("calcShowCount", {std::int32_t{100}})) == 0, "real clear retires duration and query entries");
	fixture.Layers.ReleaseInstance(fixture.Layer);
	Require(std::get<float>(fixture.Call("getRenderDelay")) == 0 && std::get<std::int32_t>(fixture.Call("calcShowCount", {std::int32_t{100}})) == 0, "detached Text wrapper returns native zero query values");
	fixture.Text.ReleaseInstance(fixture.Instance);
	RequireFailure([&] {
		static_cast<void>(fixture.Call("calcShowCount", {std::int32_t{100}}));
	},
	    "live native wrapper");
}

int main() {
	try {
		TestOriginalFormat4PrimitiveWords();
		TestRealFontPreparationDrawAndCacheRetirement();
		TestActualRenderedCountGetterAndRetirement();
		TestActualKeyWaitGetterAndRetirement();
		TestActualRevealQueries();
		std::cout << "PASS native format4 words and connected actual Text/FontInfo/atlas/cache lifecycle\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
