#include "sgre/core/performance_counter.hpp"

#include "sgre/core/frame_clock.hpp"
#include "sgre/io/file_reader.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Operation>
static void RequireFailure(Operation operation) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find("performance counter") != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("invalid performance-counter domain was accepted");
}

static void TestIndependentUnitVectors() {
	Require(sgre::core::PerformanceCounterMilliseconds(0, 1) == 0 && sgre::core::PerformanceCounterMilliseconds(1, 1) == 1000, "original conversion preserves seconds-to-milliseconds units");
	Require(sgre::core::PerformanceCounterMilliseconds(9999, 10000000) == 0 && sgre::core::PerformanceCounterMilliseconds(10000, 10000000) == 1 && sgre::core::PerformanceCounterMilliseconds(10001, 10000000) == 1, "original conversion truncates fractional milliseconds after separate double operations");
	Require(sgre::core::PerformanceCounterMilliseconds(std::numeric_limits<std::int64_t>::max(), 1000) == 0x8000000000000000u, "positive output above the signed range retains the original unsigned conversion branch");
}

static void TestAdmission() {
	RequireFailure([] {
		static_cast<void>(sgre::core::PerformanceCounterMilliseconds(-1, 1000));
	});
	RequireFailure([] {
		static_cast<void>(sgre::core::PerformanceCounterMilliseconds(1, 0));
	});
	RequireFailure([] {
		static_cast<void>(sgre::core::PerformanceCounterMilliseconds(1, -1));
	});
	RequireFailure([] {
		static_cast<void>(sgre::core::PerformanceCounterMilliseconds(std::numeric_limits<std::int64_t>::max(), 1));
	});
}

static void TestActualFrameClockUnits() {
	const auto initial = sgre::core::PerformanceCounterMilliseconds(1000000, 10000000);
	sgre::core::FrameClock clock(initial);
	clock.SetVariableFrame(true);
	Require(clock.Advance(sgre::core::PerformanceCounterMilliseconds(2000000, 10000000)) == 6 && clock.ResidualTicks() == 0, "the actual portable frame clock receives milliseconds once and advances six ticks per hundred milliseconds");
	Require(clock.Advance(sgre::core::PerformanceCounterMilliseconds(2000000, 10000000)) == 1 && clock.ResidualTicks() == -1, "same-counter samples preserve the original minimum-step and residual behavior");
}

class FixtureReader {
public:
	explicit FixtureReader(std::span<const std::uint8_t> bytes) : m_Bytes(bytes) {
	}

	std::uint64_t ReadUnsigned(std::size_t width) {
		if (width == 0 || width > 8 || m_Offset > m_Bytes.size() || width > m_Bytes.size() - m_Offset) {
			throw std::runtime_error("native counter fixture field exceeds bounded input");
		}

		std::uint64_t result = 0;
		for (std::size_t index = 0; index < width; ++index) {
			result |= static_cast<std::uint64_t>(m_Bytes[m_Offset++]) << (index * 8);
		}

		return result;
	}

private:
	std::span<const std::uint8_t> m_Bytes;
	std::size_t m_Offset = 0;
};

static void TestNativeFixture(const char* path) {
	sgre::io::FileReader file(path);
	Require(file.Size() >= 8 && file.Size() <= 256 * 1024, "native counter fixture exceeds its bounded record budget");
	const auto bytes = file.Read(0, static_cast<std::size_t>(file.Size()));
	FixtureReader reader(bytes);
	Require(reader.ReadUnsigned(4) == 0x534d4350u, "native counter fixture magic must be PCMS");
	const auto count = reader.ReadUnsigned(4);
	Require(count > 0 && count <= 8192 && bytes.size() == 8 + count * 24, "native counter fixture has invalid count, truncated or trailing records");
	for (std::uint64_t index = 0; index < count; ++index) {
		const auto counter = reader.ReadUnsigned(8);
		const auto frequency = reader.ReadUnsigned(8);
		const auto expected = reader.ReadUnsigned(8);
		Require(counter <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) && frequency <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()), "native fixture input exceeds its admitted positive signed counter domain");
		Require(sgre::core::PerformanceCounterMilliseconds(static_cast<std::int64_t>(counter), static_cast<std::int64_t>(frequency)) == expected, "counter-to-millisecond output differs from the original instructions");
	}
	std::cout << count << " original counter-conversion records match\n";
}

int main(int argc, char** argv) {
	try {
		Require(argc == 1 || argc == 2, "usage: performance_counter_tests [native-counter-clock.bin]");
		TestIndependentUnitVectors();
		TestAdmission();
		TestActualFrameClockUnits();
		if (argc == 2) {
			TestNativeFixture(argv[1]);
		}
		std::cout << "Performance counter groups PASS\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
