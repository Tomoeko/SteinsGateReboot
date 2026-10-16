#pragma once

#include <array>
#include <cstdint>

namespace sgre::test {
	enum class EmoteSkipNativePhase {
		Idle,
		Queued,
		Active,
		ActiveWithPending
	};

	struct EmoteSkipNativeCase {
		EmoteSkipNativePhase Phase;
		std::uint32_t AngleWord;
		std::array<std::uint32_t, 2> Coordinate;
		std::uint32_t Scale;
		std::array<std::uint32_t, 4> Color;
		std::uint32_t Rotation;
	};

	// Expected-only projection of original Skip, native count1/disabled Common; source JSON is pinned by the recovery manifest.
	inline constexpr std::array<EmoteSkipNativeCase, 12> kEmoteSkipNativeCases{{
	    {EmoteSkipNativePhase::Idle, 0x3fc00000u, {0x00000000u, 0x00000000u}, 0x00000000u, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, 0x00000000u},
	    {EmoteSkipNativePhase::Idle, 0xc1080000u, {0x00000000u, 0x00000000u}, 0x00000000u, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, 0x00000000u},
	    {EmoteSkipNativePhase::Idle, 0x41080000u, {0x00000000u, 0x00000000u}, 0x00000000u, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, 0x00000000u},
	    {EmoteSkipNativePhase::Queued, 0x3fc00000u, {0x41000000u, 0x41100000u}, 0x41000000u, {0x41000000u, 0x41100000u, 0x41200000u, 0x41300000u}, 0x3fc00000u},
	    {EmoteSkipNativePhase::Queued, 0xc1080000u, {0x41000000u, 0x41100000u}, 0x41000000u, {0x41000000u, 0x41100000u, 0x41200000u, 0x41300000u}, 0x40821fb6u},
	    {EmoteSkipNativePhase::Queued, 0x41080000u, {0x41000000u, 0x41100000u}, 0x41000000u, {0x41000000u, 0x41100000u, 0x41200000u, 0x41300000u}, 0x400de04au},
	    {EmoteSkipNativePhase::Active, 0x3fc00000u, {0x41000000u, 0x41100000u}, 0x41000000u, {0x41000000u, 0x41100000u, 0x41200000u, 0x41300000u}, 0x3fc00000u},
	    {EmoteSkipNativePhase::Active, 0xc1080000u, {0x41000000u, 0x41100000u}, 0x41000000u, {0x41000000u, 0x41100000u, 0x41200000u, 0x41300000u}, 0x40821fb6u},
	    {EmoteSkipNativePhase::Active, 0x41080000u, {0x41000000u, 0x41100000u}, 0x41000000u, {0x41000000u, 0x41100000u, 0x41200000u, 0x41300000u}, 0x400de04au},
	    {EmoteSkipNativePhase::ActiveWithPending, 0x3fc00000u, {0x41000000u, 0x41100000u}, 0x41000000u, {0x41000000u, 0x41100000u, 0x41200000u, 0x41300000u}, 0x40990fdbu},
	    {EmoteSkipNativePhase::ActiveWithPending, 0xc1080000u, {0x41000000u, 0x41100000u}, 0x41000000u, {0x41000000u, 0x41100000u, 0x41200000u, 0x41300000u}, 0x400de04au},
	    {EmoteSkipNativePhase::ActiveWithPending, 0x41080000u, {0x41000000u, 0x41100000u}, 0x41000000u, {0x41000000u, 0x41100000u, 0x41200000u, 0x41300000u}, 0x40821fb6u},
	}};
} // namespace sgre::test
