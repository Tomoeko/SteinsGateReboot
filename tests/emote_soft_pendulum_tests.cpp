#include "sgre/scene/emote_soft_pendulum.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

static void RequireNear(double actual, double expected, double tolerance, std::string_view operation) {
	if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance) {
		throw std::runtime_error(std::string(operation) + ": expected " + std::to_string(expected) + ", actual " + std::to_string(actual));
	}
}

static void Require(bool condition, std::string_view operation) {
	if (!condition) {
		throw std::runtime_error(std::string(operation));
	}
}

template <class Operation>
static void RequireThrows(Operation operation, std::string_view message) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		if (std::string_view(error.what()).find(message) != std::string_view::npos) {
			return;
		}
		throw;
	}
	throw std::runtime_error("expected actual physical-domain rejection");
}

static sgre::scene::EmoteSoftPendulumDefinition UnitDefinition() {
	sgre::scene::EmoteSoftPendulumDefinition definition;
	definition.Acceleration = {0, 1, 0};
	return definition;
}

static void TestInitializedOwnedParticlesAndOutput() {
	auto definition = UnitDefinition();
	definition.Acceleration = {0, 2, 0};
	definition.Length = 6;
	definition.ParticleCount = 4;
	definition.Mass = 6;
	definition.FrictionEnd = 3;
	definition.TrailingLength = 4;
	definition.TrailingPoints = 2;
	sgre::scene::EmoteSoftPendulum model(definition);
	RequireThrows([&] {
		model.Initialize();
	},
	    "preceding posture");
	sgre::scene::EmoteSoftPendulumPosture posture;
	posture.Coordinate = {2, 4, 6};
	model.SetPosture(posture);
	model.Initialize();
	Require(model.Particles().size() == 4 && model.TrailingPositions().size() == 2, "actual model owns all particle and trailing storage");
	for (std::size_t index = 0; index != model.Particles().size(); ++index) {
		const auto& particle = model.Particles()[index];
		Require(particle.Fixed == (index == 0), "only native one-end anchor is fixed");
		RequireNear(particle.Position[0], 2, 0, "straight initialization X");
		RequireNear(particle.Position[1], 4 + 2 * static_cast<double>(index), 0, "normalized acceleration places actual segment");
		RequireNear(particle.Position[2], 6, 0, "straight initialization Z");
		RequireNear(particle.Mass, index == 0 ? 0 : 2, 0, "total mass belongs only to free particles");
		RequireNear(particle.InverseMass, index == 0 ? 0 : 0.5, 0, "actual inverse free mass");
		RequireNear(particle.Friction, static_cast<double>(index), 0, "native linear friction profile");
	}
	RequireNear(model.Particles()[0].Predicted[1], 0, 0, "constructor anchor predicted state is not fabricated by initialization");
	RequireNear(model.TrailingPositions()[0][1], 12, 0, "first actual trailing point");
	RequireNear(model.TrailingPositions()[1][1], 14, 0, "last actual trailing point");
	std::array<std::array<float, 2>, 6> points;
	model.WritePoints(points);
	std::array<float, 5> lengths;
	model.WriteSegmentLengths(lengths);
	for (std::size_t index = 0; index != points.size(); ++index) {
		RequireNear(points[index][1], 4 + 2 * static_cast<double>(index), 0, "actual output order includes trailing points");
	}
	for (const auto length : lengths) {
		RequireNear(length, 2, 0, "rest and trailing segment output");
	}
	RequireThrows([&] {
		static_cast<void>(model.Bounds());
	},
	    "preceding force-reset");
	model.ClearForcesAndMeasureBounds();
	RequireNear(model.Bounds().Left, 2, 0, "physical bounds include fixed anchor");
	RequireNear(model.Bounds().Bottom, 10, 0, "physical bounds exclude nonphysical trailing points");
	posture.Coordinate[1] = 8;
	model.SetPosture(posture);
	RequireNear(model.Particles()[0].Position[1], 8, 0, "posture moves genuine fixed anchor");
	RequireNear(model.Particles()[1].Position[1], 6, 0, "posture does not reset free physical state");
	RequireNear(model.TrailingPositions()[1][1], 18, 0, "posture recomputes actual rest extension");
}

static void TestCoordinateScaleAndNativeOffsetOutput() {
	auto definition = UnitDefinition();
	definition.CoordinateScale = {2, 3, 4};
	definition.TrailingLength = 2;
	definition.TrailingPoints = 1;
	sgre::scene::EmoteSoftPendulum model(definition);
	sgre::scene::EmoteSoftPendulumPosture posture;
	posture.Coordinate = {1, 2, 3};
	model.SetPosture(posture);
	model.Initialize();
	RequireNear(model.Particles()[0].Position[0], 2, 0, "scaled anchor X");
	RequireNear(model.Particles()[0].Position[1], 6, 0, "scaled anchor Y");
	RequireNear(model.Particles()[0].Position[2], 12, 0, "scaled anchor Z");
	std::array<std::array<float, 2>, 3> points;
	model.WritePoints(points);
	RequireNear(points[0][0], 1, 0, "native physical output subtracts actual anisotropic offset");
	RequireNear(points[0][1], 2, 0, "native physical output retains original anchor");
	RequireNear(points[1][1], 3, 0, "free point uses same actual offset");
	RequireNear(points[2][0], 2, 0, "native trailing output does not subtract that offset");
	RequireNear(points[2][1], 9, 0, "native trailing output remains in actual extended space");
}

