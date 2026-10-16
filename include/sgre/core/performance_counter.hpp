#pragma once

#include <cstdint>

namespace sgre::core {
	// The original signed counter and frequency convert separately to double before division and multiplication.
	// This admitted positive counter domain rejects invalid frequency and unrepresentable millisecond output.
	[[nodiscard]] std::uint64_t PerformanceCounterMilliseconds(std::int64_t actual_counter, std::int64_t actual_frequency);
} // namespace sgre::core
