#pragma once

#include <cstdint>

namespace sgre::platform {

	struct HostLocalDateTime {
		std::int32_t Second = 0;
		std::int32_t Minute = 0;
		std::int32_t Hour = 0;
		std::int32_t MonthDay = 0;
		std::int32_t Month = 0;
		std::int32_t Year = 0;
		std::int32_t WeekDay = 0;
		std::int32_t YearDay = 0;
	};

	[[nodiscard]] HostLocalDateTime ReadHostLocalDateTime();
	[[nodiscard]] std::int64_t ReadHostUnixSeconds();

} // namespace sgre::platform