static void TestTiltedMassAndForcePhase() {
	auto definition = UnitDefinition();
	definition.ParticleCount = 3;
	definition.Mass = 12;
	definition.MassTiltedDistribution = true;
	definition.FrictionEnd = 4;
	sgre::scene::EmoteSoftPendulum model(definition);
	const auto weight = std::sqrt(8.0);
	RequireNear(model.Particles()[1].Mass, 12 * weight / (weight + 8), 1e-14, "tilted free mass from actual friction");
	RequireNear(model.Particles()[2].Mass, 96 / (weight + 8), 1e-14, "remaining tilted mass");
	sgre::scene::EmoteSoftPendulumPosture posture;
	model.SetPosture(posture);
	model.Initialize();
	model.AddForce(1, {2, 3, 4});
	model.AddForce(1, {1, -1, 2});
	RequireNear(model.Particles()[1].Force[0], 3, 0, "actual force accumulation X");
	RequireNear(model.Particles()[1].Force[2], 6, 0, "actual force accumulation Z");
	model.ClearForcesAndMeasureBounds();
	RequireNear(model.Particles()[1].Force[0], 0, 0, "native force reset phase clears free state");
	RequireNear(model.Particles()[1].Force[2], 0, 0, "native force reset phase precedes force collection");
}

static void TestAnalyticOneConstraintStepAndDamping() {
	auto definition = UnitDefinition();
	definition.FrictionBegin = 2;
	definition.FrictionEnd = 2;
	sgre::scene::EmoteSoftPendulum model(definition);
	sgre::scene::EmoteSoftPendulumPosture posture;
	model.SetPosture(posture);
	model.Initialize();
	model.ClearForcesAndMeasureBounds();
	model.AddForce(1, {0, 3, 0});
	model.Step(0.5f);
	// One free unit mass predicts Y=2. The single XPBD constraint has closed-form extension alpha/(1+alpha).
	const double alpha = std::bit_cast<double>(std::uint64_t{0x3dc5fd7fe1796495}) / 0.25;
	const auto expected_position = 1 + alpha / (1 + alpha);
	RequireNear(model.Particles()[1].Position[1], expected_position, 1e-14, "analytic native compliant length constraint");
	RequireNear(model.Particles()[1].Previous[1], 1, 0, "step retains preceding actual physical position");
	RequireNear(model.Particles()[1].Velocity[1], 0, 0, "native dt*friction damping follows position update");
	RequireNear(model.Particles()[0].Position[1], 0, 0, "distance solver keeps fixed anchor");
	RequireNear(model.Bounds().Bottom, 1, 0, "bounds remain actual preceding reset snapshot");
	RequireNear(model.Particles()[1].Force[1], 3, 0, "step does not invent a force-reset phase");
}

static void TestCompressionSpringAndUnclampedDamping() {
	auto definition = UnitDefinition();
	definition.Acceleration = {1, 0, 0};
	definition.Elastic = 1;
	definition.FrictionEnd = 3;
	sgre::scene::EmoteSoftPendulum model(definition);
	sgre::scene::EmoteSoftPendulumPosture posture;
	model.SetPosture(posture);
	model.Initialize();
	posture.Coordinate[0] = 0.5f;
	model.SetPosture(posture);
	model.ClearForcesAndMeasureBounds();
	model.Step(0.5f);
	// Initial distance .5, gravity adds .5 velocity, spring adds .25; predicted X=1.375 remains under length1.
	RequireNear(model.Particles()[1].Position[0], 1.375, 0, "actual compressed-pair elastic impulse");
	RequireNear(model.Particles()[1].Velocity[0], -0.375, 0, "damping is native arithmetic rather than clamped interpolation");
	RequireNear(model.Particles()[0].Position[0], 0.5, 0, "moved fixed anchor remains fixed during compression");
}

static void TestRotationAndSafetyBoundaries() {
	auto definition = UnitDefinition();
	definition.RotationInheritance = 1;
	sgre::scene::EmoteSoftPendulum model(definition);
	sgre::scene::EmoteSoftPendulumPosture posture;
	posture.Rotation = 1.57079637050628662109375f;
	model.SetPosture(posture);
	model.Initialize();
	RequireNear(model.Particles()[1].Position[0], -std::sin(static_cast<double>(posture.Rotation)), 0, "actual inherited acceleration rotation X");
	RequireNear(model.Particles()[1].Position[1], std::cos(static_cast<double>(posture.Rotation)), 0, "actual inherited acceleration rotation Y");
	RequireThrows([&] {
		model.Step(0);
	},
	    "positive finite seconds");
	RequireThrows([&] {
		model.Step(std::numeric_limits<float>::infinity());
	},
	    "positive finite seconds");
	RequireThrows([&] {
		model.AddForce(2, {});
	},
	    "actual initialized particle");
	std::array<std::array<float, 2>, 1> short_output;
	RequireThrows([&] {
		model.WritePoints(short_output);
	},
	    "exact initialized physical point count");
	definition.Acceleration = {};
	RequireThrows([&] {
		sgre::scene::EmoteSoftPendulum invalid(definition);
	},
	    "initialization direction");
	definition.Acceleration = {1, 0, 0};
	definition.ParticleCount = 1;
	RequireThrows([&] {
		sgre::scene::EmoteSoftPendulum invalid(definition);
	},
	    "bounded particles");
	definition.ParticleCount = 2;
	definition.Elastic = 1;
	sgre::scene::EmoteSoftPendulum coincident(definition);
	posture = {};
	coincident.SetPosture(posture);
	coincident.Initialize();
	posture.Coordinate[0] = 1;
	coincident.SetPosture(posture);
	RequireThrows([&] {
		coincident.Step(0.5f);
	},
	    "zero separation");
	RequireThrows([&] {
		coincident.Initialize();
	},
	    "invalid after a failed");
}

static std::array<double, 19> NativeParticleWords(const sgre::scene::EmoteSoftPendulumParticle& particle) {
	return {particle.Mass, particle.InverseMass, particle.Previous[0], particle.Previous[1], particle.Previous[2], particle.Predicted[0], particle.Predicted[1], particle.Predicted[2], particle.Position[0], particle.Position[1], particle.Position[2], particle.Velocity[0], particle.Velocity[1], particle.Velocity[2], particle.Force[0], particle.Force[1], particle.Force[2], particle.Friction, particle.AccelerationProjection};
}

