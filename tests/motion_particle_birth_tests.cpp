#include "sgre/scene/motion_particle_birth.hpp"

#include "motion_particle_birth_native_vectors.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
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
static void RequireFailure(Operation operation, std::string_view expected) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(expected) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("particle birth accepted an unsupported numeric policy");
}

static sgre::scene::MotionLinearTransform Transform(const std::array<std::uint32_t, 4>& words) {
	sgre::scene::MotionLinearTransform transform;
	transform.XX = std::bit_cast<float>(words[0]);
	transform.XY = std::bit_cast<float>(words[1]);
	transform.YX = std::bit_cast<float>(words[2]);
	transform.YY = std::bit_cast<float>(words[3]);

	return transform;
}

static sgre::scene::MotionParticleNumericState Numerics(const std::array<std::uint32_t, 9>& words) {
	sgre::scene::MotionParticleNumericState numeric;
	numeric.FrequencyMin = std::bit_cast<float>(words[0]);
	numeric.FrequencyMax = std::bit_cast<float>(words[1]);
	numeric.VelocityMin = std::bit_cast<float>(words[2]);
	numeric.VelocityMax = std::bit_cast<float>(words[3]);
	numeric.AngleMin = std::bit_cast<float>(words[4]);
	numeric.AngleMax = std::bit_cast<float>(words[5]);
	numeric.ZoomMin = std::bit_cast<float>(words[6]);
	numeric.ZoomMax = std::bit_cast<float>(words[7]);
	numeric.Range = std::bit_cast<float>(words[8]);

	return numeric;
}

static float CompareWord(float actual, std::uint32_t word, std::size_t index, std::string_view field) {
	const auto expected = std::bit_cast<float>(word);
	const auto error = std::abs(actual - expected) / std::max(1.0f, std::abs(expected));
	if (!std::isfinite(actual) || error > 8.0e-6f) {
		throw std::runtime_error("particle birth native case " + std::to_string(index) + " " + std::string(field) + " differs: expected " + std::to_string(expected) + ", actual " + std::to_string(actual));
	}

	return error;
}

static void TestNativeBirths() {
	float maximum_error = 0;
	for (std::size_t index = 0; index < kNativeMotionParticleBirthCases.size(); ++index) {
		const auto& expected = kNativeMotionParticleBirthCases[index];
		sgre::scene::MotionParticleDefinition definition;
		definition.Particle = expected.Particle;
		definition.FlyDirection = expected.FlyDirection;
		definition.ApplyZoomToVelocity = expected.ApplyZoomToVelocity;
		definition.AccelerationRatio = std::bit_cast<float>(expected.Acceleration);
		sgre::scene::MotionParticleBirthSource source;
		source.Transform = Transform(expected.Matrix);
		source.PlayerRootTransform = Transform(expected.RootMatrix);
		for (std::size_t axis = 0; axis < source.Coordinate.size(); ++axis) {
			source.Coordinate[axis] = std::bit_cast<float>(expected.Coordinate[axis]);
		}

		source.Origin.X = std::bit_cast<float>(expected.Origin[0]);
		source.Origin.Y = std::bit_cast<float>(expected.Origin[1]);
		source.CoordinatePlane = expected.Plane;
		source.Opacity = expected.Opacity;
		source.FlipX = expected.Flips[0];
		source.FlipY = expected.Flips[1];
		sgre::scene::MotionPlayerRandom random(std::bit_cast<float>(expected.ParentConstructorSample));
		Require(random.State() == expected.InitialRandom, "birth fixture must start from the genuine parent constructor sample");
		Require(std::bit_cast<std::uint32_t>(random.NextUnitFloat()) == expected.Samples[0], "the real target-list pick precedes birth even when it has one element");
		const auto state = sgre::scene::EvaluateMotionParticleBirth(definition, Numerics(expected.Numeric), source, random);
		Require(random.State() == expected.ParentRandom, "birth must consume exactly the original containing player's random stream");
		Require(state.FlipX == source.FlipX && state.FlipY == source.FlipY && state.Opacity == expected.ExpectedOpacity && std::bit_cast<std::uint32_t>(state.Attenuation) == expected.Acceleration, "birth must retain actual source flags, inherited opacity and attenuation");
		for (std::size_t axis = 0; axis < state.Coordinate.size(); ++axis) {
			maximum_error = std::max(maximum_error, CompareWord(state.Coordinate[axis], expected.ExpectedCoordinate[axis], index, "coordinate"));
			maximum_error = std::max(maximum_error, CompareWord(state.Velocity[axis], expected.ExpectedVelocity[axis], index, "velocity"));
		}

		maximum_error = std::max(maximum_error, CompareWord(state.AngleDegrees, expected.ExpectedAngle, index, "angle"));
		maximum_error = std::max(maximum_error, CompareWord(state.Zoom, expected.ExpectedZoom, index, "zoom"));
		sgre::scene::MotionPlayerRandom sample_replay(std::bit_cast<float>(expected.ParentConstructorSample));
		for (std::size_t sample = 0; sample < expected.SampleCount; ++sample) {
			Require(std::bit_cast<std::uint32_t>(sample_replay.NextUnitFloat()) == expected.Samples[sample], "retained native birth samples differ from the actual player generator");
		}

		Require(sample_replay.State() == random.State(), "birth must stop at the measured native sample count");
	}

	std::cout << "32 native particle births maximum scaled error " << maximum_error << '\n';
}

