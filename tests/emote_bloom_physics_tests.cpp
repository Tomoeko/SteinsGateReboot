#include "sgre/scene/emote_bloom_physics.hpp"

#include "emote_bloom_native_vectors.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

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
	throw std::runtime_error("missing expected Bloom owner rejection");
}

static sgre::scene::EmoteBloomDefinition Definition(const NativeBloomCase& input) {
	const auto& fields = input.Scalars;
	sgre::scene::EmoteBloomDefinition definition;
	definition.Substeps = input.Substeps;
	definition.DefaultRotation = fields[1];
	definition.Speed = fields[2];
	definition.Mass = fields[3];
	definition.LimitLengthRatio = fields[4];
	definition.SizeHorizontal = fields[5];
	definition.SizeVertical = fields[6];
	definition.Origin = {fields[7], fields[8]};
	definition.Center = {fields[9], fields[10]};
	definition.Elastic = fields[11];
	definition.Friction = fields[12];
	definition.RotateWeight = fields[13];
	definition.TwistWeight = fields[14];
	definition.TrapeziumWeight = fields[15];
	definition.PitchWeight = fields[16];
	definition.ControlPointSize = fields[17];
	definition.CoordinateScale = {fields[18], fields[19], fields[20]};
	definition.CollisionRepulsion = fields[21];
	definition.CollisionHardness = 0;
	definition.Straightness = fields[23];
	for (std::size_t index = 0; index != input.PointCount; ++index) {
		definition.Tresses.push_back({input.Tresses[index][0], input.Tresses[index][1]});
	}
	return definition;
}

static sgre::scene::EmotePhysicalPosture Posture(const std::array<float, 6>& input) {
	sgre::scene::EmotePhysicalPosture result;
	result.Coordinate = {input[0], input[1], input[2]};
	result.Rotation = input[3];
	result.Cosine = input[4];
	result.Sine = input[5];
	return result;
}

struct NativeComparison {
	std::size_t Compared = 0;
	std::size_t Exact = 0;
	float MaximumScaledError = 0;

	void Word(float actual, std::uint32_t expected_word, const std::string& context) {
		const float expected = std::bit_cast<float>(expected_word);
		Require(std::isfinite(actual) && std::isfinite(expected), context + " must preserve finite native state");
		const float scaled = std::abs(actual - expected) / std::max(1.0f, std::abs(expected));
		// Selected before any C++ run; native float CRT trigonometry is not assumed cross-CRT bit-identical.
		Require(scaled <= 8.0e-6f, context + " exceeds preselected native bound: actual=" + std::to_string(actual) + " expected=" + std::to_string(expected));
		MaximumScaledError = std::max(MaximumScaledError, scaled);
		Exact += std::bit_cast<std::uint32_t>(actual) == expected_word;
		++Compared;
	}
};

static void TestOriginalCompleteConstructorsAndSteps() {
	NativeComparison comparison;
	for (std::size_t case_index = 0; case_index != kNativeBloomCases.size(); ++case_index) {
		const auto& input = kNativeBloomCases[case_index];
		sgre::scene::EmoteBloomPhysics owner(Definition(input));
		Require(owner.Points().size() == input.PointCount, "actual Bloom owner must retain every serialized physical control");
		for (std::size_t index = 0; index != input.PointCount; ++index) {
			const auto& point = owner.Points()[index];
			const auto& native = input.Initial[index];
			const std::array<float, 7> relative{point.Relative.X, point.Relative.Y, point.Direction.X, point.Direction.Y, point.Rest[0], point.Rest[1], point.Rest[2]};
			const auto context = "Bloom constructor case " + std::to_string(case_index) + " point " + std::to_string(index);
			for (std::size_t word = 0; word != relative.size(); ++word) {
				comparison.Word(relative[word], native.Relative[word], context + " relative " + std::to_string(word));
			}
			comparison.Word(point.Limit, native.Limit, context + " radius limit");
			for (std::size_t word = 0; word != point.Influences.size(); ++word) {
				comparison.Word(point.Influences[word], native.Influences[word], context + " grid influence " + std::to_string(word));
			}
		}
		owner.SetPosture(Posture(input.InitialPose));
		owner.Initialize();
		for (std::size_t step_index = 0; step_index != input.Steps.size(); ++step_index) {
			const auto& step = input.Steps[step_index];
			owner.SetPosture(Posture(step.Pose));
			owner.ClearForces();
			for (std::size_t point = 0; point != input.PointCount; ++point) {
				owner.AddForce(point, step.Forces[point]);
			}
			owner.Update(step.Elapsed);
			for (std::size_t point_index = 0; point_index != input.PointCount; ++point_index) {
				const auto& point = owner.Points()[point_index];
				const std::array<std::array<float, 3>, 6> groups{point.Target, point.Previous, point.Predicted, point.Coordinate, point.Velocity, point.Force};
				for (std::size_t group = 0; group != groups.size(); ++group) {
					for (std::size_t axis = 0; axis != 3; ++axis) {
						const auto word = group * 3 + axis;
						comparison.Word(groups[group][axis], step.Points[point_index][word], "Bloom case " + std::to_string(case_index) + " step " + std::to_string(step_index) + " point " + std::to_string(point_index) + " word " + std::to_string(word));
					}
				}
			}
		}
	}
	std::cout << "native Bloom words " << comparison.Compared << " bit-exact " << comparison.Exact << " max scaled error " << comparison.MaximumScaledError << '\n';
}