static std::uint32_t ReadNativeWord(std::istream& file) {
	std::array<char, 4> bytes{};
	file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
	Require(static_cast<bool>(file), "native adhoc vector is truncated");

	return static_cast<std::uint8_t>(bytes[0]) | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[1])) << 8) | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[2])) << 16) | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[3])) << 24);
}

static float ReadNativeReal(std::istream& file) {
	return std::bit_cast<float>(ReadNativeWord(file));
}

static std::uint64_t ReadNativeDoubleWord(std::istream& file) {
	const auto lower = ReadNativeWord(file);
	const auto upper = ReadNativeWord(file);

	return (static_cast<std::uint64_t>(upper) << 32) | lower;
}

static void TestAllNativeAdhocVectors(const char* path) {
	std::ifstream file(path, std::ios::binary);
	Require(file.is_open() && ReadNativeWord(file) == 0x31415045 && ReadNativeWord(file) == 8, "native adhoc vector requires its exact EPA1 eight-case header");
	std::size_t particle_states = 0;
	std::size_t scalar_words = 0;
	for (std::size_t case_index = 0; case_index < 8; ++case_index) {
		sgre::scene::EmoteSoftPendulumDefinition definition;
		definition.AdhocSimulation = true;
		definition.ParticleCount = ReadNativeWord(file);
		Require(definition.ParticleCount >= 2 && definition.ParticleCount <= 8, "native adhoc vector has an unexpected particle count");
		for (auto& axis : definition.Acceleration) {
			axis = ReadNativeReal(file);
		}
		definition.Mass = ReadNativeReal(file);
		definition.Length = ReadNativeReal(file);
		definition.Elastic = ReadNativeReal(file);
		definition.FrictionBegin = ReadNativeReal(file);
		definition.FrictionEnd = ReadNativeReal(file);
		Require(ReadNativeWord(file) == 8, "native adhoc vector has an unexpected step count");
		sgre::scene::EmoteSoftPendulum model(definition);
		sgre::scene::EmoteSoftPendulumPosture posture;
		model.SetPosture(posture);
		model.Initialize();
		for (std::size_t step = 0; step < 8; ++step) {
			for (auto& axis : posture.Coordinate) {
				axis = ReadNativeReal(file);
			}
			const auto seconds = ReadNativeReal(file);
			model.SetPosture(posture);
			model.ClearForcesAndMeasureBounds();
			for (std::size_t particle = 0; particle < definition.ParticleCount; ++particle) {
				std::array<float, 3> force;
				for (auto& axis : force) {
					axis = ReadNativeReal(file);
				}
				model.AddForce(particle, force);
			}
			model.Step(seconds);
			for (std::size_t particle = 0; particle < model.Particles().size(); ++particle) {
				const auto actual = NativeParticleWords(model.Particles()[particle]);
				for (std::size_t word = 0; word < actual.size(); ++word) {
					const auto expected = ReadNativeDoubleWord(file);
					Require(std::bit_cast<std::uint64_t>(actual[word]) == expected, "native adhoc state mismatch case=" + std::to_string(case_index) + " step=" + std::to_string(step) + " particle=" + std::to_string(particle) + " word=" + std::to_string(word) + " actual=" + std::to_string(std::bit_cast<std::uint64_t>(actual[word])) + " expected=" + std::to_string(expected));
					++scalar_words;
				}
				++particle_states;
			}
		}
	}
	Require(file.peek() == std::char_traits<char>::eof() && particle_states == 272 && scalar_words == 5168, "native adhoc vector scope or trailing bytes changed");
	std::cout << "Native adhoc:64 steps/" << particle_states << " actual particle states/" << scalar_words << " exact double words\n";
}

template <std::size_t Count>
struct RetainedAdhocStep {
	std::array<std::uint32_t, 3> Posture{};
	std::uint32_t Seconds = 0;
	std::array<std::array<std::uint32_t, 3>, Count> Forces{};
	std::array<std::array<std::uint64_t, 6>, Count> PositionVelocity{};
};

template <std::size_t Count>
static void ReplayRetainedAdhoc(const sgre::scene::EmoteSoftPendulumDefinition& definition, const std::array<RetainedAdhocStep<Count>, 8>& steps) {
	sgre::scene::EmoteSoftPendulum model(definition);
	sgre::scene::EmoteSoftPendulumPosture posture;
	model.SetPosture(posture);
	model.Initialize();
	for (std::size_t step_index = 0; step_index < steps.size(); ++step_index) {
		const auto& step = steps[step_index];
		for (std::size_t axis = 0; axis < 3; ++axis) {
			posture.Coordinate[axis] = std::bit_cast<float>(step.Posture[axis]);
		}
		model.SetPosture(posture);
		model.ClearForcesAndMeasureBounds();
		for (std::size_t particle = 0; particle < Count; ++particle) {
			std::array<float, 3> force;
			for (std::size_t axis = 0; axis < 3; ++axis) {
				force[axis] = std::bit_cast<float>(step.Forces[particle][axis]);
			}
			model.AddForce(particle, force);
		}
		model.Step(std::bit_cast<float>(step.Seconds));
		for (std::size_t particle = 0; particle < Count; ++particle) {
			const auto& actual = model.Particles()[particle];
			for (std::size_t axis = 0; axis < 3; ++axis) {
				Require(std::bit_cast<std::uint64_t>(actual.Position[axis]) == step.PositionVelocity[particle][axis] && std::bit_cast<std::uint64_t>(actual.Velocity[axis]) == step.PositionVelocity[particle][axis + 3], "retained native adhoc position/velocity differs step=" + std::to_string(step_index) + " particle=" + std::to_string(particle) + " axis=" + std::to_string(axis));
			}
		}
	}
}

