#include "sgre/scene/emote_tress_model.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

static void Require(bool condition, std::string_view operation) {
	if (!condition) {
		throw std::runtime_error(std::string(operation));
	}
}

static void RequireNear(float actual, float expected, float tolerance, std::string_view operation) {
	if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance) {
		throw std::runtime_error(std::string(operation) + ": expected " + std::to_string(expected) + ", actual " + std::to_string(actual));
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
	throw std::runtime_error("expected actual tress-domain rejection");
}

static sgre::scene::EmoteTressDefinition TwoSidedDefinition() {
	sgre::scene::EmoteTressDefinition definition;
	definition.Gravity = 1;
	definition.First.Mass = 2;
	definition.First.Length = 2;
	definition.First.ParticleCount = 3;
	definition.First.TrailingLength = 4;
	definition.First.TrailingPoints = 2;
	definition.Second.Mass = 3;
	definition.Second.Length = 3;
	definition.Second.ParticleCount = 4;
	definition.Second.TrailingLength = 2;
	definition.Second.TrailingPoints = 1;
	return definition;
}

static void TestActualTwoSidedInitializationAndJoinedOutput() {
	sgre::scene::EmoteTressModel model(TwoSidedDefinition());
	Require(model.PointCount() == 9, "actual two branches share exactly one anchor");
	RequireThrows([&] {
		model.Initialize();
	},
	    "preceding posture");
	sgre::scene::EmotePhysicalPosture posture;
	posture.Coordinate = {3, 10, 0};
	model.SetPosture(posture);
	model.Initialize();
	std::array<std::array<float, 2>, 9> points;
	model.WritePoints(points);
	const std::array<float, 9> expected_y{4, 6, 8, 9, 10, 11, 12, 13, 15};
	for (std::size_t index = 0; index != points.size(); ++index) {
		RequireNear(points[index][0], 3, 1e-5f, "actual normalized gravity X");
		RequireNear(points[index][1], expected_y[index], 1e-5f, "first physical branch reverses and joins second at the fix");
	}
	std::array<float, 8> lengths;
	model.WriteSegmentLengths(lengths);
	const std::array<float, 8> expected_lengths{2, 2, 1, 1, 1, 1, 1, 2};
	Require(lengths == expected_lengths, "reversed first trailing lengths precede its reversed physical lengths");
	model.ClearForcesAndMeasureBounds();
	model.AddForce(false, 3, {20, 0, 0});
	model.Step(1.0f / 60.0f);
	model.WritePoints(points);
	Require(points[7][0] > 3, "second actual physical branch responds to its collected force");
	RequireNear(points[4][0], 3, 1e-6f, "joined actual fixed anchor remains stable after physical update");
	RequireNear(points[4][1], 10, 1e-6f, "shared anchor is not duplicated or advanced by solver");
}

static void TestRigidFirstUsesActualSixWordPosture() {
	auto definition = TwoSidedDefinition();
	definition.RigidSide = sgre::scene::EmoteTressRigidSide::First;
	definition.Second = {};
	definition.First.TrailingLength = 0;
	definition.First.TrailingPoints = 0;
	sgre::scene::EmoteTressModel model(definition);
	sgre::scene::EmotePhysicalPosture posture;
	posture.Coordinate = {5, 6, 7};
	// Actual String substeps carry their independently accumulated cosine/sine, not a regenerated angle pair.
	posture.Rotation = 0;
	posture.Cosine = 0;
	posture.Sine = 1;
	model.SetPosture(posture);
	model.Initialize();
	std::array<std::array<float, 2>, 3> points;
	model.WritePoints(points);
	RequireNear(points[0][0], 7, 1e-6f, "native rigid first branch direction uses lent cosine/sine");
	RequireNear(points[0][1], 6, 1e-6f, "native rigid first branch retains actual Y");
	RequireNear(points[2][0], 5, 0, "reversed rigid branch ends at actual anchor");
	RequireThrows([&] {
		model.AddForce(true, 1, {1, 0, 0});
	},
	    "actual initialized soft branch");
	posture.Coordinate = {8, 9, 10};
	posture.Cosine = 1;
	posture.Sine = 0;
	model.SetPosture(posture);
	model.Step(1.0f / 60.0f);
	model.WritePoints(points);
	RequireNear(points[0][1], 7, 1e-6f, "real rigid Step rebuilds from changed posture");
	RequireNear(points[2][1], 9, 0, "rigid anchor follows actual next posture");
}

