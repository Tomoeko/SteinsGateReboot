#include "sgre/scene/motion_child_direction.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view context) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("unsupported child direction offset was accepted");
}

static sgre::resource::MotionResource Resource() {
	sgre::resource::MotionResource resource;
	sgre::resource::EasingCurve curve;
	curve.Segments.push_back({{0, 0, 0}, {0.25f, 0.125f, 0}, {0.75f, 0.875f, 0}, {1, 1, 0}});
	resource.Easing.push_back(std::move(curve));

	return resource;
}

static sgre::scene::PreparedMotionFrame Frame(float time) {
	sgre::scene::PreparedMotionFrame frame;
	frame.Time = time;
	frame.Hidden = false;
	frame.Tween = true;
	frame.Attributes.Source = "character";
	frame.Attributes.Interval = 3;

	return frame;
}

static void TestSelectedSlotsAndEasing() {
	const auto resource = Resource();
	auto current = Frame(0);
	const auto following = Frame(1);
	sgre::scene::MotionChildFrameControl current_control;
	current_control.DirectionMode = 1;
	current_control.DirectionOffset = 350;
	current_control.CompleteDirection = true;
	sgre::scene::MotionChildFrameControl following_control;
	following_control.DirectionMode = 1;
	following_control.DirectionOffset = 10;
	Require(sgre::scene::EvaluateMotionChildDirectionOffset(current, following, current_control, following_control, resource, 0.5f) == 0, "complete direction interpolates shortest selected offset and wraps one revolution");
	current.Attributes.EasingIndices[2] = 0;
	current.Attributes.EasingIndices[0] = 99;
	Require(sgre::scene::EvaluateMotionChildDirectionOffset(current, following, current_control, following_control, resource, 0.25f) == 352.5f, "direction interpolation uses the current selected frame's actual angle curve, without coordinate curve or interval quantization");
	sgre::scene::ApplyMotionChildFrameControl(std::nullopt, current_control);
	Require(sgre::scene::EvaluateMotionChildDirectionOffset(current, following, current_control, following_control, resource, 0.25f) == 352.5f, "absent decoded metadata writes preserve the actual reused current control");
	current.Attributes.EasingIndices[2].reset();
	current_control.DirectionOffset = 0;
	following_control.DirectionOffset = 180;
	const auto tiny = sgre::scene::EvaluateMotionChildDirectionOffset(current, following, current_control, following_control, resource, 0x1p-25f);
	Require(tiny == 180.0f * 0x1p-25f, "native direction interpolation does not use the local pose epsilon snap");
	current_control.DirectionOffset = 360;
	following_control.DirectionOffset = 0;
	current.Attributes.EasingIndices[2] = 99;
	Require(sgre::scene::EvaluateMotionChildDirectionOffset(current, following, current_control, following_control, resource, 0.5f) == 0, "adjusted equal offsets bypass curve ownership lookup but still wrap after interpolation");
}

static void TestDisabledAndRetainedOffsets() {
	const auto resource = Resource();
	auto current = Frame(0);
	auto following = Frame(1);
	sgre::scene::MotionChildFrameControl current_control;
	current_control.DirectionMode = 1;
	current_control.DirectionOffset = -450;
	current_control.CompleteDirection = true;
	sgre::scene::MotionChildFrameControl following_control;
	following_control.DirectionMode = 1;
	following_control.DirectionOffset = -450;
	following.Time = 0;
	current.Attributes.EasingIndices[2] = 99;
	Require(sgre::scene::EvaluateMotionChildDirectionOffset(current, following, current_control, following_control, resource, 0) == -450, "equal current/following offsets bypass interpolation, wrapping, duration and curve use");
	following_control.DirectionOffset = 20;
	following.Hidden = true;
	Require(sgre::scene::EvaluateMotionChildDirectionOffset(current, following, current_control, following_control, resource, 0) == -450, "hidden following slot retains the current offset");
	following.Hidden = false;
	following_control.DirectionMode = 0;
	Require(sgre::scene::EvaluateMotionChildDirectionOffset(current, following, current_control, following_control, resource, 0) == -450, "disabled following direction retains the current offset");
	following_control.DirectionMode = 1;
	current.Tween = false;
	Require(sgre::scene::EvaluateMotionChildDirectionOffset(current, following, current_control, following_control, resource, 0) == -450, "static current slot retains its raw selected offset");
	current.Tween = true;
	current_control.CompleteDirection = false;
	Require(sgre::scene::EvaluateMotionChildDirectionOffset(current, following, current_control, following_control, resource, 0) == -450, "incomplete direction retains its raw selected offset");
	current_control.DirectionMode = 0;
	current_control.DirectionOffset = std::numeric_limits<float>::quiet_NaN();
	Require(!sgre::scene::EvaluateMotionChildDirectionOffset(current, following, current_control, following_control, resource, 0), "DT0 suppresses offset evaluation entirely");
}