static void TestRealOwnedForcesBoundsAndRetirement() {
	auto definition = Definition(kNativeBloomCases[0]);
	sgre::scene::EmoteBloomPhysics owner(definition);
	RequireFailure([&] {
		owner.Update(1);
	},
	    "actual initialization");
	owner.Initialize();
	const auto initial = owner.Points()[0].Coordinate;
	owner.ClearForces();
	owner.AddForce(0, {0.25f, 0.5f, 0.125f});
	owner.AddForce(0, {0.25f, 0.5f, 0.125f});
	Require(owner.Points()[0].Force == std::array<float, 3>{0.5f, 1.0f, 0.25f}, "real emission contributions accumulate into the actual owned point");
	owner.Update(1);
	Require(owner.Points()[0].Coordinate != initial && owner.Points()[0].Velocity[2] != 0, "real Bloom solver must integrate all three force axes without a replacement pendulum model");
	owner.ClearForces();
	Require(owner.Points()[0].Force == std::array<float, 3>{}, "actual clear force phase must retire prior emission accumulation");
	owner.Initialize();
	Require(owner.Points()[0].Coordinate == initial && owner.Points()[0].Velocity == std::array<float, 3>{}, "native reset restores actual target pose and velocity without replacing the owner");
	RequireFailure([&] {
		owner.AddForce(1, {1, 0, 0});
	},
	    "owned controls");
	RequireFailure([&] {
		owner.AddForce(0, {std::numeric_limits<float>::infinity(), 0, 0});
	},
	    "finite physical domain");
	RequireFailure([&] {
		owner.Update(0);
	},
	    "positive elapsed");
	RequireFailure([&] {
		owner.Update(6145);
	},
	    "positive elapsed");
	std::exception_ptr foreign_failure;
	std::thread foreign([&] {
		try {
			owner.ClearForces();
		} catch (...) {
			foreign_failure = std::current_exception();
		}
	});
	foreign.join();
	Require(static_cast<bool>(foreign_failure), "physical point mutation requires the actual owner thread");
	definition.Mass = 0;
	RequireFailure([&] {
		sgre::scene::EmoteBloomPhysics rejected(definition);
	},
	    "positive physical");
	definition = Definition(kNativeBloomCases[0]);
	definition.Tresses[0] = definition.Center;
	RequireFailure([&] {
		sgre::scene::EmoteBloomPhysics rejected(definition);
	},
	    "distinct from its actual center");
}

static void TestZeroSpeedConstructorAndFiniteStepBoundary() {
	auto definition = Definition(kNativeBloomCases[0]);
	definition.Speed = 0;
	sgre::scene::EmoteBloomPhysics owner(definition);
	Require(owner.Definition().Speed == 0 && owner.Points().size() == definition.Tresses.size(), "native3025a0 copies zero speed and constructs its actual point/influence owner");
	for (std::size_t index = 0; index != owner.Points().front().Influences.size(); ++index) {
		const auto expected = std::bit_cast<float>(kNativeBloomCases[0].Initial[0].Influences[index]);
		Require(owner.Points().front().Influences[index] == expected, "zero speed must not change native constructor influence weights");
	}
	owner.Initialize();
	const auto initial = owner.Points().front().Coordinate;
	Require(std::all_of(initial.begin(), initial.end(), [](float value) {
		return std::isfinite(value);
	}) && owner.Points().front().Velocity == std::array<float, 3>{},
	    "actual zero-speed initialization is finite without executing a solver step");
	RequireFailure([&] {
		owner.Update(1);
	},
	    "positive elapsed");
	Require(owner.Points().front().Coordinate == initial && owner.Points().front().Velocity == std::array<float, 3>{}, "active zero-scaled-time native solver is nonfinite and remains an explicit pre-mutation supported-domain boundary");
}

int main() {
	try {
		TestOriginalCompleteConstructorsAndSteps();
		TestRealOwnedForcesBoundsAndRetirement();
		TestZeroSpeedConstructorAndFiniteStepBoundary();
		std::cout << "Bloom actual constructor influence/point-spring/reset ownership domain PASS\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