static void TestOnlySecondAndNativePhysicalOffsetDifference() {
	auto definition = TwoSidedDefinition();
	definition.First = {};
	definition.Second.TrailingPoints = 0;
	definition.CoordinateScale = {2, 3, 1};
	sgre::scene::EmoteTressModel model(definition);
	sgre::scene::EmotePhysicalPosture posture;
	posture.Coordinate = {1, 2, 0};
	model.SetPosture(posture);
	model.Initialize();
	std::array<std::array<float, 2>, 4> points;
	model.WritePoints(points);
	RequireNear(points[0][0], 1, 0, "actual second-only soft anchor output keeps unscaled input");
	RequireNear(points[0][1], 2, 0, "native soft output subtracts coordinate offset");
	RequireNear(points[3][1], 5, 1e-6f, "second-only actual physical segment order");
}

static void TestActualAdhocBranchSelection() {
	sgre::scene::EmoteTressDefinition definition;
	definition.ReferenceRotation = -1.57079637050628662109375f;
	definition.Gravity = 1;
	// Amplify the real fixed XPBD compliance so its extension survives the float point publication.
	definition.Second.Mass = 10000000000.0f;
	definition.Second.Length = 1;
	definition.Second.ParticleCount = 2;
	definition.Elastic = 1;
	definition.AdhocSimulation = true;
	sgre::scene::EmoteTressModel adhoc(definition);
	definition.AdhocSimulation = false;
	sgre::scene::EmoteTressModel compliant(definition);
	sgre::scene::EmotePhysicalPosture posture;
	std::array<std::array<float, 2>, 2> adhoc_points;
	std::array<std::array<float, 2>, 2> compliant_points;
	for (auto* model : {&adhoc, &compliant}) {
		model->SetPosture(posture);
		model->Initialize();
		model->ClearForcesAndMeasureBounds();
		model->AddForce(false, 1, {30000000000.0f, 0, 0});
		model->Step(0.5f);
	}
	adhoc.WritePoints(adhoc_points);
	compliant.WritePoints(compliant_points);
	// The native adhoc preprojection removes this outward velocity; compliant XPBD permits extension.
	RequireNear(adhoc_points[1][0], 1, 0.000001f, "actual tress forwards adhoc selection to its owned physical branch");
	Require(compliant_points[1][0] > adhoc_points[1][0] + 0.01f, "metadata solver selection must preserve distinct native length behavior; adhoc=" + std::to_string(adhoc_points[1][0]) + ", compliant=" + std::to_string(compliant_points[1][0]));
}

static void TestMixedFailureAndBounds() {
	auto definition = TwoSidedDefinition();
	definition.Elastic = 1;
	definition.First.TrailingPoints = 0;
	definition.Second = {};
	sgre::scene::EmoteTressModel model(definition);
	sgre::scene::EmotePhysicalPosture posture;
	model.SetPosture(posture);
	model.Initialize();
	std::array<std::array<float, 2>, 1> wrong_points;
	RequireThrows([&] {
		model.WritePoints(wrong_points);
	},
	    "exact actual initialized point count");
	RequireThrows([&] {
		model.Step(0);
	},
	    "positive finite seconds");
	definition.First = {};
	RequireThrows([&] {
		sgre::scene::EmoteTressModel empty(definition);
	},
	    "at least one actual physical branch");
	definition.First.Mass = 1;
	definition.First.Length = 1;
	definition.First.ParticleCount = 2;
	definition.RigidSide = static_cast<sgre::scene::EmoteTressRigidSide>(3);
	RequireThrows([&] {
		sgre::scene::EmoteTressModel unknown(definition);
	},
	    "rigid-side mode");
}

int main() {
	try {
		TestActualTwoSidedInitializationAndJoinedOutput();
		TestRigidFirstUsesActualSixWordPosture();
		TestOnlySecondAndNativePhysicalOffsetDifference();
		TestActualAdhocBranchSelection();
		TestMixedFailureAndBounds();
		std::cout << "Emote tress: actual two-sided physical ownership, joined points/lengths, force integration, rigid posture and domain guards passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
