#pragma once

#include <array>
#include <cstdint>

// Original309af0/30da60/30c760 captures: synthetic definitions, never authored runtime state.
struct NativeZeroStringOutput {
	std::uint8_t Degree = 0;
	bool Unordered = false;
	std::array<std::array<std::uint32_t, 2>, 20> Controls{};
};

inline constexpr std::array<NativeZeroStringOutput, 3> kNativeZeroStringOutputs{{
    {0x43, true, {{
                     {0x7fc00000, 0x7fc00000},
                     {0x7fc00000, 0x7fc00000},
                     {0x7fc00000, 0x7fc00000},
                     {0x7fc00000, 0x7fc00000},
                     {0x7fc00000, 0x7fc00000},
                     {0x7fc00000, 0x7fc00000},
                     {0x7fc00000, 0x7fc00000},
                     {0x7fc00000, 0x7fc00000},
                     {0x7fc00000, 0x7fc00000},
                     {0x7fc00000, 0x7fc00000},
                     {0x7fc00000, 0x7fc00000},
                     {0x7fc00000, 0x7fc00000},
                     {0x7fc00000, 0x7fc00000},
                     {0x7fc00000, 0x7fc00000},
                     {0x7fc00000, 0x7fc00000},
                     {0x7fc00000, 0x7fc00000},
                     {0x7fc00000, 0x7fc00000},
                     {0x7fc00000, 0x7fc00000},
                     {0x7fc00000, 0x7fc00000},
                     {0x7fc00000, 0x7fc00000},
                 }}},
    {0x34, true, {{
                     {0x7fc00000, 0x7fc00000},
                     {0x7fc00000, 0x7fc00000},
                     {0x7fc00000, 0x7fc00000},
                     {0x7fc00000, 0x7fc00000},
                     {0x7fc00000, 0x7fc00000},
                     {0x7fc00000, 0x7fc00000},
                     {0x7fc00000, 0x7fc00000},
                     {0x7fc00000, 0x7fc00000},
                     {0x7fc00000, 0x7fc00000},
                     {0x7fc00000, 0x7fc00000},
                     {0x7fc00000, 0x7fc00000},
                     {0x7fc00000, 0x7fc00000},
                     {0x7fc00000, 0x7fc00000},
                     {0x7fc00000, 0x7fc00000},
                     {0x7fc00000, 0x7fc00000},
                     {0x7fc00000, 0x7fc00000},
                     {0x7fc00000, 0x7fc00000},
                     {0x7fc00000, 0x7fc00000},
                     {0x7fc00000, 0x7fc00000},
                     {0x7fc00000, 0x7fc00000},
                 }}},
    {0x43, false, {{
                      {0x33800000, 0x00000000},
                      {0x3eaaaaac, 0x00000000},
                      {0x3f2aaaaa, 0xb3000000},
                      {0x3f7fffff, 0xb3000000},
                      {0x33800000, 0x3e800000},
                      {0x3eaaaaab, 0x3e800000},
                      {0x3f2aaaaa, 0x3e800000},
                      {0x3f7ffffe, 0x3e7ffffc},
                      {0x33800000, 0x3f000000},
                      {0x3eaaaaac, 0x3f000000},
                      {0x3f2aaaaa, 0x3f000000},
                      {0x3f7ffffe, 0x3efffffe},
                      {0x33800000, 0x3f400000},
                      {0x3eaaaaac, 0x3f400000},
                      {0x3f2aaaaa, 0x3f400000},
                      {0x3f7fffff, 0x3f400000},
                      {0x33800000, 0x3f800000},
                      {0x3eaaaaac, 0x3f800000},
                      {0x3f2aaaaa, 0x3f800000},
                      {0x3f7fffff, 0x3f800000},
                  }}},
}};
