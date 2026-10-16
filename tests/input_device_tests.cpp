#include "sgre/input/hub.hpp"

#include <array>
#include <bit>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <class Operation>
static void RequireFailure(Operation operation, std::string_view context) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("input safety rejection was not reported");
}

struct NativeFrame {
	std::int32_t Step = 0;
	std::uint32_t Buttons = 0;
	std::array<float, 6> Axes{};
	bool Connected = false;
	std::array<std::uint8_t, 3> Physical{};
	std::uint32_t Query = 0;
	std::uint32_t Previous = 0;
	std::uint32_t Pressed = 0;
	std::uint32_t Released = 0;
	bool Combo = false;
	std::uint32_t Direction = 0;
	std::array<float, 2> Cursor{};
	std::uint32_t Remaining = 0;
	std::array<std::uint32_t, 3> PhysicalRemaining{};
};

static void TestNativeSamples() {
	// Original 0x1404f4200 and key query instructions supplied these retained synthetic vectors.
	{
		sgre::input::InputDevice device;
		device.SetButtonRepeat(0xffffffff, UINT64_C(1), UINT64_C(0));
		device.SetPhysicalRepeat(5, sgre::input::RepeatSettings{true, UINT64_C(3), UINT64_C(0)});
		device.SetPhysicalRepeat(127, sgre::input::RepeatSettings{true, UINT64_C(18446744073709551615), UINT64_C(7)});
		device.SetPhysicalRepeat(255, sgre::input::RepeatSettings{true, UINT64_C(18446744073709551615), UINT64_C(0)});
		const std::array<NativeFrame, 8> frames{{
		    {2, 0xf9736f9, {0.5f, 0.5f, -1.0f, 1.0f, 0.5f, 1.0f}, true, {128, 255, 255}, 0xf8555509, 0x0, 0x8151409, 0x0, false, 0xffffffff, {2.5f, 2.5f}, 0x0, {0x0, 0x40a00000, 0x0}},
		    {2, 0x12764944, {-1.0f, 0.0f, 1.0f, 0.0f, 0.25f, 1.0f}, true, {0, 1, 255}, 0xeca451e6, 0x0, 0x244144, 0x0, false, 0x6, {-2.5f, 2.5f}, 0x0, {0x40400000, 0x5f800000, 0x0}},
		    {1, 0xcc2a1fa, {1.0f, 1.0f, 1.0f, -1.0f, 0.0f, 0.25f}, false, {128, 1, 0}, 0xe32cc2f7, 0x0, 0x80f2, 0x0, false, 0xffffffff, {2.5f, 7.5f}, 0x0, {0x40000000, 0x5f800000, 0x5f800000}},
		    {1, 0x0, {1.0f, 0.25f, -1.0f, 0.5f, -1.0f, -1.0f}, false, {255, 128, 128}, 0x77068c32, 0xcc2a1fa, 0x0, 0x4028032, false, 0xffffffff, {7.5f, 8.75f}, 0x3f800000, {0x3f800000, 0x5f800000, 0x5f800000}},
		    {0, 0xcb3a40b, {0.0f, 0.25f, 0.25f, 0.5f, 0.25f, 0.25f}, true, {255, 1, 128}, 0xe55d928, 0x0, 0xc118008, 0x0, false, 0xffffffff, {7.5f, 10.0f}, 0x3f800000, {0x3f800000, 0x5f800000, 0x5f800000}},
		    {0, 0xa02d266, {0.0f, 0.0f, 0.5f, 0.0f, 0.25f, 0.0f}, false, {1, 255, 255}, 0xfa8fd0e3, 0xcb3a40b, 0x2005060, 0x810001, false, 0x5, {7.5f, 10.0f}, 0x3f800000, {0x40400000, 0x5f800000, 0x5f800000}},
		    {8, 0x74ce3c6, {0.0f, 0.5f, 0.25f, 0.5f, 0.5f, 0.5f}, false, {128, 1, 255}, 0x8e6d29be, 0x0, 0x64c2186, 0x0, false, 0xffffffff, {7.5f, 12.5f}, 0x0, {0x0, 0x5f800000, 0x5f800000}},
		    {3, 0x9f1be47, {1.0f, 0.25f, -1.0f, 0.0f, 0.0f, 1.0f}, false, {1, 128, 128}, 0xc07dbe96, 0x0, 0x71be06, 0x0, false, 0x6, {12.5f, 13.75f}, 0x0, {0x40400000, 0x5f800000, 0x5f800000}},
		}};
		const std::array<std::uint8_t, 3> physical_keys{5, 127, 255};
		for (const auto& frame : frames) {
			sgre::input::DeviceSample sample;
			sample.Buttons = frame.Buttons;
			sample.LeftStick = {frame.Axes[0], frame.Axes[1]};
			sample.RightStick = {frame.Axes[2], frame.Axes[3]};
			sample.LeftTrigger = frame.Axes[4];
			sample.RightTrigger = frame.Axes[5];
			sample.Connected = frame.Connected;
			for (std::size_t index = 0; index < physical_keys.size(); ++index) {
				sample.PhysicalKeys[physical_keys[index]] = frame.Physical[index];
			}

			device.Advance(sample, frame.Step);
			Require(device.Buttons() == frame.Buttons && device.PreviousButtons() == frame.Previous && device.Pressed(frame.Query) == frame.Pressed && device.Released(frame.Query) == frame.Released && device.ComboPressed(frame.Query) == frame.Combo, "logical transitions differ from native instructions");
			Require(std::bit_cast<std::uint32_t>(device.StickDirection()) == frame.Direction && device.Connected() == frame.Connected, "direction/connectivity differs from native state");
			Require(device.Cursor().X == frame.Cursor[0] && device.Cursor().Y == frame.Cursor[1] && std::bit_cast<std::uint32_t>(device.ButtonRepeatRemaining()) == frame.Remaining, "cursor/repeat state differs from native instructions");
			for (std::size_t index = 0; index < physical_keys.size(); ++index) {
				Require(std::bit_cast<std::uint32_t>(device.PhysicalRepeatRemaining(physical_keys[index])) == frame.PhysicalRemaining[index], "physical key repeat differs from native instructions");
			}
		}
	}
	{
		sgre::input::InputDevice device;
		device.SetButtonRepeat(0x1, UINT64_C(25), UINT64_C(2));
		device.SetPhysicalRepeat(5, sgre::input::RepeatSettings{true, UINT64_C(18446744073709551615), UINT64_C(0)});
		device.SetPhysicalRepeat(127, sgre::input::RepeatSettings{true, UINT64_C(0), UINT64_C(7)});
		device.SetPhysicalRepeat(255, sgre::input::RepeatSettings{true, UINT64_C(0), UINT64_C(7)});
		const std::array<NativeFrame, 8> frames{{
		    {3, 0x44f12e3, {-1.0f, 0.5f, -1.0f, -1.0f, 0.25f, 0.0f}, false, {1, 1, 128}, 0xac30a526, 0x0, 0x4000022, 0x0, false, 0xffffffff, {-5.0f, 2.5f}, 0x41b00000, {0x5f800000, 0x0, 0x40800000}},
		    {1, 0x13d9d41f, {0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 1.0f}, false, {128, 128, 1}, 0x3cc54add, 0x44f12e3, 0x1080401c, 0x40402c0, false, 0x0, {-5.0f, 2.5f}, 0x41a80000, {0x5f800000, 0x40c00000, 0x0}},
		    {2, 0x8027f8a, {1.0f, 0.0f, 0.25f, -1.0f, 1.0f, -1.0f}, false, {128, 0, 128}, 0xb76c9ff9, 0x13d9d41f, 0xb80, 0x13488011, false, 0x2, {0.0f, 2.5f}, 0x41c80000, {0x5f800000, 0x0, 0x40a00000}},
		    {1, 0x4b4be40, {-1.0f, 0.0f, 0.25f, 1.0f, 1.0f, 0.5f}, false, {0, 0, 1}, 0xa6f3f75f, 0x8027f8a, 0x4b08040, 0x2410a, false, 0x6, {-5.0f, 2.5f}, 0x41c80000, {0x5f800000, 0x0, 0x0}},
		    {2, 0x13154644, {-1.0f, -1.0f, -1.0f, 0.25f, -1.0f, 1.0f}, false, {255, 128, 255}, 0x404c4eda, 0x4b4be40, 0x4000, 0x800, false, 0x6, {-10.0f, -2.5f}, 0x41c80000, {0x5f800000, 0x40a00000, 0x40a00000}},
		    {2, 0x0, {1.0f, 0.0f, 0.25f, 0.0f, 1.0f, 0.5f}, true, {128, 255, 128}, 0xbff115f8, 0x13154644, 0x0, 0x13110440, false, 0xffffffff, {-5.0f, -2.5f}, 0x41c80000, {0x5f800000, 0x40400000, 0x40400000}},
		    {0, 0x0, {0.0f, 0.25f, 0.0f, 1.0f, 0.0f, -1.0f}, false, {0, 1, 255}, 0x9f791b0d, 0x0, 0x0, 0x0, false, 0xffffffff, {-5.0f, -1.25f}, 0x41c80000, {0x5f800000, 0x0, 0x40400000}},
		    {1, 0x159ac7b1, {0.25f, -1.0f, 0.0f, 0.25f, 0.5f, 0.25f}, false, {1, 0, 1}, 0x797505e9, 0x0, 0x111005a1, 0x0, false, 0xffffffff, {-3.75f, -6.25f}, 0x41c00000, {0x5f800000, 0x0, 0x0}},
		}};
		const std::array<std::uint8_t, 3> physical_keys{5, 127, 255};
		for (const auto& frame : frames) {
			sgre::input::DeviceSample sample;
			sample.Buttons = frame.Buttons;
			sample.LeftStick = {frame.Axes[0], frame.Axes[1]};
			sample.RightStick = {frame.Axes[2], frame.Axes[3]};
			sample.LeftTrigger = frame.Axes[4];
			sample.RightTrigger = frame.Axes[5];
			sample.Connected = frame.Connected;
			for (std::size_t index = 0; index < physical_keys.size(); ++index) {
				sample.PhysicalKeys[physical_keys[index]] = frame.Physical[index];
			}

			device.Advance(sample, frame.Step);
			Require(device.Buttons() == frame.Buttons && device.PreviousButtons() == frame.Previous && device.Pressed(frame.Query) == frame.Pressed && device.Released(frame.Query) == frame.Released && device.ComboPressed(frame.Query) == frame.Combo, "logical transitions differ from native instructions");
			Require(std::bit_cast<std::uint32_t>(device.StickDirection()) == frame.Direction && device.Connected() == frame.Connected, "direction/connectivity differs from native state");
			Require(device.Cursor().X == frame.Cursor[0] && device.Cursor().Y == frame.Cursor[1] && std::bit_cast<std::uint32_t>(device.ButtonRepeatRemaining()) == frame.Remaining, "cursor/repeat state differs from native instructions");
			for (std::size_t index = 0; index < physical_keys.size(); ++index) {
				Require(std::bit_cast<std::uint32_t>(device.PhysicalRepeatRemaining(physical_keys[index])) == frame.PhysicalRemaining[index], "physical key repeat differs from native instructions");
			}
		}
	}
	{
		sgre::input::InputDevice device;
		device.SetButtonRepeat(0x1, UINT64_C(25), UINT64_C(8));
		device.SetPhysicalRepeat(5, sgre::input::RepeatSettings{true, UINT64_C(0), UINT64_C(7)});
		device.SetPhysicalRepeat(127, sgre::input::RepeatSettings{true, UINT64_C(0), UINT64_C(18446744073709551615)});
		device.SetPhysicalRepeat(255, sgre::input::RepeatSettings{true, UINT64_C(18446744073709551615), UINT64_C(0)});
		const std::array<NativeFrame, 8> frames{{
		    {0, 0x0, {0.0f, -1.0f, 1.0f, 0.0f, 1.0f, 1.0f}, true, {1, 128, 0}, 0xee38f338, 0x0, 0x0, 0x0, false, 0xffffffff, {0.0f, -5.0f}, 0x41c80000, {0x0, 0x5f800000, 0x5f800000}},
		    {8, 0x115a8086, {0.0f, 0.5f, 0.0f, 1.0f, 0.0f, 1.0f}, false, {0, 0, 1}, 0xac27fd67, 0x0, 0x28006, 0x0, false, 0x2, {0.0f, -2.5f}, 0x41c80000, {0x0, 0x0, 0x5f800000}},
		    {2, 0x0, {0.25f, 0.0f, 0.5f, -1.0f, 0.25f, 1.0f}, true, {128, 128, 255}, 0x8ff0a593, 0x115a8086, 0x0, 0x1508082, false, 0xffffffff, {1.25f, -2.5f}, 0x41c80000, {0x40a00000, 0x5f800000, 0x5f800000}},
		    {3, 0x0, {0.5f, -1.0f, 0.0f, 0.5f, 0.5f, 1.0f}, false, {0, 0, 255}, 0xc9eb8811, 0x0, 0x0, 0x0, false, 0xffffffff, {3.75f, -7.5f}, 0x41c80000, {0x0, 0x0, 0x5f800000}},
		    {3, 0x1a369dc4, {-1.0f, 0.25f, 0.5f, -1.0f, 0.0f, 0.0f}, false, {0, 1, 0}, 0x647a427e, 0x0, 0x320044, 0x0, false, 0xffffffff, {-1.25f, -6.25f}, 0x41c80000, {0x0, 0x0, 0x5f800000}},
		    {3, 0x3d8072e, {1.0f, 0.5f, 0.5f, 0.0f, 1.0f, 1.0f}, true, {128, 1, 1}, 0xc9c66bb2, 0x1a369dc4, 0x1c00222, 0x8060880, false, 0x4, {3.75f, -3.75f}, 0x41c80000, {0x40800000, 0x0, 0x5f800000}},
		    {2, 0xd3f9e70, {-1.0f, 1.0f, 0.25f, 0.5f, -1.0f, 0.5f}, true, {255, 0, 255}, 0x9afcc19b, 0x3d8072e, 0x8248010, 0x2c0010a, false, 0xffffffff, {-1.25f, 1.25f}, 0x41c80000, {0x40000000, 0x0, 0x5f800000}},
		    {2, 0x0, {0.5f, 0.5f, 0.5f, 0.5f, 0.25f, -1.0f}, true, {1, 1, 255}, 0x95f69ef6, 0xd3f9e70, 0x0, 0x5369e70, false, 0xffffffff, {1.25f, 3.75f}, 0x41c80000, {0x0, 0x0, 0x5f800000}},
		}};
		const std::array<std::uint8_t, 3> physical_keys{5, 127, 255};
		for (const auto& frame : frames) {
			sgre::input::DeviceSample sample;
			sample.Buttons = frame.Buttons;
			sample.LeftStick = {frame.Axes[0], frame.Axes[1]};
			sample.RightStick = {frame.Axes[2], frame.Axes[3]};
			sample.LeftTrigger = frame.Axes[4];
			sample.RightTrigger = frame.Axes[5];
			sample.Connected = frame.Connected;
			for (std::size_t index = 0; index < physical_keys.size(); ++index) {
				sample.PhysicalKeys[physical_keys[index]] = frame.Physical[index];
			}

			device.Advance(sample, frame.Step);
			Require(device.Buttons() == frame.Buttons && device.PreviousButtons() == frame.Previous && device.Pressed(frame.Query) == frame.Pressed && device.Released(frame.Query) == frame.Released && device.ComboPressed(frame.Query) == frame.Combo, "logical transitions differ from native instructions");
			Require(std::bit_cast<std::uint32_t>(device.StickDirection()) == frame.Direction && device.Connected() == frame.Connected, "direction/connectivity differs from native state");
			Require(device.Cursor().X == frame.Cursor[0] && device.Cursor().Y == frame.Cursor[1] && std::bit_cast<std::uint32_t>(device.ButtonRepeatRemaining()) == frame.Remaining, "cursor/repeat state differs from native instructions");
			for (std::size_t index = 0; index < physical_keys.size(); ++index) {
				Require(std::bit_cast<std::uint32_t>(device.PhysicalRepeatRemaining(physical_keys[index])) == frame.PhysicalRemaining[index], "physical key repeat differs from native instructions");
			}
		}
	}
}

