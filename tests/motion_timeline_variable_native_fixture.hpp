#pragma once

#include <array>
#include <cstdint>

// Original50e750/511640/518160/50a6b0/51afb0/50c550, five cases and eleven genuine phases.
// Expected words are outputs only; the connected test builds actual serialized records and ordinary tasks.
struct NativeTimelineVariableState {
	std::uint32_t EvaluationBits;
	std::uint32_t CurrentSlot;
	std::array<std::uint32_t, 2> FrameIndices;
	std::array<bool, 2> Hidden;
	std::array<bool, 2> Tween;
	std::array<std::uint32_t, 2> CellValueBits;
	std::uint32_t ValueBits;
	std::uint32_t ParameterBits;
	std::uint32_t LocalXBits;
};

static constexpr std::array<std::array<NativeTimelineVariableState, 11>, 5> kNativeTimelineVariableStates{{
    {{
        {0x00000000u, 0, {0, 1}, {false, false}, {true, true}, {0xbf000000u, 0x3fc00000u}, 0xbf000000u, 0x00000000u, 0x00000000u},  // seek0
        {0x402ccccdu, 0, {0, 1}, {false, false}, {true, true}, {0xbf000000u, 0x3fc00000u}, 0x3f147ae2u, 0x3f147ae2u, 0x00000000u},  // forward2.7
        {0x40a00000u, 1, {2, 1}, {false, false}, {false, true}, {0x3e800000u, 0x3fc00000u}, 0x3fc00000u, 0x3fc00000u, 0x00000000u}, // forward5
        {0x40eccccdu, 1, {2, 1}, {false, false}, {false, true}, {0x3e800000u, 0x3fc00000u}, 0x3f666666u, 0x3f666666u, 0x00000000u}, // forward7.4
        {0x4099999au, 0, {0, 1}, {false, false}, {true, true}, {0xbf000000u, 0x3fc00000u}, 0x3fb5c290u, 0x3fb5c290u, 0x00000000u},  // reverse4.8
        {0x41200000u, 0, {2, 3}, {false, true}, {false, true}, {0x3e800000u, 0x3fc00000u}, 0x3e800000u, 0x3e800000u, 0x00000000u},  // forward10
        {0x41300000u, 0, {2, 3}, {false, true}, {false, true}, {0x3e800000u, 0x3fc00000u}, 0x3e800000u, 0x3e800000u, 0x00000000u},  // forward11
        {0x41500000u, 0, {0, 1}, {false, false}, {true, true}, {0xbf000000u, 0x3fc00000u}, 0x40966666u, 0x40966666u, 0x00000000u},  // seek13
        {0x40c00000u, 0, {0, 1}, {false, false}, {true, true}, {0xbf000000u, 0x3fc00000u}, 0x3ff33334u, 0x3ff33334u, 0x00000000u},  // reverse6
        {0x40c00000u, 0, {0, 1}, {false, false}, {true, true}, {0xbf000000u, 0x3fc00000u}, 0x3ff33334u, 0x3ff33334u, 0x00000000u},  // retained6
        {0x40c00000u, 0, {0, 1}, {false, false}, {true, true}, {0xbf000000u, 0x3fc00000u}, 0x3ff33334u, 0x3ff33334u, 0x00000000u},  // clear-retained
    }},
    {{
        {0x00000000u, 0, {0, 1}, {false, false}, {true, true}, {0xbf000000u, 0x3fc00000u}, 0xbf000000u, 0x00000000u, 0x00000000u},  // seek0
        {0x402ccccdu, 0, {0, 1}, {false, false}, {true, true}, {0xbf000000u, 0x3fc00000u}, 0x3e99999au, 0x3e99999au, 0x00000000u},  // forward2.7
        {0x40a00000u, 1, {2, 1}, {false, false}, {false, true}, {0x3e800000u, 0x3fc00000u}, 0x3fc00000u, 0x3fc00000u, 0x00000000u}, // forward5
        {0x40eccccdu, 1, {2, 1}, {false, false}, {false, true}, {0x3e800000u, 0x3fc00000u}, 0x3f800000u, 0x3f800000u, 0x00000000u}, // forward7.4
        {0x4099999au, 0, {0, 1}, {false, false}, {true, true}, {0xbf000000u, 0x3fc00000u}, 0x3f8ccccdu, 0x3f8ccccdu, 0x00000000u},  // reverse4.8
        {0x41200000u, 0, {2, 3}, {false, true}, {false, true}, {0x3e800000u, 0x3fc00000u}, 0x3e800000u, 0x3e800000u, 0x00000000u},  // forward10
        {0x41300000u, 0, {2, 3}, {false, true}, {false, true}, {0x3e800000u, 0x3fc00000u}, 0x3e800000u, 0x3e800000u, 0x00000000u},  // forward11
        {0x41500000u, 0, {0, 1}, {false, false}, {true, true}, {0xbf000000u, 0x3fc00000u}, 0x40966666u, 0x40966666u, 0x00000000u},  // seek13
        {0x40c00000u, 0, {0, 1}, {false, false}, {true, true}, {0xbf000000u, 0x3fc00000u}, 0x3ff33334u, 0x3ff33334u, 0x00000000u},  // reverse6
        {0x40c00000u, 0, {0, 1}, {false, false}, {true, true}, {0xbf000000u, 0x3fc00000u}, 0x3ff33334u, 0x3ff33334u, 0x00000000u},  // retained6
        {0x40c00000u, 0, {0, 1}, {false, false}, {true, true}, {0xbf000000u, 0x3fc00000u}, 0x3ff33334u, 0x3ff33334u, 0x00000000u},  // clear-retained
    }},
    {{
        {0x00000000u, 0, {0, 1}, {false, false}, {true, true}, {0xbf000000u, 0x3fc00000u}, 0xbf000000u, 0x00000000u, 0x00000000u},  // seek0
        {0x402ccccdu, 0, {0, 1}, {false, false}, {true, true}, {0xbf000000u, 0x3fc00000u}, 0x3e99999au, 0x3e99999au, 0x00000000u},  // forward2.7
        {0x40a00000u, 1, {2, 1}, {false, false}, {false, true}, {0x3e800000u, 0x3fc00000u}, 0x3fc00000u, 0x3fc00000u, 0x00000000u}, // forward5
        {0x40eccccdu, 1, {2, 1}, {false, false}, {false, true}, {0x3e800000u, 0x3fc00000u}, 0x3f800000u, 0x3f800000u, 0x00000000u}, // forward7.4
        {0x4099999au, 0, {0, 1}, {false, false}, {true, true}, {0xbf000000u, 0x3fc00000u}, 0x3f8ccccdu, 0x3f8ccccdu, 0x00000000u},  // reverse4.8
        {0x41200000u, 0, {2, 3}, {false, true}, {false, true}, {0x3e800000u, 0x3fc00000u}, 0x3e800000u, 0x3e800000u, 0x00000000u},  // forward10
        {0x41300000u, 0, {2, 3}, {false, true}, {false, true}, {0x3e800000u, 0x3fc00000u}, 0x3e800000u, 0x3e800000u, 0x00000000u},  // forward11
        {0x41500000u, 0, {0, 1}, {false, false}, {true, true}, {0xbf000000u, 0x3fc00000u}, 0x4089999au, 0x4089999au, 0x00000000u},  // seek13
        {0x40c00000u, 0, {0, 1}, {false, false}, {true, true}, {0xbf000000u, 0x3fc00000u}, 0x3ff33334u, 0x3ff33334u, 0x00000000u},  // reverse6
        {0x40c00000u, 0, {0, 1}, {false, false}, {true, true}, {0xbf000000u, 0x3fc00000u}, 0x3ff33334u, 0x3ff33334u, 0x00000000u},  // retained6
        {0x40c00000u, 0, {0, 1}, {false, false}, {true, true}, {0xbf000000u, 0x3fc00000u}, 0x3ff33334u, 0x3ff33334u, 0x00000000u},  // clear-retained
    }},
    {{
        {0x00000000u, 0, {0, 1}, {false, false}, {true, true}, {0xbf000000u, 0x3fc00000u}, 0xbf000000u, 0x00000000u, 0x00000000u},  // seek0
        {0x402ccccdu, 0, {0, 1}, {false, false}, {true, true}, {0xbf000000u, 0x3fc00000u}, 0xbf000000u, 0x00000000u, 0x00000000u},  // forward2.7
        {0x40a00000u, 1, {2, 1}, {false, false}, {false, true}, {0x3e800000u, 0x3fc00000u}, 0x3fc00000u, 0x3fc00000u, 0x00000000u}, // forward5
        {0x40eccccdu, 1, {2, 1}, {false, false}, {false, true}, {0x3e800000u, 0x3fc00000u}, 0x3fc00000u, 0x3fc00000u, 0x00000000u}, // forward7.4
        {0x4099999au, 0, {0, 1}, {false, false}, {true, true}, {0xbf000000u, 0x3fc00000u}, 0x3f333334u, 0x3f333334u, 0x00000000u},  // reverse4.8
        {0x41200000u, 0, {2, 3}, {false, true}, {false, true}, {0x3e800000u, 0x3fc00000u}, 0x3e800000u, 0x3e800000u, 0x00000000u},  // forward10
        {0x41300000u, 0, {2, 3}, {false, true}, {false, true}, {0x3e800000u, 0x3fc00000u}, 0x3e800000u, 0x3e800000u, 0x00000000u},  // forward11
        {0x41500000u, 0, {0, 1}, {false, false}, {true, true}, {0xbf000000u, 0x3fc00000u}, 0x4089999au, 0x4089999au, 0x00000000u},  // seek13
        {0x40c00000u, 0, {0, 1}, {false, false}, {true, true}, {0xbf000000u, 0x3fc00000u}, 0x3ff33334u, 0x3ff33334u, 0x00000000u},  // reverse6
        {0x40c00000u, 0, {0, 1}, {false, false}, {true, true}, {0xbf000000u, 0x3fc00000u}, 0x3ff33334u, 0x3ff33334u, 0x00000000u},  // retained6
        {0x40c00000u, 0, {0, 1}, {false, false}, {true, true}, {0xbf000000u, 0x3fc00000u}, 0x3ff33334u, 0x3ff33334u, 0x00000000u},  // clear-retained
    }},
    {{
        {0x00000000u, 0, {0, 1}, {false, false}, {true, true}, {0xbf000000u, 0x3fc00000u}, 0xbf000000u, 0x00000000u, 0x00000000u},  // seek0
        {0x402ccccdu, 0, {0, 1}, {false, false}, {true, true}, {0xbf000000u, 0x3fc00000u}, 0xbf000000u, 0x00000000u, 0x00000000u},  // forward2.7
        {0x40a00000u, 1, {2, 1}, {false, false}, {false, true}, {0x3e800000u, 0x3fc00000u}, 0x3fc00000u, 0x3fc00000u, 0x41700000u}, // forward5
        {0x40eccccdu, 1, {2, 1}, {false, false}, {false, true}, {0x3e800000u, 0x3fc00000u}, 0x3fc00000u, 0x3fc00000u, 0x41700000u}, // forward7.4
        {0x4099999au, 0, {0, 1}, {false, false}, {true, true}, {0xbf000000u, 0x3fc00000u}, 0x3f8ccccdu, 0x3f8ccccdu, 0x41300000u},  // reverse4.8
        {0x41200000u, 0, {2, 3}, {false, true}, {false, true}, {0x3e800000u, 0x3fc00000u}, 0x3e800000u, 0x3e800000u, 0x40200000u},  // forward10
        {0x41300000u, 0, {2, 3}, {false, true}, {false, true}, {0x3e800000u, 0x3fc00000u}, 0x3e800000u, 0x3e800000u, 0x40200000u},  // forward11
        {0x41500000u, 0, {0, 1}, {false, false}, {true, true}, {0xbf000000u, 0x3fc00000u}, 0x4089999au, 0x4089999au, 0x422c0000u},  // seek13
        {0x40c00000u, 0, {0, 1}, {false, false}, {true, true}, {0xbf000000u, 0x3fc00000u}, 0x3f8ccccdu, 0x3f8ccccdu, 0x41300000u},  // reverse6
        {0x40c00000u, 0, {0, 1}, {false, false}, {true, true}, {0xbf000000u, 0x3fc00000u}, 0x3f8ccccdu, 0x3f8ccccdu, 0x41300000u},  // retained6
        {0x40c00000u, 0, {0, 1}, {false, false}, {true, true}, {0xbf000000u, 0x3fc00000u}, 0x3f8ccccdu, 0x3f8ccccdu, 0x00000000u},  // clear-retained
    }},
}};
