#include "sgre/scene/motion_particle_emission.hpp"

#include "motion_particle_emission_vectors.hpp"

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

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view fragment) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(fragment) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("unsupported particle emission domain was accepted");
}

static void TestOriginalCadence() {
	for (std::size_t case_index = 0; case_index < kNativeParticleEmissionCases.size(); ++case_index) {
		const auto& native = kNativeParticleEmissionCases[case_index];
		sgre::scene::MotionPlayerRandom random(std::bit_cast<float>(native.ConstructorSample));
		Require(random.State() == native.InitialRandom, "actual constructor sample must seed the native parent stream");
		sgre::scene::MotionParticleEmissionState state;
		for (std::size_t phase = 0; phase < native.Phases.size(); ++phase) {
			try {
				const auto& expected = native.Phases[phase];
				sgre::scene::MotionParticleEmissionInput input;
				input.Active = expected.Active;
				input.FrameChanged = expected.Changed;
				input.Trigger = expected.Trigger;
				input.FrequencyMin = std::bit_cast<float>(expected.MinimumBits);
				input.FrequencyMax = std::bit_cast<float>(expected.MaximumBits);
				input.DeltaTicks = std::bit_cast<float>(expected.DeltaBits);
				Require(state.Advance(input, random) == expected.Attempts, "native original emission attempt count differs");
				Require(state.Ready() == expected.Ready, "native original retained readiness differs");
				Require(std::bit_cast<std::uint32_t>(state.TimerTicks()) == expected.TimerBits, "native original retained timer word differs");
				Require(random.State() == expected.Random, "native original random consumption or word state differs");
			} catch (const std::exception& error) {
				throw std::runtime_error("native particle emission case " + std::to_string(case_index) + " phase " + std::to_string(phase) + ": " + error.what());
			}
		}
	}
}

static void TestRetainedTimerAndPhysicalTrigger() {
	sgre::scene::MotionParticleEmissionState state;
	sgre::scene::MotionPlayerRandom random(0);
	sgre::scene::MotionParticleEmissionInput input;
	input.Active = true;
	input.FrequencyMin = 10;
	input.FrequencyMax = 10;
	Require(state.Advance(input, random) == 0 && state.Ready() && state.TimerTicks() == 6, "automatic activation initializes its actual reciprocal period");
	const auto initial_random = random.State();
	input.Active = false;
	Require(state.Advance(input, random) == 0 && !state.Ready() && state.TimerTicks() == 6, "inactive layers clear readiness while preserving their native timer");
	input.Active = true;
	input.FrequencyMin = 0;
	Require(state.Advance(input, random) == 0 && !state.Ready() && state.TimerTicks() == 6, "zero automatic minimum skips both activation and countdown");
	input.Trigger = 1;
	input.FrameChanged = false;
	Require(state.Advance(input, random) == 0 && state.Ready(), "current physical trigger activates independently of evaluated frequency");
	input.FrameChanged = true;
	input.FrequencyMin = -2.75f;
	input.FrequencyMax = -2.75f;
	Require(state.Advance(input, random) == 0, "native negative burst truncation creates no positive attempts");
	Require(random.State() == initial_random, "equal reciprocal or burst endpoints never consume a parent random word");
	input.Trigger = 2;
	input.FrequencyMin = 500;
	input.FrequencyMax = 600;
	Require(state.Advance(input, random) == 0 && random.State() == initial_random && state.TimerTicks() == 6, "other physical trigger values preserve readiness and timer without emitting");
}

static void TestBoundedFailureIsAtomic() {
	sgre::scene::MotionParticleEmissionState state;
	sgre::scene::MotionPlayerRandom random(0);
	const auto initial_random = random.State();
	sgre::scene::MotionParticleEmissionInput input;
	input.Active = true;
	input.FrequencyMin = 1000000;
	input.FrequencyMax = 2000000;
	input.DeltaTicks = 100;
	RequireFailure([&] {
		static_cast<void>(state.Advance(input, random));
	},
	    "bounded update budget");
	Require(!state.Ready() && state.TimerTicks() == 0 && random.State() == initial_random, "bounded automatic rejection must not partly publish its timer or parent stream");
	input.Trigger = 1;
	input.FrameChanged = true;
	RequireFailure([&] {
		static_cast<void>(state.Advance(input, random));
	},
	    "bounded update budget");
	Require(!state.Ready() && random.State() == initial_random, "bounded burst rejection must retain its actual pre-update owner state");
	input.Trigger = 0;
	input.FrequencyMin = 1;
	input.FrequencyMax = 0;
	RequireFailure([&] {
		static_cast<void>(state.Advance(input, random));
	},
	    "positive evaluated frequencies");
	input.FrequencyMax = std::numeric_limits<float>::infinity();
	RequireFailure([&] {
		static_cast<void>(state.Advance(input, random));
	},
	    "finite actual clock");
	input.FrequencyMax = 1;
	input.DeltaTicks = std::numeric_limits<float>::quiet_NaN();
	RequireFailure([&] {
		static_cast<void>(state.Advance(input, random));
	},
	    "finite actual clock");
}

int main() {
	try {
		TestOriginalCadence();
		TestRetainedTimerAndPhysicalTrigger();
		TestBoundedFailureIsAtomic();
		std::cout << "native particle cadence: 24 cases, 192 phases, timer/attempt/random words exact\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
