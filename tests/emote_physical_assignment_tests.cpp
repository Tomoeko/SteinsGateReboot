#include "sgre/scene/emote_bloom_physics.hpp"
#include "sgre/scene/emote_bobble_model.hpp"
#include "sgre/scene/emote_bobble_sections.hpp"
#include "sgre/scene/emote_emission_emitter.hpp"
#include "sgre/scene/emote_soft_pendulum.hpp"
#include "sgre/scene/emote_tress_model.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static void TestBloomCopiesMutablePrefixAndKeepsCalibration() {
	sgre::scene::EmoteBloomDefinition source_definition;
	source_definition.SizeHorizontal = 2;
	source_definition.SizeVertical = 3;
	source_definition.Tresses = {{.2f, .3f}};
	auto target_definition = source_definition;
	target_definition.SizeHorizontal = 4;
	target_definition.Mass = 2;
	target_definition.Tresses.push_back({.7f, .8f});
	sgre::scene::EmoteBloomPhysics target(target_definition);
	sgre::scene::EmotePhysicalPosture target_posture;
	target_posture.Coordinate = {-3, 4, 2};
	target.SetPosture(target_posture);
	target.Initialize();
	const auto retained_point = target.Points()[1];
	const auto retained_relative = target.Points()[0].Relative;
	const auto retained_influences = target.Points()[0].Influences;
	sgre::scene::EmoteBloomPoint copied;
	{
		sgre::scene::EmoteBloomPhysics source(source_definition);
		sgre::scene::EmotePhysicalPosture posture;
		posture.Coordinate = {5, 6, 1};
		source.SetPosture(posture);
		source.Initialize();
		source.ClearForces();
		source.AddForce(0, {3, 2, 1});
		source.Update(1);
		copied = source.Points()[0];
		target.RequireCanAssignState(source);
		target.AssignState(source);
		Require(target.Points().data() != source.Points().data(), "Bloom assignment must retain independent physical storage");
	}
	const auto& actual = target.Points()[0];
	Require(actual.Target == copied.Target && actual.Previous == copied.Previous && actual.Predicted == copied.Predicted && actual.Coordinate == copied.Coordinate && actual.Velocity == copied.Velocity && actual.Force == copied.Force, "Bloom imports all six mutable vectors after source retirement");
	Require(actual.Relative.X == retained_relative.X && actual.Relative.Y == retained_relative.Y && actual.Influences == retained_influences && target.Definition().Mass == 2 && target.Definition().SizeHorizontal == 4, "Bloom imports no destination calibration or influence catalogue");
	Require(target.Points().size() == 2 && target.Points()[1].Coordinate == retained_point.Coordinate, "Bloom copies the native matched prefix without resizing or touching the unmatched point");
	target.AssignState(target);
	Require(target.Points()[0].Velocity == copied.Velocity, "Bloom self assignment preserves completed physical state");
	target.SetPosture(target_posture);
	target.ClearForces();
	target.Update(1);
	static_cast<void>(target.PrepareLattice());
}

static sgre::scene::EmoteSoftPendulumDefinition SoftDefinition(std::uint32_t count) {
	sgre::scene::EmoteSoftPendulumDefinition definition;
	definition.Acceleration = {0, 1, 0};
	definition.Length = 3;
	definition.ParticleCount = count;
	definition.TrailingLength = 1;
	definition.TrailingPoints = 2;
	return definition;
}

