#include "sgre/scene/emote_tress_spline.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

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
	throw std::runtime_error("expected actual spline-domain rejection");
}

static sgre::scene::EmoteTressDefinition CornerDefinition() {
	sgre::scene::EmoteTressDefinition definition;
	definition.Gravity = 1;
	definition.GravityRotation = 1.57079637050628662109375f;
	definition.RigidSide = sgre::scene::EmoteTressRigidSide::First;
	definition.First.Mass = 1;
	definition.First.Length = 1;
	definition.First.ParticleCount = 2;
	definition.Second.Mass = 1;
	definition.Second.Length = 1;
	definition.Second.ParticleCount = 2;
	return definition;
}

static void TestAnalyticCornerFromRealRigidAndFreeBranches() {
	sgre::scene::EmoteTressModel model(CornerDefinition());
	sgre::scene::EmoteTressSpline spline(model);
	RequireThrows([&] {
		static_cast<void>(spline.Sample(0));
	},
	    "preceding physical preparation");
	RequireThrows([&] {
		spline.Prepare();
	},
	    "actual initialized point count");
	sgre::scene::EmotePhysicalPosture posture;
	model.SetPosture(posture);
	model.Initialize();
	spline.Prepare();
	const auto knots = spline.Knots();
	RequireNear(knots[0], 0, 0, "actual normalized first knot");
	RequireNear(knots[1], 0.5f, 0, "actual joined rest lengths determine middle knot");
	RequireNear(knots[2], 1, 0, "actual normalized last knot");
	// Actual model points approach (0,-1),(0,0),(-1,0); its natural spline has M1=(-6,-6).
	const auto first_quarter = spline.Sample(0.25f);
	RequireNear(first_quarter[0], 0.09375f, 1e-6f, "actual natural cubic bends away from linear X");
	RequireNear(first_quarter[1], -0.40625f, 1e-6f, "actual natural cubic first interval Y");
	const auto third_quarter = spline.Sample(0.75f);
	RequireNear(third_quarter[0], -0.40625f, 1e-6f, "actual natural cubic second interval X");
	RequireNear(third_quarter[1], 0.09375f, 1e-6f, "actual natural cubic bends away from linear Y");
	const auto middle = spline.Sample(0.5f);
	RequireNear(middle[0], 0, 0, "exact actual knot keeps shared anchor X");
	RequireNear(middle[1], 0, 0, "exact actual knot keeps shared anchor Y");
	posture.Coordinate = {5, 6, 0};
	model.SetPosture(posture);
	model.Initialize();
	RequireNear(spline.Sample(0.25f)[0], first_quarter[0], 0, "sampler retains real previous preparation until next output phase");
	spline.Prepare();
	RequireNear(spline.Sample(0.25f)[0], first_quarter[0] + 5, 1e-6f, "next preparation reads actual moved physical points");
	RequireNear(spline.Sample(0.25f)[1], first_quarter[1] + 6, 1e-6f, "spline never injects a replacement pose");
}

static void TestActualForceThenPhysicalSampleAndBoundaryClamp() {
	sgre::scene::EmoteTressDefinition definition;
	definition.Gravity = 1;
	definition.Second.Mass = 1;
	definition.Second.Length = 2;
	definition.Second.ParticleCount = 2;
	sgre::scene::EmoteTressModel model(definition);
	sgre::scene::EmoteTressSpline spline(model);
	sgre::scene::EmotePhysicalPosture posture;
	model.SetPosture(posture);
	model.Initialize();
	spline.Prepare();
	RequireNear(spline.Sample(0.25f)[1], 0.5f, 1e-6f, "native two-point spline is linear without fabricated curvature");
	RequireNear(spline.Sample(-2)[1], 0, 0, "native sample clamps before first knot");
	RequireNear(spline.Sample(4)[1], 2, 1e-6f, "native sample clamps after final knot");
	const auto previous = spline.Sample(1);
	model.ClearForcesAndMeasureBounds();
	model.AddForce(false, 1, {4, 0, 0});
	model.Step(0.1f);
	RequireNear(spline.Sample(1)[0], previous[0], 0, "physical Step does not bypass native output preparation");
	spline.Prepare();
	if (!(spline.Sample(1)[0] > previous[0])) {
		throw std::runtime_error("prepared spline must consume the actual force-integrated physical endpoint");
	}
	const auto captured = spline.Sample(0.33f);
	spline.Prepare();
	if (spline.Sample(0.33f) != captured) {
		throw std::runtime_error("unchanged actual particle input must preserve repeated spline preparation");
	}
	RequireThrows([&] {
		static_cast<void>(spline.Sample(std::numeric_limits<float>::quiet_NaN()));
	},
	    "finite position");
}

static void TestNonuniformNativeRestKnots() {
	sgre::scene::EmoteTressDefinition definition;
	definition.Gravity = 1;
	definition.Second.Mass = 1;
	definition.Second.Length = 2;
	definition.Second.ParticleCount = 3;
	definition.Second.TrailingLength = 4;
	definition.Second.TrailingPoints = 1;
	sgre::scene::EmoteTressModel model(definition);
	sgre::scene::EmoteTressSpline spline(model);
	const auto knots = spline.Knots();
	RequireNear(knots[0], 0, 0, "nonuniform rest first knot");
	RequireNear(knots[1], 1.0f / 6.0f, 0, "native float prefix first actual segment");
	RequireNear(knots[2], 2.0f / 6.0f, 0, "native float prefix second actual segment");
	RequireNear(knots[3], 1, 0, "native trailing rest length determines final interval");
	sgre::scene::EmotePhysicalPosture posture;
	model.SetPosture(posture);
	model.Initialize();
	spline.Prepare();
	for (std::size_t index = 0; index != knots.size(); ++index) {
		const std::array<float, 4> expected{0, 1, 2, 6};
		RequireNear(spline.Sample(knots[index])[1], expected[index], 1e-6f, "spline passes each actual physical or trailing point");
	}
	definition.RigidSide = sgre::scene::EmoteTressRigidSide::Second;
	definition.Second.Length = 0;
	definition.Second.TrailingPoints = 0;
	sgre::scene::EmoteTressModel zero_length(definition);
	RequireThrows([&] {
		sgre::scene::EmoteTressSpline invalid(zero_length);
	},
	    "finite positive actual rest length");
}

int main() {
	try {
		TestAnalyticCornerFromRealRigidAndFreeBranches();
		TestActualForceThenPhysicalSampleAndBoundaryClamp();
		TestNonuniformNativeRestKnots();
		std::cout << "Emote String spline: actual physical producer, nonuniform rest knots, analytic cubic bend, retained preparation and force-integrated sample passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
