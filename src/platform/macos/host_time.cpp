#include "sgre/platform/host_time.hpp"

#include <ctime>
#include <limits>
#include <stdexcept>
#include <utility>

namespace sgre::platform {

	std::int64_t ReadHostUnixSeconds() {
		const auto now = std::time(nullptr);
		if (now == std::time_t{-1} || !std::in_range<std::int64_t>(now)) {
			throw std::runtime_error("macOS Unix timestamp exceeds the native signed64 domain");
		}

		return static_cast<std::int64_t>(now);
	}

	HostLocalDateTime ReadHostLocalDateTime() {
		const auto now = std::time(nullptr);
		std::tm local{};
		if (now == std::time_t{-1} || localtime_r(&now, &local) == nullptr) {
			throw std::runtime_error("macOS local calendar conversion failed");
		}

		const auto year = static_cast<std::int64_t>(local.tm_year) + 1900;
		if (year < std::numeric_limits<std::int32_t>::min() || year > std::numeric_limits<std::int32_t>::max()) {
			throw std::runtime_error("macOS local year exceeds the script integer range");
		}

		HostLocalDateTime result;
		result.Second = local.tm_sec;
		result.Minute = local.tm_min;
		result.Hour = local.tm_hour;
		result.MonthDay = local.tm_mday;
		result.Month = local.tm_mon + 1;
		result.Year = static_cast<std::int32_t>(year);
		result.WeekDay = local.tm_wday;
		result.YearDay = local.tm_yday;

		return result;
	}

} // namespace sgre::platform
