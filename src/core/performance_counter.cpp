#include "sgre/core/performance_counter.hpp"

#include <cmath>
#include <stdexcept>

namespace sgre::core {
	std::uint64_t PerformanceCounterMilliseconds(std::int64_t actual_counter, std::int64_t actual_frequency) {
		if (actual_counter < 0 || actual_frequency <= 0) {
			throw std::runtime_error("performance counter requires its nonnegative signed sample and positive actual frequency");
		}

		const auto counter = static_cast<double>(actual_counter);
		const auto frequency = static_cast<double>(actual_frequency);
		const auto seconds = counter / frequency;
		const auto milliseconds = seconds * 1000.0;
		if (!std::isfinite(milliseconds) || milliseconds >= 0x1p64) {
			throw std::runtime_error("performance counter milliseconds exceed the recovered unsigned output domain");
		}

		return static_cast<std::uint64_t>(milliseconds);
	}
} // namespace sgre::core
