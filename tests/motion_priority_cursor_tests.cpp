#include "sgre/scene/motion_priority_cursor.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

static void Require(bool condition, std::string_view context) {
	if (!condition) {
		throw std::runtime_error(std::string(context));
	}
}

template <typename Operation>
static void RequireFailure(Operation operation) {
	try {
		operation();
	} catch (const std::runtime_error&) {
		return;
	}

	throw std::runtime_error("priority cursor accepted unsupported preparation or evaluation");
}

static sgre::scene::PreparedMotionTimeline Track() {
	sgre::scene::PreparedMotionTimeline timeline;
	timeline.LastTime = 4;
	for (const auto time : std::array<float, 4>{0, 2, 2, 4}) {
		sgre::scene::PreparedMotionPriorityFrame frame;
		frame.Time = time;
		frame.DrawOrder.push_back(static_cast<std::uint32_t>(timeline.Priority.size()));
		timeline.Priority.push_back(std::move(frame));
	}

	return timeline;
}

static void TestCursor() {
	const auto timeline = Track();
	sgre::scene::MotionPriorityCursor cursor(timeline);
	Require(!cursor.State().Initialized && cursor.State().Index == 0 && cursor.State().CurrentTime == 0 && cursor.State().FollowingTime == 0 && cursor.DrawOrder().empty(), "constructor must retain empty native priority output before initial seek");
	RequireFailure([&] {
		cursor.Forward(0);
	});
	cursor.Seek(2);
	Require(cursor.State().Index == 1 && cursor.State().CurrentTime == 2 && cursor.State().FollowingTime == 2 && cursor.DrawOrder().front() == 1, "native seek must select the first exact duplicate time");
	Require(cursor.DrawOrder().data() == timeline.Priority[1].DrawOrder.data(), "priority output must borrow immutable preparation without a frame allocation");
	cursor.Forward(2);
	Require(cursor.State().Index == 2 && cursor.DrawOrder().front() == 2, "forward must cross an equal following boundary");
	cursor.Reverse(2);
	Require(cursor.State().Index == 2, "reverse must retain the current frame at exact time");
	cursor.Reverse(1);
	Require(cursor.State().Index == 0, "reverse must cross every strictly greater current boundary");
	cursor.Seek(4);
	Require(cursor.State().Index == 2 && cursor.State().FollowingTime == 4 && cursor.DrawOrder().front() == 2, "exact terminal seek must clamp to count minus two");
	cursor.Forward(4);
	Require(cursor.State().Index == 2, "forward must never activate terminal content");
	RequireFailure([&] {
		cursor.Seek(5);
	});
	RequireFailure([&] {
		cursor.Seek(-1);
	});
	RequireFailure([&] {
		cursor.Seek(std::numeric_limits<float>::quiet_NaN());
	});
	RequireFailure([&] {
		auto invalid = Track();
		invalid.Priority.resize(1);
		sgre::scene::MotionPriorityCursor rejected(invalid);
	});
	RequireFailure([&] {
		auto invalid = Track();
		invalid.Priority.back().Time = 3;
		sgre::scene::MotionPriorityCursor rejected(invalid);
	});
	RequireFailure([&] {
		auto invalid = Track();
		invalid.Priority[1].Time = 3;
		sgre::scene::MotionPriorityCursor rejected(invalid);
	});
}

static std::uint8_t ReadByte(std::istream& input) {
	const auto value = input.get();
	if (value == std::char_traits<char>::eof()) {
		throw std::runtime_error("native priority cursor fixture is truncated");
	}

	return static_cast<std::uint8_t>(value);
}

static std::uint32_t ReadU32(std::istream& input) {
	std::uint32_t value = 0;
	for (std::uint32_t byte = 0; byte < 4; ++byte) {
		value |= static_cast<std::uint32_t>(ReadByte(input)) << (byte * 8);
	}

	return value;
}

static float ReadFloat(std::istream& input) {
	return std::bit_cast<float>(ReadU32(input));
}

static std::vector<std::uint32_t> ReadIndices(std::istream& input) {
	const auto count = ReadU32(input);
	Require(count <= 32, "native priority fixture output exceeds its bound");
	std::vector<std::uint32_t> values;
	values.reserve(count);
	for (std::uint32_t index = 0; index < count; ++index) {
		values.push_back(ReadU32(input));
	}

	return values;
}

static void ReplayNative(const char* path) {
	std::ifstream input(path, std::ios::binary);
	Require(input.good(), "native priority cursor fixture is unavailable");
	for (const auto byte : std::array<std::uint8_t, 4>{'M', 'T', 'D', '1'}) {
		Require(ReadByte(input) == byte, "native priority cursor fixture signature differs");
	}

	const auto cases = ReadU32(input);
	Require(cases > 0 && cases <= 4096, "native priority cursor fixture case count exceeds its bound");
	std::uint32_t transitions = 0;
	for (std::uint32_t case_index = 0; case_index < cases; ++case_index) {
		const auto frames = ReadU32(input);
		const auto steps = ReadU32(input);
		Require(frames >= 2 && frames <= 8 && steps <= 4096, "native priority cursor fixture track or step count exceeds its bound");
		sgre::scene::PreparedMotionTimeline timeline;
		for (std::uint32_t index = 0; index < frames; ++index) {
			sgre::scene::PreparedMotionPriorityFrame frame;
			frame.Time = ReadFloat(input);
			frame.DrawOrder = ReadIndices(input);
			std::reverse(frame.DrawOrder.begin(), frame.DrawOrder.end());
			timeline.Priority.push_back(std::move(frame));
		}

		timeline.LastTime = timeline.Priority.back().Time;
		sgre::scene::MotionPriorityCursor cursor(timeline);
		for (std::uint32_t step = 0; step < steps; ++step) {
			const auto mode = ReadByte(input);
			const auto position = ReadFloat(input);
			const auto expected_index = ReadU32(input);
			const auto expected_current = ReadU32(input);
			const auto expected_following = ReadU32(input);
			auto expected_order = ReadIndices(input);
			std::reverse(expected_order.begin(), expected_order.end());
			Require(mode <= 2, "native priority cursor fixture traversal is invalid");
			if (mode == 0) {
				cursor.Seek(position);
			} else if (mode == 1) {
				cursor.Forward(position);
			} else {
				cursor.Reverse(position);
			}

			const auto& state = cursor.State();
			Require(state.Initialized && state.Index == expected_index && std::bit_cast<std::uint32_t>(state.CurrentTime) == expected_current && std::bit_cast<std::uint32_t>(state.FollowingTime) == expected_following, "native priority cursor state differs");
			Require(std::ranges::equal(cursor.DrawOrder(), expected_order), "native priority publication order differs");
			++transitions;
		}
	}

	Require(input.peek() == std::char_traits<char>::eof(), "native priority cursor fixture has trailing bytes");
	std::cout << "Replayed " << transitions << " native priority cursor transitions\n";
}

int main(int argc, char** argv) {
	try {
		Require(argc == 1 || argc == 2, "usage: motion_priority_cursor_tests [native-track-domain-fixture]");
		TestCursor();
		if (argc == 2) {
			ReplayNative(argv[1]);
		}

		std::cout << "motion priority cursor tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
