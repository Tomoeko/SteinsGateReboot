#pragma once

#include <cstdint>

namespace sgre::platform {

	class HostPerformanceCounter {
	public:
		// The frequency is read once from the host timebase; subsequent sampling is thread safe.
		HostPerformanceCounter();
		[[nodiscard]] std::uint64_t ReadRawCounter() const;
		[[nodiscard]] std::int64_t Frequency() const noexcept;
		[[nodiscard]] std::uint64_t ReadMilliseconds() const;

	private:
		std::int64_t m_Frequency = 0;
	};

} // namespace sgre::platform