static void TestAnalyticAdhocProjection() {
	auto definition = UnitDefinition();
	definition.AdhocSimulation = true;
	sgre::scene::EmoteSoftPendulum model(definition);
	sgre::scene::EmoteSoftPendulumPosture posture;
	model.SetPosture(posture);
	model.Initialize();
	model.ClearForcesAndMeasureBounds();
	model.AddForce(1, {0, 3, 0});
	model.Step(0.5f);
	// Native velocity projection removes this outward unit-line motion before prediction; XPBD has a nonzero compliant extension.
	RequireNear(model.Particles()[1].Position[1], 1, 0, "adhoc projected rest position");
	RequireNear(model.Particles()[1].Velocity[1], 0, 0, "adhoc projected outgoing velocity");
	RequireNear(model.Particles()[1].AccelerationProjection, 1, 0, "actual preceding-segment gravity projection");
}

static void TestRetainedNativeAdhoc0() {
	// Bounded synthetic native input/output observations; expected particle state is never assigned to the owner.
	auto definition = UnitDefinition();
	definition.AdhocSimulation = true;
	definition.ParticleCount = 2;
	definition.Acceleration = {std::bit_cast<float>(std::uint32_t{0x00000000}), std::bit_cast<float>(std::uint32_t{0x3f800000}), std::bit_cast<float>(std::uint32_t{0x00000000})};
	definition.Mass = std::bit_cast<float>(std::uint32_t{0x40000000});
	definition.Length = std::bit_cast<float>(std::uint32_t{0x3f800000});
	definition.Elastic = std::bit_cast<float>(std::uint32_t{0x40000000});
	definition.FrictionBegin = std::bit_cast<float>(std::uint32_t{0x00000000});
	definition.FrictionEnd = std::bit_cast<float>(std::uint32_t{0x00000000});
	std::array<RetainedAdhocStep<2>, 8> steps;
	steps[0].Posture = {0x00000000, 0x00000000, 0x00000000};
	steps[0].Seconds = 0x3c888889;
	steps[0].Forces[0] = {0x00000000, 0x00000000, 0x00000000};
	steps[0].Forces[1] = {0x00000000, 0x00000000, 0x00000000};
	steps[0].PositionVelocity[0] = {0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000};
	steps[0].PositionVelocity[1] = {0x0000000000000000, 0x3ff0000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000};
	steps[1].Posture = {0x00000000, 0x00000000, 0x00000000};
	steps[1].Seconds = 0x3e000000;
	steps[1].Forces[0] = {0x00000000, 0x00000000, 0x00000000};
	steps[1].Forces[1] = {0xc0300000, 0x00000000, 0x3fa00000};
	steps[1].PositionVelocity[0] = {0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000};
	steps[1].PositionVelocity[1] = {0xbf95a7db7ca4b8b7, 0x3feffdca0f67b3c8, 0x3f83afdece67338f, 0xbfc5a7db7ca4b8b7, 0xbf61af84c261c000, 0x3fb3afdece67338f};
	steps[2].Posture = {0x00000000, 0x40400000, 0x00000000};
	steps[2].Seconds = 0x3e800000;
	steps[2].Forces[0] = {0x00000000, 0x00000000, 0x00000000};
	steps[2].Forces[1] = {0xc0200000, 0xbf000000, 0xc0400000};
	steps[2].PositionVelocity[0] = {0x0000000000000000, 0x4008000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000};
	steps[2].PositionVelocity[1] = {0xbfb27d4d18806000, 0x400006798b7ed4a5, 0xbfa0f49316ef0e27, 0xbfca26ac72ae63a4, 0x40100e0e0f49cf66, 0xbfc5e08aca88db0b};
	steps[3].Posture = {0x00000000, 0xc0000000, 0x00000000};
	steps[3].Seconds = 0x3d088889;
	steps[3].Forces[0] = {0x00000000, 0x00000000, 0x00000000};
	steps[3].Forces[1] = {0x00000000, 0xbfa00000, 0xbe800000};
	steps[3].PositionVelocity[0] = {0x0000000000000000, 0xc000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000};
	steps[3].PositionVelocity[1] = {0xbf938e80ff5b93aa, 0xbff000edb54b43a8, 0xbf8346e2af334cc8, 0x3ff98023ffeda748, 0xc05686814c0c7def, 0x3fe6c15974f70045};
	steps[4].Posture = {0x3e800000, 0xbe000000, 0x3f000000};
	steps[4].Seconds = 0x3c888889;
	steps[4].Forces[0] = {0x00000000, 0x00000000, 0x00000000};
	steps[4].Forces[1] = {0x3f400000, 0x40200000, 0xbfc00000};
	steps[4].PositionVelocity[0] = {0x3fd0000000000000, 0xbfc0000000000000, 0x3fe0000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000};
	steps[4].PositionVelocity[1] = {0x3fc348322a6ceb32, 0xbff19526c10a401d, 0x3fd2f186b7058150, 0x40245e6213e041fb, 0xc017af579b775e33, 0x40325301dfae8857};
	steps[5].Posture = {0x00000000, 0x00000000, 0x00000000};
	steps[5].Seconds = 0x3e000000;
	steps[5].Forces[0] = {0x00000000, 0x00000000, 0x00000000};
	steps[5].Forces[1] = {0x3e800000, 0x40200000, 0xc0400000};
	steps[5].PositionVelocity[0] = {0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000};
	steps[5].PositionVelocity[1] = {0x3fda7f9baa123ac5, 0xbfe0c3c7071627dd, 0x3fe7d1f702902a0a, 0x4000db8294dbc52c, 0x401266867afe585d, 0x400cb2674e1ad2c4};
	steps[6].Posture = {0x00000000, 0x00000000, 0x00000000};
	steps[6].Seconds = 0x3e800000;
	steps[6].Forces[0] = {0x00000000, 0x00000000, 0x00000000};
	steps[6].Forces[1] = {0x40100000, 0x3e800000, 0xbf000000};
	steps[6].PositionVelocity[0] = {0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000};
	steps[6].PositionVelocity[1] = {0x3fdfc46c52a00c19, 0x3fd5e1582e166c16, 0x3fe988e5a2ac2dd7, 0x3fd51342a2374550, 0x400bb4731e215de8, 0x3fcb6eea01c03cd0};
	steps[7].Posture = {0x00000000, 0x00000000, 0x00000000};
	steps[7].Seconds = 0x3d088889;
	steps[7].Forces[0] = {0x00000000, 0x00000000, 0x00000000};
	steps[7].Forces[1] = {0xc0100000, 0xbf800000, 0x3fa00000};
	steps[7].PositionVelocity[0] = {0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000};
	steps[7].PositionVelocity[1] = {0x3fdeac3e19915388, 0x3fdbc01a3ecf592b, 0x3fe86b3074f03b70, 0xbfe06ab5497f71df, 0x40060357ab728c59, 0xbff0bd9d9f5d4a1e};
	ReplayRetainedAdhoc(definition, steps);
}