static void TestIndependentEdgesAndBounds() {
	sgre::input::InputDevice device;
	sgre::input::DeviceSample sample;
	sample.Buttons = 1;
	sample.Connected = true;
	device.Advance(sample, 1);
	Require(device.Pressed(3) == 1 && !device.ComboPressed(3), "a partial combination was accepted");
	sample.Buttons = 3;
	device.Advance(sample, 1);
	Require(device.Pressed(3) == 2 && device.ComboPressed(3), "a newly completed combination was lost");
	sample.Buttons = 0;
	device.Advance(sample, 1);
	Require(device.Released(3) == 3 && !device.ComboPressed(0), "release or zero-mask combination semantics differ");
	sample.LeftStick = {1, -1};
	const sgre::input::InputExtent extent{3, 7};
	device.Advance(sample, 1, extent);
	Require(device.Cursor().X == 1.5f && device.Cursor().Y == -3.5f, "centered cursor extent was not applied");
	RequireFailure([&] {
		device.Advance(sample, -1);
	},
	    "nonnegative");
	sample.LeftStick.X = std::numeric_limits<float>::infinity();
	RequireFailure([&] {
		device.Advance(sample, 1);
	},
	    "nonfinite");
	Require(device.Cursor().X == 1.5f && device.Cursor().Y == -3.5f, "invalid sample changed previously published state");

	sgre::input::InputHub hub;
	Require(hub.DeviceCount() == 8 && &hub.DeviceAt(999) == &hub.DeviceAt(7), "native input count/upper clamping differs");
	RequireFailure([&] {
		static_cast<void>(hub.DeviceAt(-1));
	},
	    "negative");
	std::array<sgre::input::DeviceSample, sgre::input::kInputDeviceCount> samples{};
	samples[0].Buttons = 1;
	samples[1].Buttons = 1;
	hub.Advance(samples, 1);
	Require(hub.DeviceAt(0).Pressed(1) == 0 && hub.DeviceAt(1).Pressed(1) == 1, "native first-device priming changed other devices");
	hub.Advance(samples, 1);
	Require(hub.DeviceAt(1).Pressed(1) == 0, "a held button produced an extra edge");
}

int main() {
	try {
		TestNativeSamples();
		TestIndependentEdgesAndBounds();
		std::cout << "native input samples, key repeats, transitions, cursor bounds, and hub priming passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