static void TestRequiredOwnersAndBounds() {
	const auto resource = Resource();
	auto current = Frame(0);
	auto following = Frame(1);
	sgre::scene::MotionChildFrameControl current_control;
	current_control.DirectionMode = 1;
	current_control.CompleteDirection = true;
	sgre::scene::MotionChildFrameControl following_control;
	following_control.DirectionMode = 1;
	following_control.DirectionOffset = 90;
	current.Attributes.EasingIndices[2] = 1;
	RequireFailure([&] {
		static_cast<void>(sgre::scene::EvaluateMotionChildDirectionOffset(current, following, current_control, following_control, resource, 0.5f));
	},
	    "actual resource owner");
	current.Attributes.EasingIndices[2].reset();
	following.Time = 0;
	RequireFailure([&] {
		static_cast<void>(sgre::scene::EvaluateMotionChildDirectionOffset(current, following, current_control, following_control, resource, 0.5f));
	},
	    "positive-duration");
	following.Time = 1;
	current_control.DirectionMode = 5;
	RequireFailure([&] {
		static_cast<void>(sgre::scene::EvaluateMotionChildDirectionOffset(current, following, current_control, following_control, resource, 0.5f));
	},
	    "native family");
	current_control.DirectionMode = 1;
	current.Hidden = true;
	RequireFailure([&] {
		static_cast<void>(sgre::scene::EvaluateMotionChildDirectionOffset(current, following, current_control, following_control, resource, 0.5f));
	},
	    "selected-tree reset");
}

static std::uint32_t ReadWord(std::span<const std::uint8_t> bytes, std::size_t offset) {
	if (offset > bytes.size() || 4 > bytes.size() - offset) {
		throw std::runtime_error("truncated native child direction word");
	}

	std::uint32_t result = 0;
	for (std::size_t index = 0; index < 4; ++index) {
		result |= static_cast<std::uint32_t>(bytes[offset + index]) << (index * 8);
	}

	return result;
}

static float ReadNumber(std::span<const std::uint8_t> bytes, std::size_t offset) {
	return std::bit_cast<float>(ReadWord(bytes, offset));
}

template <std::size_t Size>
static std::array<std::uint8_t, Size> ReadRecord(std::ifstream& input) {
	std::array<char, Size> raw{};
	input.read(raw.data(), static_cast<std::streamsize>(raw.size()));
	Require(input.gcount() == static_cast<std::streamsize>(raw.size()), "truncated native child direction record");
	std::array<std::uint8_t, Size> result{};
	for (std::size_t index = 0; index < Size; ++index) {
		result[index] = static_cast<std::uint8_t>(static_cast<unsigned char>(raw[index]));
	}

	return result;
}

static void ReplayNative(const char* path) {
	std::ifstream input(path, std::ios::binary);
	Require(input.is_open(), "cannot open native child direction fixture");
	const auto header = ReadRecord<8>(input);
	Require(header[0] == 'M' && header[1] == 'C' && header[2] == 'D' && header[3] == 'O', "invalid native child direction signature");
	const auto count = ReadWord(header, 4);
	Require(count > 0 && count <= 512, "native child direction work exceeds its bound");
	const auto resource = Resource();
	for (std::uint32_t index = 0; index < count; ++index) {
		const auto record = ReadRecord<35>(input);
		Require(record[0] <= 4 && record[1] <= 4 && record[2] <= 1 && record[3] <= 1 && record[4] <= 1 && record[5] <= 1 && record[30] <= 1, "native child direction input/output flags exceed their domain");
		auto current = Frame(ReadNumber(record, 18));
		auto following = Frame(ReadNumber(record, 22));
		current.Tween = record[3] != 0;
		following.Hidden = record[4] != 0;
		current.Attributes.Interval = ReadWord(record, 26);
		if (record[5] != 0) {
			current.Attributes.EasingIndices[2] = 0;
		}

		sgre::scene::MotionChildFrameControl current_control;
		current_control.DirectionMode = record[0];
		current_control.CompleteDirection = record[2] != 0;
		current_control.DirectionOffset = ReadNumber(record, 10);
		sgre::scene::MotionChildFrameControl following_control;
		following_control.DirectionMode = record[1];
		following_control.DirectionOffset = ReadNumber(record, 14);
		const auto actual = sgre::scene::EvaluateMotionChildDirectionOffset(current, following, current_control, following_control, resource, ReadNumber(record, 6));
		Require(actual.has_value() == (record[30] != 0), "child selected direction suppression differs from actual native prefix");
		if (actual) {
			Require(std::bit_cast<std::uint32_t>(*actual) == ReadWord(record, 31), "child selected effective offset word differs from actual native prefix");
		}
	}

	Require(input.peek() == std::char_traits<char>::eof(), "native child direction fixture has trailing data");
}

int main(int argc, char** argv) {
	try {
		Require(argc == 1 || argc == 2, "usage: motion_child_direction_tests [native-direction-fixture]");
		TestSelectedSlotsAndEasing();
		TestDisabledAndRetainedOffsets();
		TestRequiredOwnersAndBounds();
		if (argc == 2) {
			ReplayNative(argv[1]);
		}

		std::cout << "motion child direction tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
