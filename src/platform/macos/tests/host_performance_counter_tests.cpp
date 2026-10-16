#include "sgre/platform/host_performance_counter.hpp"

#include "sgre/core/frame_clock.hpp"
#include "sgre/core/performance_counter.hpp"

#include <array>
#include <exception>
#include <iostream>
#include <mach/mach_time.h>
#include <stdexcept>
#include <thread>

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

int main() {
	try {
		sgre::platform::HostPerformanceCounter host;
		mach_timebase_info_data_t timebase{};
		Require(mach_timebase_info(&timebase) == KERN_SUCCESS && timebase.numer != 0 && timebase.denom != 0, "actual test host timebase unavailable");
		const auto expected_frequency = std::uint64_t{1000000000} * timebase.denom / timebase.numer;
		Require(host.Frequency() > 0 && static_cast<std::uint64_t>(host.Frequency()) == expected_frequency, "host frequency differs from actual OS timebase");
		const auto before = host.ReadRawCounter();
		const auto milliseconds = host.ReadMilliseconds();
		const auto after = host.ReadRawCounter();
		Require(after >= before && milliseconds >= sgre::core::PerformanceCounterMilliseconds(static_cast<std::int64_t>(before), host.Frequency()) && milliseconds <= sgre::core::PerformanceCounterMilliseconds(static_cast<std::int64_t>(after), host.Frequency()), "actual millisecond projection is outside its containing OS sample interval");
		std::array<std::exception_ptr, 2> failures;
		std::array<std::thread, 2> workers;
		try {
			for (std::size_t index = 0; index < workers.size(); ++index) {
				workers[index] = std::thread([&, index] {
					try {
						auto previous = host.ReadRawCounter();
						for (std::size_t sample = 0; sample < 4096; ++sample) {
							const auto current = host.ReadRawCounter();
							Require(current >= previous, "actual host raw counter regressed on a worker");
							previous = current;
						}
					} catch (...) {
						failures[index] = std::current_exception();
					}
				});
			}
		} catch (...) {
			for (auto& worker : workers) {
				if (worker.joinable()) {
					worker.join();
				}
			}
			throw;
		}

		for (auto& worker : workers) {
			worker.join();
		}
		for (const auto& failure : failures) {
			if (failure) {
				std::rethrow_exception(failure);
			}
		}
		sgre::core::FrameClock clock(milliseconds);
		const auto next = host.ReadMilliseconds();
		Require(next >= milliseconds && clock.Advance(next) >= 1, "actual portable clock rejected the host millisecond input");
		std::cout << "Actual macOS counter frequency=" << host.Frequency() << "; contained projection and 8192 concurrent samples PASS\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