static void TestSoftSnapshotOwnsStateAndRetainsUnmatchedParticles() {
	sgre::scene::EmoteSoftPendulum target(SoftDefinition(4));
	target.SetPosture({{-2, 5, 0}, 0});
	target.Initialize();
	const auto last = target.Particles().back();
	std::array<double, 3> copied_position{};
	std::array<double, 3> copied_velocity{};
	{
		sgre::scene::EmoteSoftPendulum source(SoftDefinition(3));
		source.SetPosture({{4, 2, 0}, 0});
		source.Initialize();
		source.ClearForcesAndMeasureBounds();
		source.AddForce(2, {5, 0, 0});
		source.Step(1.0f / 60.0f);
		copied_position = source.Particles()[2].Position;
		copied_velocity = source.Particles()[2].Velocity;
		target.AssignState(source);
		Require(target.Particles().data() != source.Particles().data() && target.TrailingPositions().data() != source.TrailingPositions().data(), "soft import retains its destination particle and trailing owners");
		Require(target.TrailingPositions()[0] == source.TrailingPositions()[0], "soft import includes actual trailing positions");
	}
	Require(target.Particles().size() == 4 && target.Particles()[2].Position == copied_position && target.Particles()[2].Velocity == copied_velocity && target.Particles()[3].Position == last.Position, "soft native prefix import survives source destruction and retains unmatched state");
	target.SetPosture({{0, 0, 0}, .1f});
	target.ClearForcesAndMeasureBounds();
	target.Step(1.0f / 60.0f);
}

static void TestBobbleSnapshotRetainsIndependentShapeOwner() {
	sgre::scene::EmoteBobbleDefinition definition;
	definition.Length = 2;
	definition.MassWeights = {0, 1, 1};
	sgre::scene::EmoteBobbleSectionDefinition sections;
	sections.Radii = {1, 1, 1};
	sgre::scene::EmoteBobbleModel target(definition, sections);
	target.SetPosture({{-1, 2, 0}, 0});
	target.Initialize();
	std::array<float, 3> expected{};
	{
		sgre::scene::EmoteBobbleModel source(definition, sections);
		source.SetPosture({{3, 4, 0}, 0});
		source.Initialize();
		source.ClearForces();
		source.AddForce(2, {2, 0, 0});
		source.Step(1.0f / 60.0f);
		expected = source.Particles()[2].Position;
		target.AssignState(source);
		Require(target.Particles().data() != source.Particles().data(), "Bobble import cannot retain a source particle borrow");
	}
	Require(target.Particles()[2].Position == expected, "Bobble source-first retirement preserves the actual imported particle state");
	target.SetPosture({{1, 1, 0}, .2f});
	target.ClearForces();
	target.Step(1.0f / 60.0f);
}

static void TestTressMatchesRigidAndSoftSnapshotsSeparately() {
	sgre::scene::EmoteTressDefinition definition;
	definition.Gravity = 1;
	definition.First.Mass = 1;
	definition.First.Length = 2;
	definition.First.ParticleCount = 3;
	definition.Second = definition.First;
	definition.RigidSide = sgre::scene::EmoteTressRigidSide::First;
	sgre::scene::EmoteTressModel source(definition);
	definition.RigidSide = sgre::scene::EmoteTressRigidSide::Second;
	sgre::scene::EmoteTressModel target(definition);
	sgre::scene::EmotePhysicalPosture posture;
	posture.Coordinate = {4, 5, 0};
	source.SetPosture(posture);
	source.Initialize();
	posture.Coordinate = {-2, -3, 0};
	target.SetPosture(posture);
	target.Initialize();
	std::array<std::array<float, 2>, 5> source_points{};
	std::array<std::array<float, 2>, 5> target_points{};
	source.WritePoints(source_points);
	target.AssignState(source);
	target.WritePoints(target_points);
	// Native3237d0 pairs each separate rigid/soft vector by ordinal, even when their branch sides differ.
	Require(target_points[0] == source_points[4] && target_points[1] == source_points[3] && target_points[2] == source_points[2] && target_points[3] == source_points[1] && target_points[4] == source_points[0], "Tress assignment must match physical categories rather than source branch sides");
}