static void TestRetainedNativeAdhoc7() {
	// Bounded synthetic native input/output observations; expected particle state is never assigned to the owner.
	auto definition = UnitDefinition();
	definition.AdhocSimulation = true;
	definition.ParticleCount = 8;
	definition.Acceleration = {std::bit_cast<float>(std::uint32_t{0x3f800000}), std::bit_cast<float>(std::uint32_t{0x00000000}), std::bit_cast<float>(std::uint32_t{0x00000000})};
	definition.Mass = std::bit_cast<float>(std::uint32_t{0x40600000});
	definition.Length = std::bit_cast<float>(std::uint32_t{0x40600000});
	definition.Elastic = std::bit_cast<float>(std::uint32_t{0x00000000});
	definition.FrictionBegin = std::bit_cast<float>(std::uint32_t{0x3e800000});
	definition.FrictionEnd = std::bit_cast<float>(std::uint32_t{0x3f800000});
	std::array<RetainedAdhocStep<8>, 8> steps;
	steps[0].Posture = {0x00000000, 0x00000000, 0x00000000};
	steps[0].Seconds = 0x3c888889;
	steps[0].Forces[0] = {0x00000000, 0x00000000, 0x00000000};
	steps[0].Forces[1] = {0x00000000, 0x00000000, 0x00000000};
	steps[0].Forces[2] = {0x00000000, 0x00000000, 0x00000000};
	steps[0].Forces[3] = {0x00000000, 0x00000000, 0x00000000};
	steps[0].Forces[4] = {0x00000000, 0x00000000, 0x00000000};
	steps[0].Forces[5] = {0x00000000, 0x00000000, 0x00000000};
	steps[0].Forces[6] = {0x00000000, 0x00000000, 0x00000000};
	steps[0].Forces[7] = {0x00000000, 0x00000000, 0x00000000};
	steps[0].PositionVelocity[0] = {0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000};
	steps[0].PositionVelocity[1] = {0x3fe0000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000};
	steps[0].PositionVelocity[2] = {0x3ff0000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000};
	steps[0].PositionVelocity[3] = {0x3ff8000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000};
	steps[0].PositionVelocity[4] = {0x4000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000};
	steps[0].PositionVelocity[5] = {0x4004000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000};
	steps[0].PositionVelocity[6] = {0x4008000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000};
	steps[0].PositionVelocity[7] = {0x400c000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000};
	steps[1].Posture = {0x00000000, 0x00000000, 0x00000000};
	steps[1].Seconds = 0x3e000000;
	steps[1].Forces[0] = {0x00000000, 0x00000000, 0x00000000};
	steps[1].Forces[1] = {0x3fa00000, 0x40300000, 0xbf000000};
	steps[1].Forces[2] = {0x40000000, 0xc0400000, 0xbfc00000};
	steps[1].Forces[3] = {0x3f800000, 0x40400000, 0xbf000000};
	steps[1].Forces[4] = {0xc0100000, 0x40300000, 0xc0400000};
	steps[1].Forces[5] = {0xbfc00000, 0x00000000, 0x40000000};
	steps[1].Forces[6] = {0x3f000000, 0x40200000, 0x3f400000};
	steps[1].Forces[7] = {0x3fa00000, 0xbf400000, 0x3e800000};
	steps[1].PositionVelocity[0] = {0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000};
	steps[1].PositionVelocity[1] = {0x3fe0354d38cee273, 0x3faa7f809d4361af, 0xbf8f62938c607515, 0x3fa97607b51c2b81, 0x3fd950aadf62a8c0, 0xbfbdfbe3d189dd92};
	steps[1].PositionVelocity[2] = {0x3ff00410a6619525, 0xbfa4dd6016fd8d05, 0xbfa16a13aa00a5f7, 0x3f7ea20a5f5ac489, 0xbfd3a762835ed5fa, 0xbfd06759614777c3};
	steps[1].PositionVelocity[3] = {0x3ff7e37e55469c01, 0x3fb0251c5b752bb3, 0xbf996eab7a51580d, 0xbfaa7867ac25ff12, 0x3fddfbc6f2fe2c96, 0xbfc79d9f3ab93f7a};
	steps[1].PositionVelocity[4] = {0x3fffe7587e714259, 0x3fb13d46aebd837d, 0xbfae64b2d5fc71f9, 0xbfa69027b60fd9fa, 0x3fdf8ddf16b1bc19, 0xbfdbd0ba861f084e};
	steps[1].PositionVelocity[5] = {0x4003db63541a8c95, 0x3f8aa6e1b1564807, 0x3fa530e0033370ca, 0xbfc082121407783f, 0x3fb808c6f47dceab, 0x3fd31c1327753edb};
	steps[1].PositionVelocity[6] = {0x4007d6e8ee57020e, 0x3fb032a5e13a879b, 0x3f979d6e9b982549, 0xbfc240886a7e8411, 0x3fdcc7b47277fea8, 0x3fc4fab7fa3a97fb};
	steps[1].PositionVelocity[7] = {0x400bc94c694d9a1e, 0xbf90ef996a8aa969, 0x3f8007857cf180b4, 0xbfc7ee91ee0c92e0, 0xbfbda34c7a72a878, 0x3fac0d299aa6a13b};
	steps[2].Posture = {0x3fc00000, 0x00000000, 0x00000000};
	steps[2].Seconds = 0x3e800000;
	steps[2].Forces[0] = {0x00000000, 0x00000000, 0x00000000};
	steps[2].Forces[1] = {0x3e800000, 0x3fa00000, 0xbfa00000};
	steps[2].Forces[2] = {0x3f000000, 0x40100000, 0x40000000};
	steps[2].Forces[3] = {0x00000000, 0x3fc00000, 0xbfe00000};
	steps[2].Forces[4] = {0x3fa00000, 0x40000000, 0xbfe00000};
	steps[2].Forces[5] = {0x40300000, 0xc0000000, 0x3fe00000};
	steps[2].Forces[6] = {0xc0100000, 0x3fa00000, 0xc0400000};
	steps[2].Forces[7] = {0x00000000, 0x40100000, 0xc0300000};
	steps[2].PositionVelocity[0] = {0x3ff8000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000};
	steps[2].PositionVelocity[1] = {0x3ff1249efbc51524, 0x3fc09d898c320792, 0xbfb7171200e153c4, 0x400076bb3ffcd847, 0x3fd232991c510398, 0xbfd174a5503ff13e};
	steps[2].PositionVelocity[2] = {0x3ff093670d61cf0c, 0x3fc25c2c57d9569e, 0x3fb50214e8348477, 0x3fbfaccec2e83955, 0x3fe4d6f74996ffba, 0x3fda44269987d9de};
	steps[2].PositionVelocity[3] = {0x3ff7c0ba461ecdc4, 0x3fc7566bee7dc0b1, 0xbfc15eb09772bcc3, 0xbf9dcc9f46b0c692, 0x3fda2b33014e9296, 0xbfd853c0d720f9de};
	steps[2].PositionVelocity[4] = {0x3fffae7e7b02bade, 0x3fc1505f5b4df62c, 0xbfb6631c4ce5d752, 0xbfa79a83b5e3ad5a, 0x3fcce09e31a3a4fe, 0xbfb7e1f5009cbb9c};
	steps[2].PositionVelocity[5] = {0x40038f54b7127e1d, 0x3f7e9c9836f310ce, 0x3fa71d88faee71eb, 0xbfbe8f03aa18195c, 0xbf923c14f9fe33f8, 0x3f88be334c88e9f2};
	steps[2].PositionVelocity[6] = {0x40074786cce6fa4c, 0x3fbed0d05743ff72, 0xbfb99d8149f00469, 0xbfcbd833109cef64, 0x3fc6b5b8d2ceb59c, 0xbfd87bc70d5d2186};
	steps[2].PositionVelocity[7] = {0x400b06e95a06361b, 0x3fc960e837773041, 0xbfcfa31fe230954f, 0xbfd239496eb16048, 0x3fe49f248b966412, 0xbfe87ab22b7fc204};
	steps[3].Posture = {0xbf800000, 0x00000000, 0x00000000};
	steps[3].Seconds = 0x3d088889;
	steps[3].Forces[0] = {0x00000000, 0x00000000, 0x00000000};
	steps[3].Forces[1] = {0x40400000, 0xbfc00000, 0xc0300000};
	steps[3].Forces[2] = {0x3fe00000, 0xbf800000, 0x3fa00000};
	steps[3].Forces[3] = {0xbf400000, 0x40200000, 0xbf400000};
	steps[3].Forces[4] = {0x40100000, 0xbe800000, 0x40300000};
	steps[3].Forces[5] = {0xbf000000, 0xbe800000, 0x3f800000};
	steps[3].Forces[6] = {0x3fc00000, 0x3f800000, 0x40200000};
	steps[3].Forces[7] = {0x3f800000, 0x40200000, 0x00000000};
	steps[3].PositionVelocity[0] = {0xbff0000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000};
	steps[3].PositionVelocity[1] = {0xbfe00d130fd1f6cc, 0x3fa0270c232979ce, 0xbf990b7989588aac, 0xc047508ce0c344ba, 0xc0074d356a3ebfb6, 0x3fff2db6621ee0a8};
	steps[3].PositionVelocity[2] = {0x3ff0c5d7684690aa, 0x3fc4e369c2aaaa60, 0x3fb8c49561750e70, 0x3fd746fe700669d9, 0x3fe2ab2c4affdc0a, 0x3fdbc30b66a4b454};
	steps[3].PositionVelocity[3] = {0x3ff7c3bb514917e5, 0x3fc978d5cb377647, 0xbfc261c565b6c395, 0x3f9619f6a36a874c, 0x3fdf681593cd796c, 0xbfcdc8642dd3545e};
	steps[3].PositionVelocity[4] = {0x3fffa7ec5fc6ded3, 0x3fc22d609a856265, 0xbfb59b40f728ed90, 0xbfa815384dd5f150, 0x3fc9502d9dcb2004, 0x3fb6e415fd773f7f};
	steps[3].PositionVelocity[5] = {0x40038b4e00d77e19, 0x3f82edc2aa9c8f3e, 0x3fa6bf03dc32eaeb, 0xbfad67e08b58a3b4, 0x3faa75caf47dea47, 0xbf9592ab14d85e8d};
	steps[3].PositionVelocity[6] = {0x40073cdbdabf56c3, 0x3fc05cc70feb4e11, 0xbfbb432354273d49, 0xbfc3682075775493, 0x3fcbc8ee8046ffd3, 0xbfc7f843e3a12d09};
	steps[3].PositionVelocity[7] = {0x400aefb1f46b8d9e, 0x3fccb0f95c9b2230, 0xbfd153facf5656de, 0xbfd50a34011d4000, 0x3fe8047c37872994, 0xbfe5e30d82b3f683};
	steps[4].Posture = {0x3e800000, 0xbe000000, 0x3f000000};
	steps[4].Seconds = 0x3c888889;
	steps[4].Forces[0] = {0x00000000, 0x00000000, 0x00000000};
	steps[4].Forces[1] = {0x3fa00000, 0x40100000, 0x00000000};
	steps[4].Forces[2] = {0x40300000, 0x3f000000, 0x3f400000};
	steps[4].Forces[3] = {0xbfc00000, 0xbe800000, 0x3f400000};
	steps[4].Forces[4] = {0xbf400000, 0x3f800000, 0x40400000};
	steps[4].Forces[5] = {0x3f800000, 0xbf800000, 0xbfa00000};
	steps[4].Forces[6] = {0x3fc00000, 0xbfc00000, 0x3fa00000};
	steps[4].Forces[7] = {0xc0400000, 0xbfe00000, 0x3fe00000};
	steps[4].PositionVelocity[0] = {0x3fd0000000000000, 0xbfc0000000000000, 0x3fe0000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000};
	steps[4].PositionVelocity[1] = {0x3ff0a9c12d52b8be, 0x3fae39b72a9328a8, 0x3fd1bb7053d9f798, 0x40570200f8a47417, 0x3ffa3acb407134cf, 0x4031fbd40039271f};
	steps[4].PositionVelocity[2] = {0x3fec9fd2d99b95d7, 0x3fbcdebcae430f94, 0x3fac1b16012dd870, 0xc0224fe9d994df0c, 0xc008033a693fcab9, 0xc003ef66f45b4344};
	steps[4].PositionVelocity[3] = {0x3ff624cd7d49f77f, 0x3fc35e74565fa02f, 0xbfa8679207723166, 0xc01814a8caad147f, 0xc006ab20c288d4a3, 0x4016cea193019e41};
	steps[4].PositionVelocity[4] = {0x3ffe281e7f00bb8b, 0x3fc01fdcd5051565, 0xbfab42009615a274, 0xc0163bf4065f4f70, 0xbfee71921ced5dac, 0x3ffd93d563f71184};
	steps[4].PositionVelocity[5] = {0x40030543152eb006, 0x3fab26f4f5447d0a, 0xbf2a7545f4b9022e, 0xc00f013d66e9db04, 0x4004be55785babcf, 0xc00524116e492339};
	steps[4].PositionVelocity[6] = {0x4006e57b59e7e1a1, 0x3fbff6ff7037f23a, 0xbfba7355ce1d2cec, 0xc0042c9a3fd3ca2b, 0xbfc675dd5ff4ce2e, 0x3fc7fd50bae3be63};
	steps[4].PositionVelocity[7] = {0x400a959d7c62d294, 0x3fcd178efd0b6239, 0xbfd12059c6d7d0e0, 0xc004c2b79789e8c0, 0x3fc7a47be4d512be, 0x3fc7cc39d524b6ab};
	steps[5].Posture = {0x00000000, 0x00000000, 0x00000000};
	steps[5].Seconds = 0x3e000000;
	steps[5].Forces[0] = {0x00000000, 0x00000000, 0x00000000};
	steps[5].Forces[1] = {0xbf000000, 0xc0200000, 0xbf000000};
	steps[5].Forces[2] = {0xbfa00000, 0xbf400000, 0xc0100000};
	steps[5].Forces[3] = {0xbf400000, 0xbf400000, 0xbf400000};
	steps[5].Forces[4] = {0xbfe00000, 0x40000000, 0xc0400000};
	steps[5].Forces[5] = {0xbf400000, 0xbfa00000, 0xbf400000};
	steps[5].Forces[6] = {0xbf000000, 0xbf800000, 0xbf000000};
	steps[5].Forces[7] = {0x3fc00000, 0x00000000, 0xc0300000};
	steps[5].PositionVelocity[0] = {0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000};
	steps[5].PositionVelocity[1] = {0xbfdff208d1def30d, 0xbf6808ec1f5722e3, 0xbfbafb88d19130cd, 0xc0278c8e16916bb0, 0xbfde4fabf1f95d11, 0xc00762936b4dc556};
	steps[5].PositionVelocity[2] = {0x3ffc79b67f0637c8, 0x3fd04865c4fa24b4, 0x3fd1772192a9a370, 0x401aaec00dc12884, 0x3ff1140edc71f418, 0x3ffa48e9c8063814};
	steps[5].PositionVelocity[3] = {0x3ff8b7163f6fc57c, 0x3fbd2993f0c92f5a, 0xbf6d8b4f16913960, 0x3ff31a1c7dab1ec5, 0xbfd1c878cac8fb07, 0x3fd4f25f94762df8};
	steps[5].PositionVelocity[4] = {0x3ffebb98265f5a87, 0x3fb2da62eea5c1d0, 0x3f8a0c34ff81d1c9, 0x3fd0deeab7d30b66, 0xbfd884ea2dda126e, 0x3fdee7c4a986ee19};
	steps[5].PositionVelocity[5] = {0x4002cb84ff5a771f, 0x3fcb60304c0d1328, 0xbfca055ea32d52f8, 0xbfca0922fc42ccba, 0x3ff290d0e66e1084, 0xbff77129c2ced7f7};
	steps[5].PositionVelocity[6] = {0x4006ba053b44527e, 0x3fc0de29e2ac5f16, 0xbfc4b1935e10f77e, 0xbfc34e2ec0a70268, 0x3fa92bc1032d9c14, 0xbfda8a1a149f4faa};
	steps[5].PositionVelocity[7] = {0x40087d468971957c, 0x3fd0060f3cd30248, 0xbfd5239629858575, 0xbffd54c149315750, 0x3fc4afec683a7061, 0xbfdc16a6b2bff013};
	steps[6].Posture = {0x00000000, 0x00000000, 0x00000000};
	steps[6].Seconds = 0x3e800000;
	steps[6].Forces[0] = {0x00000000, 0x00000000, 0x00000000};
	steps[6].Forces[1] = {0x40000000, 0x3fe00000, 0xbfc00000};
	steps[6].Forces[2] = {0xbe800000, 0x40400000, 0x3f800000};
	steps[6].Forces[3] = {0x3f000000, 0xbe800000, 0x40400000};
	steps[6].Forces[4] = {0xbfc00000, 0x3fc00000, 0x00000000};
	steps[6].Forces[5] = {0xc0300000, 0x40000000, 0xbf800000};
	steps[6].Forces[6] = {0xbfc00000, 0xbf800000, 0xbfe00000};
	steps[6].Forces[7] = {0xc0200000, 0x3fe00000, 0xbf800000};
	steps[6].PositionVelocity[0] = {0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000};
	steps[6].PositionVelocity[1] = {0x3ff737fa73d473a8, 0xbfc284dfb8b60976, 0x3fd0f005b4bb1014, 0x401c6b3aab8e8786, 0xbfe08606ac0f0b30, 0x3ff59193344ed89c};
	steps[6].PositionVelocity[2] = {0x3fef360425134f78, 0xbfc9d9c7074f866e, 0x3fbf43edac75e930, 0xc006c0913fc9ec5e, 0xbff9d1631033af49, 0xbfe10edee157c8bb};
	steps[6].PositionVelocity[3] = {0x3fff7d9d5eda8d2d, 0x3f8b08d93001a298, 0x3fdd018146f0690b, 0x3ff73af3d96e1a5f, 0xbfd6198c1b87b298, 0x3ff90f5d9fd02e6c};
	steps[6].PositionVelocity[4] = {0x3fff433baceddbb8, 0x3fc5bf4a2361cc7e, 0x3fb385bfd3bb9507, 0x3fbc283b5c708875, 0x3fd4760d8b7434f4, 0x3fcb0395da964916};
	steps[6].PositionVelocity[5] = {0x40036c2edb16da90, 0x3fb11444f42b5f4d, 0xbfa72a0b0ee5eab0, 0x3fd02358edc7b3ec, 0xbfde45bac84d96c8, 0x3fe0419541463b86};
	steps[6].PositionVelocity[6] = {0x4005655051648341, 0x3f943d549112f523, 0xbfde918a50c46ae3, 0xbfe08a7f5ab90fad, 0xbfd64661388d40b0, 0xbfef6a7468fad386};
	steps[6].PositionVelocity[7] = {0x4004143d523746d1, 0x3fd8d49e9ec1ce42, 0xbfdbdad695821792, 0xbffa76374b5dd802, 0x3fda6bae25cc63ee, 0xbfd425c143f5b657};
	steps[7].Posture = {0x00000000, 0x00000000, 0x00000000};
	steps[7].Seconds = 0x3d088889;
	steps[7].Forces[0] = {0x00000000, 0x00000000, 0x00000000};
	steps[7].Forces[1] = {0xc0000000, 0x3e800000, 0x40200000};
	steps[7].Forces[2] = {0x40100000, 0x40100000, 0x3f800000};
	steps[7].Forces[3] = {0xbfc00000, 0xc0100000, 0x40200000};
	steps[7].Forces[4] = {0xc0300000, 0x40300000, 0x3f400000};
	steps[7].Forces[5] = {0x3f800000, 0xbe800000, 0xc0000000};
	steps[7].Forces[6] = {0xc0400000, 0x40400000, 0x3fa00000};
	steps[7].Forces[7] = {0xbfa00000, 0xbf400000, 0x3f000000};
	steps[7].PositionVelocity[0] = {0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000};
	steps[7].PositionVelocity[1] = {0x3fd0c0092c26d130, 0xbfaeaa7d0fca1340, 0x3f9e5072428e3b28, 0xc041a118ad086b2d, 0x40041b350fa9e0a3, 0xc01bdea787bc3417};
	steps[7].PositionVelocity[2] = {0x3ff98591f3088b7c, 0xbfb53b98fcc6f826, 0x3fd51e4d51fb4dbe, 0x40324e23d100d625, 0x400c1f4a629e7799, 0x40188e28d17ea250};
	steps[7].PositionVelocity[3] = {0x3ff6fb0cb355e306, 0xbfbd23e217362fa9, 0x3fd2db6b970d02fc, 0xc02f4e01c8bb999a, 0xc00de80436c2a44e, 0xc012aa9eae497b9f};
	steps[7].PositionVelocity[4] = {0x3fff64a9b424b68f, 0x3fc793a900a38e38, 0x3fb5197f83484ef9, 0x3fcea1acc9fbb177, 0x3fdad2a58aa0ad72, 0x3fc71f3ebd8e3b6e};
	steps[7].PositionVelocity[5] = {0x40036f98fb452db7, 0x3fab4fdd3e322679, 0xbfa33e66e6df076a, 0x3fa8f03d3bedb772, 0xbfd9006429258759, 0x3fcca22a4177e902};
	steps[7].PositionVelocity[6] = {0x400523d49a8ae767, 0x3f913bff64a50f8f, 0xbfdf4313552c3684, 0xbfedc81f32ae6cd0, 0xbfb5de44db0260da, 0xbfd42f8af72b2cb3};
	steps[7].PositionVelocity[7] = {0x40039fec2f4544c2, 0x3fd99ac4daa0aa82, 0xbfdc748f1719524a, 0xbffa5a61d2f9d3d0, 0x3fd67254b3ed87ae, 0xbfd169e69e5e7b9c};
	ReplayRetainedAdhoc(definition, steps);
}

int main(int argc, char** argv) {
	try {
		Require(argc <= 2, "usage: emote_soft_pendulum_tests [native-adhoc-vectors.bin]");
		TestInitializedOwnedParticlesAndOutput();
		TestCoordinateScaleAndNativeOffsetOutput();
		TestTiltedMassAndForcePhase();
		TestAnalyticOneConstraintStepAndDamping();
		TestCompressionSpringAndUnclampedDamping();
		TestRotationAndSafetyBoundaries();
		TestAnalyticAdhocProjection();
		TestRetainedNativeAdhoc0();
		TestRetainedNativeAdhoc7();
		if (argc == 2) {
			TestAllNativeAdhocVectors(argv[1]);
		}
		std::cout << "Emote soft pendulum: actual owned initialization/mass/posture/force phases, analytic XPBD, compression, damping and output passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
