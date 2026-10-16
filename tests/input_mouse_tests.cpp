#include "sgre/input/mouse.hpp"

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

struct NativeMouseFrame {
	std::int32_t Width = 0;
	std::int32_t Height = 0;
	std::int32_t X = 0;
	std::int32_t Y = 0;
	std::uint32_t Buttons = 0;
	std::int32_t Wheel = 0;
	bool Match = false;
	bool Enter = false;
	std::array<std::uint32_t, 2> Position{};
	std::array<std::int32_t, 2> Movement{};
	std::array<std::uint32_t, 3> ButtonState{};
	std::array<bool, 3> Flags{};
	std::uint32_t WheelResult = 0;
};

static void TestNativeMouseSamples() {
	// Original mouse sample and query instructions produced these retained sequential vectors.
	{
		sgre::input::MouseInput mouse;
		const std::array<NativeMouseFrame, 8> frames{{
		    {16384, 1080, 2560, 955, 0x43, 120, true, true, {0xbf300000, 0x3f44bc6a}, {0, 0}, {0x43, 0x0, 0x0}, {true, true, false}, 0x1},
		    {3, 16384, 6, 13931, 0x74, -1, true, false, {0x47000000, 0x47000000}, {39643, 25083}, {0x74, 0x34, 0x3}, {false, false, true}, 0xffffffff},
		    {1279, 7, 37, -3, 0x65, 0, true, false, {0x47000000, 0x47000000}, {0, 0}, {0x65, 0x0, 0x0}, {false, false, false}, 0x0},
		    {16384, 16384, 5344, 15555, 0xb9, 0, false, false, {0x47000000, 0x47000000}, {0, 0}, {0xb9, 0x0, 0x0}, {false, false, false}, 0x0},
		    {16384, 1080, 9602, 182, 0x8d, 120, true, true, {0x3e303afb, 0xbf29b3d0}, {0, 0}, {0x8d, 0x0, 0x0}, {true, true, false}, 0x1},
		    {64, 16384, 9, 3256, 0xc3, 0, true, true, {0xbf37fcb9, 0xbf1a3d71}, {-8908, 604}, {0xc3, 0x42, 0xc}, {true, false, false}, 0x0},
		    {1279, 32, 1221, 13, 0xbd, 1, true, true, {0x3f68c7e3, 0xbe400000}, {16280, 4150}, {0xbd, 0x3c, 0x42}, {true, false, false}, 0x1},
		    {16384, 719, 14110, 368, 0xa5, 0, true, true, {0x3f38ef35, 0x3cc154ca}, {-1869, 2111}, {0xa5, 0x0, 0x18}, {true, false, false}, 0x0},
		}};
		for (const auto& frame : frames) {
			sgre::input::MouseSample sample;
			sample.Client.Right = frame.Width;
			sample.Client.Bottom = frame.Height;
			sample.ClientX = frame.X;
			sample.ClientY = frame.Y;
			sample.Buttons = frame.Buttons;
			sample.WheelTicks = frame.Wheel;
			sample.TargetWindowMatches = frame.Match;
			sample.EnterTracking = frame.Enter;
			mouse.Advance(sample);
			const auto position = mouse.Position();
			const auto movement = mouse.Movement();
			Require(std::bit_cast<std::uint32_t>(position.X) == frame.Position[0] && std::bit_cast<std::uint32_t>(position.Y) == frame.Position[1], "native mouse position quantization differs");
			Require(movement.X == static_cast<float>(frame.Movement[0]) && movement.Y == static_cast<float>(frame.Movement[1]), "native mouse movement units differ");
			Require(mouse.Buttons() == frame.ButtonState[0] && mouse.Pressed() == frame.ButtonState[1] && mouse.Released() == frame.ButtonState[2], "native mouse button transitions differ");
			Require(mouse.Inside() == frame.Flags[0] && mouse.Entered() == frame.Flags[1] && mouse.Left() == frame.Flags[2] && std::bit_cast<std::uint32_t>(mouse.WheelDirection()) == frame.WheelResult, "native enter tracking or wheel sign differs");
		}
	}
	{
		sgre::input::MouseInput mouse;
		const std::array<NativeMouseFrame, 8> frames{{
		    {64, 719, 48, 622, 0x6, -1, true, true, {0x3f000000, 0x3f3ae7d5}, {0, 0}, {0x6, 0x0, 0x0}, {true, true, false}, 0xffffffff},
		    {64, 720, 43, 722, 0x25, -1, true, true, {0x47000000, 0x47000000}, {27768, 25467}, {0x25, 0x21, 0x2}, {true, false, false}, 0xffffffff},
		    {1280, 32, 1140, -2, 0x67, 120, true, false, {0x47000000, 0x47000000}, {0, 0}, {0x67, 0x0, 0x0}, {false, false, true}, 0x1},
		    {1279, 16384, 1197, 10335, 0x0, 120, true, true, {0x3f5f27bb, 0x3e85e354}, {0, 0}, {0x0, 0x0, 0x0}, {true, true, false}, 0x1},
		    {64, 719, 52, 93, 0x2b, -120, false, true, {0x47000000, 0x47000000}, {24051, 30153}, {0x2b, 0x2b, 0x0}, {true, false, false}, 0xffffffff},
		    {64, 32, 38, -3, 0x58, -120, false, true, {0x47000000, 0x47000000}, {0, 0}, {0x58, 0x0, 0x0}, {true, false, false}, 0xffffffff},
		    {1279, 719, 68, 274, 0x17, -120, true, true, {0xbf64c2f8, 0xbe7381d8}, {0, 0}, {0x17, 0x0, 0x0}, {true, false, false}, 0xffffffff},
		    {1920, 16384, 1864, 9354, 0x6d, -120, true, true, {0x3f710cb3, 0x3e113405}, {18352, 3796}, {0x6d, 0x68, 0x12}, {true, false, false}, 0xffffffff},
		}};
		for (const auto& frame : frames) {
			sgre::input::MouseSample sample;
			sample.Client.Right = frame.Width;
			sample.Client.Bottom = frame.Height;
			sample.ClientX = frame.X;
			sample.ClientY = frame.Y;
			sample.Buttons = frame.Buttons;
			sample.WheelTicks = frame.Wheel;
			sample.TargetWindowMatches = frame.Match;
			sample.EnterTracking = frame.Enter;
			mouse.Advance(sample);
			const auto position = mouse.Position();
			const auto movement = mouse.Movement();
			Require(std::bit_cast<std::uint32_t>(position.X) == frame.Position[0] && std::bit_cast<std::uint32_t>(position.Y) == frame.Position[1], "native mouse position quantization differs");
			Require(movement.X == static_cast<float>(frame.Movement[0]) && movement.Y == static_cast<float>(frame.Movement[1]), "native mouse movement units differ");
			Require(mouse.Buttons() == frame.ButtonState[0] && mouse.Pressed() == frame.ButtonState[1] && mouse.Released() == frame.ButtonState[2], "native mouse button transitions differ");
			Require(mouse.Inside() == frame.Flags[0] && mouse.Entered() == frame.Flags[1] && mouse.Left() == frame.Flags[2] && std::bit_cast<std::uint32_t>(mouse.WheelDirection()) == frame.WheelResult, "native enter tracking or wheel sign differs");
		}
	}
}