static void TestEmissionPoolAndEmitterKeepTheirOwners() {
	sgre::scene::EmoteEmissionPoolDefinition definition;
	definition.MaximumParticles = 8;
	sgre::scene::EmoteEmissionPool target_pool(definition);
	sgre::scene::EmoteEmissionRandomState target_random{7, 3};
	sgre::scene::EmoteEmissionEmitterDefinition emitter_definition;
	emitter_definition.First = {1, 2};
	emitter_definition.Particle.Motions.push_back({"owned-motion", 1, 4, 1});
	sgre::scene::EmoteEmissionEmitter target(target_pool, target_random, emitter_definition);
	{
		sgre::scene::EmoteEmissionPool source_pool(definition);
		sgre::scene::EmoteEmissionRandomState source_random{123, 17};
		emitter_definition.First = {5, 6};
		sgre::scene::EmoteEmissionEmitter source(source_pool, source_random, emitter_definition);
		source.SetPosture({{3, 4}, 0, 0, 1, 0});
		source.Step(1);
		Require(source_pool.Particles().size() == 1, "genuine emitter cadence must create the source particle before assignment");
		target.AssignState(source);
		target_pool.AssignState(source_pool);
		Require(target.Countdown() == source.Countdown() && target.Emitted() == source.Emitted(), "emitter snapshot copies countdown and emitted state");
		Require(target_random.State == 7 && target_random.Increment == 3, "emitter import must not rebind or independently copy its containing RNG");
		Require(target_pool.Particles().data() != source_pool.Particles().data() && target_pool.Particles()[0].Birth.Motion.data() != source_pool.Particles()[0].Birth.Motion.data(), "emission particles and their Motion strings are independently owned");
	}
	Require(target_pool.Particles()[0].Birth.Motion == "owned-motion", "source-first retirement preserves imported Motion identity");
	sgre::scene::EmoteEmissionBirth birth;
	birth.Motion = "next";
	target_pool.Spawn(birth);
	Require(target_pool.Particles().back().Id == 1, "import computes the next ID from copied actual particles");
	target.Step(61);
	Require(target_pool.Particles().size() > 2 && target_random.State != 7, "imported emitter resumes into its own pool and its own containing RNG");
	Require(target_pool.Particles().back().InitialPosition.X == 8 && target_pool.Particles().back().InitialPosition.Y == 10, "Point emitter import also copies its source coordinates through the actual resumed generation path");
}

static void TestRectangleImportPreservesDestinationCorners() {
	sgre::scene::EmoteEmissionPoolDefinition pool_definition;
	sgre::scene::EmoteEmissionPool source_pool(pool_definition);
	sgre::scene::EmoteEmissionPool target_pool(pool_definition);
	sgre::scene::EmoteEmissionRandomState source_random{31, 17};
	sgre::scene::EmoteEmissionRandomState target_random{71, 19};
	sgre::scene::EmoteEmissionEmitterDefinition definition;
	definition.Geometry = sgre::scene::EmoteEmissionGeometry::Rectangle;
	definition.First = {5, 6};
	definition.Second = definition.First;
	sgre::scene::EmoteEmissionEmitter source(source_pool, source_random, definition);
	definition.First = {1, 2};
	definition.Second = definition.First;
	sgre::scene::EmoteEmissionEmitter target(target_pool, target_random, definition);
	source.SetPosture({{3, 4}, 0, 0, 1, 0});
	source.Step(1);
	target.AssignState(source);
	target.Step(61);
	Require(target_pool.Particles().size() == 1 && target_pool.Particles().front().InitialPosition.X == 4 && target_pool.Particles().front().InitialPosition.Y == 6, "Rectangle snapshot must retain destination corners while importing source posture and cadence");
}

int main() {
	try {
		TestBloomCopiesMutablePrefixAndKeepsCalibration();
		TestSoftSnapshotOwnsStateAndRetainsUnmatchedParticles();
		TestBobbleSnapshotRetainsIndependentShapeOwner();
		TestTressMatchesRigidAndSoftSnapshotsSeparately();
		TestEmissionPoolAndEmitterKeepTheirOwners();
		TestRectangleImportPreservesDestinationCorners();
		std::cout << "physical assignment ownership tests passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
