#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

struct NativeMotionParticlePhase {
	std::uint32_t ElapsedBits;
	std::size_t LiveChildren;
	std::size_t DrawRows;
	std::size_t Events;
	std::array<std::uint32_t, 4> Random;
};

// Expected-only outputs of actual registered lookup, spawning, child tick and recursive50ad30.
// Input624 seed words are index*0x9e3779b9+17+case*13, reduced to uint32.
inline constexpr std::array<std::array<NativeMotionParticlePhase, 5>, 2> kNativeMotionParticlePhases{{
    {{
        {0x00000000u, 3, 3, 3, {1138633014u, 2217503189u, 174925174u, 855645271u}},
        {0x3f800000u, 3, 3, 0, {1138633014u, 2217503189u, 174925174u, 855645271u}},
        {0x3f800000u, 3, 3, 0, {1138633014u, 2217503189u, 174925174u, 855645271u}},
        {0x40c00000u, 3, 3, 0, {1138633014u, 2217503189u, 174925174u, 855645271u}},
        {0x00000000u, 0, 0, 0, {1138633014u, 2217503189u, 174925174u, 855645271u}},
    }},
    {{
        {0x00000000u, 3, 3, 3, {2005993919u, 2959298517u, 1042417151u, 1249731441u}},
        {0x3f800000u, 3, 3, 0, {2005993919u, 2959298517u, 1042417151u, 1249731441u}},
        {0x3f800000u, 3, 3, 0, {2005993919u, 2959298517u, 1042417151u, 1249731441u}},
        {0x40c00000u, 3, 3, 0, {2005993919u, 2959298517u, 1042417151u, 1249731441u}},
        {0x00000000u, 0, 0, 0, {2005993919u, 2959298517u, 1042417151u, 1249731441u}},
    }},
}};