static void TestIndependentSamplingAndRequests() {
	sgre::input::MouseInput mouse;
	sgre::input::MouseSample sample;
	sample.Client = {0, 0, 100, 80};
	sample.ClientX = 50;
	sample.ClientY = 40;
	sample.Buttons = 1;
	sample.TargetWindowMatches = true;
	sample.EnterTracking = true;
	mouse.Advance(sample);
	Require(mouse.Position().X == 0 && mouse.Position().Y == 0 && mouse.Pressed() == 0 && mouse.Entered(), "initial mouse priming changed entry tracking");
	sample.ClientX = 75;
	sample.Buttons = 3;
	mouse.Advance(sample);
	Require(mouse.Position().X == 0.5f && mouse.Movement().X == 5000 && mouse.Pressed() == 2, "normalized position and integer movement were conflated");
	sample.TargetWindowMatches = false;
	mouse.Advance(sample);
	Require(mouse.Position().X == 32768 && mouse.Inside(), "position validity was conflated with independent enter tracking");
	sample.TargetWindowMatches = true;
	sample.Buttons = 7;
	mouse.Advance(sample);
	Require(mouse.Movement().X == 0 && mouse.Pressed() == 0, "reentry did not prime the native previous state");
	const auto request = sgre::input::PlanMousePosition(sample.Client, {1, -1});
	Require(request.X == 100 && request.Y == 0 && mouse.Position().X == 0.5f, "cursor planning mutated sampled state or clamped the native request");
	// Original 0x1404f4670 retains separate float operations before integer conversion.
	struct CursorFixture {
		std::int32_t Width;
		std::int32_t Height;
		std::array<std::uint32_t, 2> Point;
		std::array<std::uint32_t, 2> Expected;
	};
	const std::array<CursorFixture, 3> cursor_fixtures{{
	    {3, 719, {0xc09ad44f, 0xbf58c6f9}, {0xfffffffb, 0x37}},
	    {1280, 720, {0xbf991162, 0x40c9328e}, {0xffffff83, 0xa3f}},
	    {3, 1080, {0x3efe0e0f, 0xc0d0b411}, {0x2, 0xfffff45b}},
	}};
	for (const auto& fixture : cursor_fixtures) {
		const sgre::input::ClientRectangle client{0, 0, fixture.Width, fixture.Height};
		const auto native_request = sgre::input::PlanMousePosition(client, {std::bit_cast<float>(fixture.Point[0]), std::bit_cast<float>(fixture.Point[1])});
		Require(std::bit_cast<std::uint32_t>(native_request.X) == fixture.Expected[0] && std::bit_cast<std::uint32_t>(native_request.Y) == fixture.Expected[1], "cursor position request differs from original instructions");
	}

	try {
		static_cast<void>(sgre::input::PlanMousePosition(sample.Client, {std::numeric_limits<float>::infinity(), 0}));
		throw std::logic_error("nonfinite cursor request was accepted");
	} catch (const std::runtime_error&) {
	}
}

int main() {
	try {
		TestNativeMouseSamples();
		TestIndependentSamplingAndRequests();
		std::cout << "native mouse position, movement, buttons, enter tracking, and bounded cursor planning passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