static void TestSourcePoliciesAndEqualEndpoints() {
	sgre::scene::MotionParticleDefinition definition;
	definition.InheritOpacity = 0;
	definition.AccelerationRatio = 0.75f;
	sgre::scene::MotionParticleNumericState numeric;
	numeric.VelocityMin = numeric.VelocityMax = 3;
	numeric.AngleMin = numeric.AngleMax = -30;
	numeric.ZoomMin = numeric.ZoomMax = 2;
	sgre::scene::MotionParticleBirthSource source;
	source.Coordinate = {10, 20, 30};
	source.Origin = {2, 3};
	source.PlayerRootTransform.XX = 4;
	source.PlayerRootTransform.YY = 9;
	source.Opacity = 127;
	source.FlipY = true;
	sgre::scene::MotionPlayerRandom random(0.25f);
	const auto initial = random.State();
	const auto state = sgre::scene::EvaluateMotionParticleBirth(definition, numeric, source, random);
	Require(random.State() == initial, "point mode and equal numeric endpoints consume no birth samples");
	Require(state.Coordinate == std::array<float, 3>{8, 17, 30} && state.Velocity == std::array<float, 3>{3, 0, 0} && state.AngleDegrees == 30 && state.Zoom == 12 && state.Opacity == 255 && state.Attenuation == 0.75f && !state.FlipX && state.FlipY, "point mode preserves raw origin, flip parity and containing root determinant compensation");

	source.CoordinatePlane = 1;
	source.IndependentLayerInherit = true;
	source.FlipX = true;
	const auto independent = sgre::scene::EvaluateMotionParticleBirth(definition, numeric, source, random);
	Require(independent.Coordinate == std::array<float, 3>{8, 20, 27} && independent.Zoom == 2 && independent.AngleDegrees == 330 && random.State() == initial, "XZ plane and actual independent-inherit byte preserve separate coordinate and zoom policies");

	numeric.AngleMin = 0.0f;
	numeric.AngleMax = -0.0f;
	numeric.VelocityMin = 0.0f;
	numeric.VelocityMax = -0.0f;
	numeric.ZoomMin = numeric.ZoomMax = 1;
	const auto zero = sgre::scene::EvaluateMotionParticleBirth(definition, numeric, source, random);
	Require(random.State() == initial && zero.AngleDegrees == 0, "ordered equal signed-zero endpoints do not draw a random sample");
}

static void TestUnsupportedInputs() {
	sgre::scene::MotionParticleDefinition definition;
	sgre::scene::MotionParticleNumericState numeric;
	numeric.ZoomMin = numeric.ZoomMax = 1;
	sgre::scene::MotionParticleBirthSource source;
	sgre::scene::MotionPlayerRandom random(0.5f);
	const auto initial = random.State();
	definition.TriangularVolume = true;
	RequireFailure([&] {
		(void)sgre::scene::EvaluateMotionParticleBirth(definition, numeric, source, random);
	},
	    "planar");
	definition.TriangularVolume = false;
	definition.InheritAngle = true;
	RequireFailure([&] {
		(void)sgre::scene::EvaluateMotionParticleBirth(definition, numeric, source, random);
	},
	    "inheritance");
	definition.InheritAngle = false;
	definition.InheritVelocity = 1;
	RequireFailure([&] {
		(void)sgre::scene::EvaluateMotionParticleBirth(definition, numeric, source, random);
	},
	    "inheritance");
	definition.InheritVelocity = 0;
	definition.FlyDirection = 2;
	RequireFailure([&] {
		(void)sgre::scene::EvaluateMotionParticleBirth(definition, numeric, source, random);
	},
	    "lifetime");
	definition.FlyDirection = 0;
	source.CoordinatePlane = 2;
	RequireFailure([&] {
		(void)sgre::scene::EvaluateMotionParticleBirth(definition, numeric, source, random);
	},
	    "planar");
	source.CoordinatePlane = 0;
	source.Coordinate[0] = std::numeric_limits<float>::infinity();
	RequireFailure([&] {
		(void)sgre::scene::EvaluateMotionParticleBirth(definition, numeric, source, random);
	},
	    "finite actual");
	source.Coordinate[0] = 0;
	numeric.AngleMin = 30000000;
	RequireFailure([&] {
		(void)sgre::scene::EvaluateMotionParticleBirth(definition, numeric, source, random);
	},
	    "bounded native");
	Require(random.State() == initial, "unsupported birth producers must fail before consuming actual parent random values");

	numeric.AngleMin = 0;
	numeric.VelocityMin = numeric.VelocityMax = 1;
	numeric.ZoomMin = numeric.ZoomMax = 0;
	definition.ApplyZoomToVelocity = 2;
	RequireFailure([&] {
		(void)sgre::scene::EvaluateMotionParticleBirth(definition, numeric, source, random);
	},
	    "finite recovered");
}

int main() {
	try {
		TestNativeBirths();
		TestSourcePoliciesAndEqualEndpoints();
		TestUnsupportedInputs();
		std::cout << "motion particle birth tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
