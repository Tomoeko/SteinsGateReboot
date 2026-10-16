#include "sgre/scene/captured_opacity_animation.hpp"

#include "captured_animation_native_vectors.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view expected) {
	try {
		operation();
	} catch (const std::exception& error) {
		Require(std::string_view(error.what()).find(expected) != std::string_view::npos, error.what());

		return;
	}
	throw std::runtime_error("missing Captured opacity rejection: " + std::string(expected));
}

static void TestOriginalAnimationPhases() {
	std::size_t comparisons = 0;
	for (const auto& sample : kCapturedAnimationNativeCases) {
		sgre::scene::CapturedOpacityAnimation animation(sample.Initial, sample.Target, sample.Duration, sample.Curve);
		float opacity = sample.Initial;
		for (const auto& phase : sample.Phases) {
			if (const auto value = animation.Advance(phase.Delta)) {
				opacity = std::max(std::min(*value, 255.0f), 0.0f);
			}
			const auto state = animation.State();
			Require(std::bit_cast<std::uint32_t>(opacity) == phase.OpacityWord && std::bit_cast<std::uint32_t>(state.Sampled) == phase.SampledWord, "native Captured sampled/clamped float words differ");
			Require(state.Elapsed == phase.Elapsed && state.Ordinal.value_or(std::numeric_limits<std::uint32_t>::max()) == static_cast<std::uint32_t>(phase.Ordinal) && state.Complete == phase.Complete, "native Captured elapsed/ordinal/completion differ");
			++comparisons;
		}
	}
	Require(comparisons == 768, "every original Captured animation phase must compare");
}

static void TestActualCommonFirstOwnerAndReplacement() {
	auto source = std::make_shared<sgre::scene::CaptureTask>(4, 4);
	sgre::scene::CapturedTask task(source);
	sgre::core::FrameClock clock(0);
	sgre::scene::ParallelTaskSet common_first(8);
	float before = -1;
	float after = -1;
	sgre::scene::ParallelTaskCallbacks before_callbacks;
	before_callbacks.Update = [&](auto) {
		before = task.State().Opacity;
	};
	static_cast<void>(common_first.Append(std::move(before_callbacks)));
	sgre::scene::CapturedOpacityAnimations owner(task, clock, common_first);
	owner.Append(-128, 7, 0);
	Require(task.State().Opacity == 255 && common_first.Count() == 2, "actual animateOpacity registers a deferred handler without fabricating an immediate sample");
	sgre::scene::ParallelTaskCallbacks after_callbacks;
	after_callbacks.Update = [&](auto) {
		after = task.State().Opacity;
	};
	static_cast<void>(common_first.Append(std::move(after_callbacks)));
	const auto clock_step = clock.VariableStep();
	common_first.Update();
	Require(before == 255 && after == std::bit_cast<float>(kCapturedAnimationNativeCases[35].Phases[0].OpacityWord), "real CommonFirst list order must expose the native first sampled value before camera-child drawing");
	Require(clock.VariableStep() == clock_step && clock.ResidualTicks() == 0, "opacity handler reads the actual shared clock without advancing it");
	const auto initial = task.State().Opacity;
	owner.Append(0, 0, 0);
	Require(task.State().Opacity == initial, "replacement captures actual retained opacity but remains deferred");
	common_first.Update();
	Require(task.State().Opacity == 0 && common_first.Count() == 2, "same-type replacement retires predecessor and zero-duration handler after genuine update");
	clock.SetVariableFrame(true);
	static_cast<void>(clock.Advance(50));
	owner.Append(255, 1, 0);
	common_first.Update();
	Require(task.State().Opacity == 255 && common_first.Count() == 2, "actual variable-clock phase consumes shared delta and retires at its native terminal sample");
	RequireFailure([&] {
		owner.Append(0, -1, 0);
	},
	    "duration/curve");
	RequireFailure([&] {
		owner.Append(0, 1, 65);
	},
	    "duration/curve");
	common_first.Clear();
	task.Close();
	source->Close();
}

int main() {
	try {
		TestOriginalAnimationPhases();
		TestActualCommonFirstOwnerAndReplacement();
		std::cout << "PASS Captured opacity original64cases/768phases and genuine CommonFirst/shared-clock/replacement owner\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
