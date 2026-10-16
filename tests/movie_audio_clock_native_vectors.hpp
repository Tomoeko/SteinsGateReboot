#pragma once

#include <array>
#include <cstdint>

struct NativeMovieAudioClockCase {
	std::uint64_t CompletedFrames;
	std::int64_t SampleEpoch100ns;
	std::int64_t EndClock100ns;
	std::int64_t QueryClock100ns;
	std::uint64_t PositionSecondsWord;
};

// Original38e720/38e670 callbacks and whole38f080 consumer, using explicit PCM16/32 format inputs.
static constexpr std::array<NativeMovieAudioClockCase, 16> kNativeMovieAudioClockCases{{
    {648u, 0, 100200000, 100270000, 0x3f94fdf3c0000000ull},
    {648u, 0, 101200000, 101270000, 0x3f94fdf3c0000000ull},
    {648u, 0, 102200000, 102270000, 0x3f94fdf3c0000000ull},
    {648u, 0, 103200000, 103270000, 0x3f94fdf3c0000000ull},
    {960u, 0, 104200000, 104270000, 0x3f9ba5e360000000ull},
    {960u, 0, 105200000, 105270000, 0x3f9ba5e360000000ull},
    {960u, 0, 106200000, 106270000, 0x3f9ba5e360000000ull},
    {960u, 0, 107200000, 107270000, 0x3f9ba5e360000000ull},
    {1920u, 65000, 108200000, 108270000, 0x3fab645a20000000ull},
    {1920u, 65000, 109200000, 109270000, 0x3fab645a20000000ull},
    {1920u, 65000, 110200000, 110270000, 0x3fab645a20000000ull},
    {1920u, 65000, 111200000, 111270000, 0x3fab645a20000000ull},
    {3840u, 65000, 112200000, 112270000, 0x3fb7ef9dc0000000ull},
    {3840u, 65000, 113200000, 113270000, 0x3fb7ef9dc0000000ull},
    {3840u, 65000, 114200000, 114270000, 0x3fb7ef9dc0000000ull},
    {3840u, 65000, 115200000, 115270000, 0x3fb7ef9dc0000000ull},
}};
