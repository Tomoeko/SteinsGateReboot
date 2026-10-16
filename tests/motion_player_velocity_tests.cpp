#include "sgre/scene/motion_root_scene.hpp"

#include "motion_player_velocity_vectors.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
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

static void Compare(const std::array<float, 3>& actual, const std::array<std::uint32_t, 3>& expected, bool exact, std::string_view field) {
	for (std::size_t axis = 0; axis < actual.size(); ++axis) {
		const auto wanted = std::bit_cast<float>(expected[axis]);
		if (exact) {
			Require(std::bit_cast<std::uint32_t>(actual[axis]) == expected[axis], std::string(field) + " differs from original word");
		} else {
			const auto scale = std::max({1.0f, std::abs(actual[axis]), std::abs(wanted)});
			Require(std::isfinite(actual[axis]) && std::abs(actual[axis] - wanted) <= 8e-6f * scale, std::string(field) + " differs from original attenuation arithmetic");
		}
	}
}

static void TestOriginalCompleteComposition() {
	for (std::size_t case_index = 0; case_index < kNativeMotionVelocityCases.size(); ++case_index) {
		const auto& native = kNativeMotionVelocityCases[case_index];
		sgre::scene::MotionRootScene root;
		root.SetCoordinate3(native.Coordinate);
		root.SetVelocity(native.Velocity);
		root.SetVelocityAttenuation(native.Attenuation);
		for (std::size_t phase_index = 0; phase_index < native.Phases.size(); ++phase_index) {
			try {
				const auto& phase = native.Phases[phase_index];
				root.AdvanceVelocity(std::bit_cast<float>(phase.DeltaBits));
				root.Compose(phase.Assigned);
				const bool exact = native.Attenuation == 1;
				Compare(root.Overrides().Coordinate, phase.Coordinate, exact, "coordinate");
				Compare(root.Velocity(), phase.Velocity, exact, "velocity");
				Compare(root.Frame().Hierarchy.Attributes.Coordinate, phase.Composed, exact, "published coordinate");
				Compare(root.Frame().Displacement, phase.Displacement, exact, "published displacement");
			} catch (const std::exception& error) {
				throw std::runtime_error("native velocity case " + std::to_string(case_index) + " phase " + std::to_string(phase_index) + ": " + error.what());
			}
		}
	}
}

template <typename Operation>
static void RequireFailure(Operation operation) {
	try {
		operation();
	} catch (const std::runtime_error&) {
		return;
	}

	throw std::runtime_error("unsupported player velocity domain was accepted");
}

static void TestActualPhaseOrderingAndFailures() {
	sgre::scene::MotionRootScene root;
	root.Compose(false);
	root.SetVelocity({1, 0, 0});
	root.AdvanceVelocity(0);
	Require(root.Overrides().Dirty && root.Overrides().Coordinate[0] == 0, "native nonzero velocity dirties the root even when actual elapsed delta is zero");
	root.Compose(false);
	root.SetVelocityAttenuation(0.25f);
	root.AdvanceVelocity(60);
	Require(root.Overrides().Coordinate[0] == 60 && root.Velocity()[0] == 0.25f, "coordinates must advance with prior velocity before per-second attenuation");
	root.Compose(true);
	Require(root.Frame().Hierarchy.Attributes.Coordinate[0] == 60 && root.Frame().Displacement == std::array<float, 3>{}, "assigned tick composition suppresses displacement while preserving actual velocity advancement");
	const auto coordinates = root.Overrides().Coordinate;
	const auto velocity = root.Velocity();
	RequireFailure([&] {
		root.SetVelocity({std::numeric_limits<float>::quiet_NaN(), 0, 0});
	});
	RequireFailure([&] {
		root.SetVelocityAttenuation(-1);
	});
	RequireFailure([&] {
		root.AdvanceVelocity(std::numeric_limits<float>::infinity());
	});
	Require(root.Overrides().Coordinate == coordinates && root.Velocity() == velocity, "rejected receiving inputs must retain prior completed/pending owner state");
	root.SetVelocityAttenuation(0);
	RequireFailure([&] {
		root.AdvanceVelocity(-1);
	});
	Require(root.Overrides().Coordinate == coordinates && root.Velocity() == velocity, "nonfinite power result cannot partly publish coordinates or velocity");
}

int main() {
	try {
		TestOriginalCompleteComposition();
		TestActualPhaseOrderingAndFailures();
		std::cout << "native velocity: 24 cases, 96 complete root phases\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
