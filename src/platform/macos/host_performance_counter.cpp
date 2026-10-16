#include "sgre/platform/host_performance_counter.hpp"

#include "sgre/core/performance_counter.hpp"

#include <limits>
#include <mach/mach_time.h>
#include <stdexcept>

namespace sgre::platform {

	HostPerformanceCounter::HostPerformanceCounter() {
		mach_timebase_info_data_t timebase{};
		if (mach_timebase_info(&timebase) != KERN_SUCCESS || timebase.numer == 0 || timebase.denom == 0) {
			throw std::runtime_error("macOS performance counter timebase is unavailable");
		}

		constexpr std::uint64_t kNanosecondsPerSecond = 1000000000;
		const auto numerator = kNanosecondsPerSecond * timebase.denom;
		if (numerator % timebase.numer != 0 || numerator / timebase.numer > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
			throw std::runtime_error("macOS performance counter frequency cannot preserve the original integer-frequency projection");
		}

		m_Frequency = static_cast<std::int64_t>(numerator / timebase.numer);
	}

	std::uint64_t HostPerformanceCounter::ReadRawCounter() const {
		// Both Windows QPC and this host counter advance while the machine sleeps.
		const auto counter = mach_continuous_time();
		if (counter > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
			throw std::runtime_error("macOS performance counter exceeds the recovered signed input domain");
		}

		return counter;
	}

	std::int64_t HostPerformanceCounter::Frequency() const noexcept {
		return m_Frequency;
	}

	std::uint64_t HostPerformanceCounter::ReadMilliseconds() const {
		return core::PerformanceCounterMilliseconds(static_cast<std::int64_t>(ReadRawCounter()), m_Frequency);
	}

} // namespace sgre::platform
