#include "sgre/scene/emote_blink_control.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static void TestIndependentPcg() {
	sgre::scene::EmoteBlinkRandom random(0x12345678);
	// Independently calculated integer recurrence from native 2c2b30/2d5940; not a captured pose.
	const std::array<std::uint32_t, 6> expected{1396232816, 2574534932u, 1827820963, 420176249, 3425969301u, 3861661550u};
	for (const auto value : expected) {
		Require(random.Next() == value, "native eye PCG seed/advance/rotate differs");
	}
}

static void TestActualBlinkPhases() {
	sgre::scene::EmoteBlinkRandom random(42);
	sgre::scene::EmoteBlinkParameters parameters;
	parameters.IntervalMinimum = 2;
	parameters.IntervalMaximum = 2;
	parameters.CloseTicks = 2;
	parameters.HoldTicks = 1;
	parameters.OpenTicks = 2;
	sgre::scene::EmoteBlinkControl blink(random, parameters);
	Require(!blink.Active() && blink.Amount() == 0 && blink.NextInterval() == 2, "native blink initial interval/idle state differs");
	blink.Step(1);
	Require(!blink.Active() && blink.Elapsed() == 1, "idle blink interval did not accumulate actual ticks");
	blink.Step(1);
	Require(blink.Active() && blink.Elapsed() == 0 && blink.Amount() == 0 && blink.NextInterval() == 5, "entering blink must discard interval overshoot and wait before closing");
	blink.Step(1);
	Require(blink.Amount() == 0.5f, "native blink close ramp differs");
	blink.ResetIdleClock();
	Require(blink.Elapsed() == 1, "eye mutation must not reset an active blink");
	blink.Step(1);
	Require(blink.Amount() == 1, "native blink fully closed boundary differs");
	blink.SetSuppressed(true);
	blink.Step(1);
	Require(blink.Amount() == 1 && blink.Active(), "suppression must not cancel a live blink hold");
	blink.Step(1);
	Require(blink.Amount() == 0.5f, "native blink open ramp differs");
	blink.Step(1);
	Require(blink.Amount() == 0 && !blink.Active() && blink.Elapsed() == 0 && blink.NextInterval() == 2, "blink completion must resample only the next actual interval");
	blink.Step(10);
	Require(!blink.Active() && blink.Elapsed() == 0, "suppression must preserve idle interval without synthetic advancement");
	blink.SetSuppressed(false);
	blink.Step(20);
	Require(blink.Active() && blink.Elapsed() == 0 && blink.Amount() == 0, "large delta must follow native single-transition behavior, not invented catch-up blinks");
}

static void TestParameterAndFrameDomains() {
	sgre::scene::EmoteBlinkRandom random(99);
	sgre::scene::EmoteBlinkParameters parameters;
	parameters.IntervalMinimum = 2;
	parameters.IntervalMaximum = 2;
	parameters.CloseTicks = 1;
	sgre::scene::EmoteBlinkControl blink(random, parameters);
	blink.Step(1);
	parameters.IntervalMinimum = 3;
	parameters.IntervalMaximum = 3;
	blink.SetParameters(parameters, true);
	Require(blink.Elapsed() == 0 && blink.NextInterval() == 2, "native parameter update resets idle clock without drawing a new interval");
	Require(sgre::scene::ApplyEmoteBlinkFrame(2, 0, 10, 0.5f, 1) == 6, "native selected blink parameter modulation differs");
	Require(sgre::scene::ApplyEmoteBlinkFrame(2, 0, 10, 0.5f, 0) == 2, "native zero parameter index must not modulate frame");
	Require(sgre::scene::ApplyEmoteBlinkFrame(12, 0, 10, 0.5f, 1) == 12, "blink must preserve out-of-range authored frame");
	Require(sgre::scene::ApplyEmoteBlinkFrame(8, 10, 0, 0.5f, 1) == 4, "reversed native blink frame range differs");
}

int main() {
	try {
		TestIndependentPcg();
		TestActualBlinkPhases();
		TestParameterAndFrameDomains();
		std::cout << "Emote independent PCG, native blink interval/close/hold/open/suppression, single-transition timing and selected-frame modulation passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
