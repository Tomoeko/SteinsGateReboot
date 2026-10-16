#include "sgre/scene/emote_string_mesh_output.hpp"

#include "sgre/scene/motion_bezier_lattice.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

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
	throw std::runtime_error("expected actual String physical-domain rejection");
}

template <class Value>
static sgre::resource::PsbValue Atom(Value value) {
	sgre::resource::PsbValue result;
	result.Data = std::move(value);
	return result;
}

static sgre::resource::PsbValue VerticalLine(double x) {
	return Atom(sgre::resource::PsbObject{{"topXRate", Atom(x)}, {"topYRate", Atom(0.0)}, {"bottomXRate", Atom(x)}, {"bottomYRate", Atom(1.0)}});
}

static sgre::resource::PsbValue StringControl() {
	return Atom(sgre::resource::PsbObject{{"uid", Atom(std::string("physical-string\0ignored", 23))}, {"ppm", Atom(2.0)}, {"boneDirection", Atom(std::int64_t{0})}, {"gravity", Atom(1.0)}, {"roughness", Atom(0.0)}, {"rigidMode", Atom(std::int64_t{0})}, {"sizeH", Atom(2.0)}, {"sizeV", Atom(4.0)}, {"originX", Atom(0.25)}, {"originY", Atom(0.75)}, {"splitLeft", Atom(0.5)}, {"splitRight", Atom(0.5)}, {"mass", Atom(1.0)}, {"numPerMeter", Atom(1.0)}, {"frictionTop", Atom(0.0)}, {"frictionBottom", Atom(0.0)}, {"elastic", Atom(0.0)}, {"straightness", Atom(0.0)}, {"coordScaleH", Atom(1.0)}, {"coordScaleV", Atom(1.0)}, {"coordScaleD", Atom(0.0)}, {"compressWiden", Atom(0.0)}, {"controlPointSize", Atom(0.0)}, {"adhocSim", Atom(false)}, {"tresses", Atom(sgre::resource::PsbArray{VerticalLine(0.5)})}});
}

static sgre::resource::PsbDocument Document(sgre::resource::PsbValue control) {
	sgre::resource::PsbDocument document;
	document.Root = Atom(sgre::resource::PsbObject{{"metadata", Atom(sgre::resource::PsbObject{{"velvetyCommonParams", Atom(sgre::resource::PsbObject{{"ppm", Atom(200.0)}})}, {"velvetyStringControl", Atom(sgre::resource::PsbArray{std::move(control)})}})}});
	return document;
}

static void Set(sgre::resource::PsbValue& object, std::string name, sgre::resource::PsbValue value) {
	for (auto& [key, field] : std::get<sgre::resource::PsbObject>(object.Data)) {
		if (key == name) {
			field = std::move(value);
			return;
		}
	}
	std::get<sgre::resource::PsbObject>(object.Data).emplace_back(std::move(name), std::move(value));
}

static sgre::scene::EmoteStringDefinition Definition() {
	const auto document = Document(StringControl());
	auto definitions = sgre::scene::ReadEmoteStringPhysicsDefinitions(document);
	Require(definitions.size() == 1, "actual String metadata must create one owned definition");
	return std::move(definitions.front());
}

static sgre::scene::EmoteStringMeshOutputDefinition OutputDefinition() {
	auto control = StringControl();
	Set(control, "rigidRange", Atom(sgre::resource::PsbObject{{"l", Atom(0.0)}, {"t", Atom(0.0)}, {"r", Atom(1.0)}, {"b", Atom(1.0)}}));
	Set(control, "rigidIter", Atom(std::int64_t{2}));
	Set(control, "fixes", Atom(sgre::resource::PsbArray{}));
	const auto definitions = sgre::scene::ReadEmoteStringMeshOutputDefinitions(Document(control));
	Require(definitions.size() == 1 && definitions.front().RigidIterations == 2, "actual output metadata preserves native control order and rigid iteration input");
	return definitions.front();
}

static void RequireIdentity(const sgre::resource::MotionBezierLattice& actual) {
	const auto identity = sgre::scene::IdentityMotionBezierLattice(actual.EncodedDegree);
	Require(actual.Controls.size() == identity.Controls.size(), "native corrected output preserves final lattice control count");
	for (std::size_t index = 0; index != identity.Controls.size(); ++index) {
		RequireNear(actual.Controls[index].X, identity.Controls[index].X, 0.000003f, "constructor identity-minus-initial-output preserves authored initial X lattice");
		RequireNear(actual.Controls[index].Y, identity.Controls[index].Y, 0.000003f, "constructor baseline preserves authored initial Y lattice");
	}
}

static void TestActualPhysicalSamplesBecomeCorrectedLattice() {
	for (const auto horizontal : {false, true}) {
		for (const auto pivot : {0.5f, 0.2f}) {
			auto definition = Definition();
			definition.HorizontalBones = horizontal;
			definition.SplitLeft = pivot;
			definition.SplitRight = pivot;
			sgre::scene::EmoteStringPhysics physics(definition);
			sgre::scene::EmoteStringMeshOutput output(physics, OutputDefinition());
			sgre::scene::EmotePhysicalPosture posture;
			const auto& initial = output.Prepare(posture, 0, 1, {});
			Require(initial.EncodedDegree == (horizontal ? 0x34 : 0x43), "native horizontal rotation must also retain its distinct final degree");
			RequireIdentity(initial);
			const auto before = initial.Controls;
			physics.ClearForcesAndMeasureBounds();
			// Actual branch point count is supplied by this authored vertical length/density and fix, not injected particle state.
			const auto particle = horizontal ? std::size_t{1} : pivot == 0.5f ? std::size_t{2}
			                                                                  : std::size_t{3};
			const std::array<float, 3> force = horizontal ? std::array<float, 3>{0, 20, 0} : std::array<float, 3>{20, 0, 0};
			physics.AddForce(0, false, particle, force);
			physics.Update(1);
			const auto& moved = output.Prepare(posture, 0, 1, {});
			float maximum_delta = 0;
			for (std::size_t index = 0; index != before.size(); ++index) {
				maximum_delta = std::max(maximum_delta, std::max(std::abs(moved.Controls[index].X - before[index].X), std::abs(moved.Controls[index].Y - before[index].Y)));
			}
			Require(maximum_delta > 0.00001f, "real transverse force integration reaches converted output; horizontal=" + std::to_string(horizontal) + ", pivot=" + std::to_string(pivot) + ", maximum delta=" + std::to_string(maximum_delta));
		}
	}
}

static void TestTranslatedRotatedActualReactivationNormalization() {
	auto definition = Definition();
	definition.Straightness = 1;
	sgre::scene::EmoteStringPhysics physics(definition);
	sgre::scene::EmoteStringMeshOutput output(physics, OutputDefinition());
	sgre::scene::EmotePhysicalPosture posture;
	RequireIdentity(output.Prepare(posture, 0, 1, {}));
	posture.Coordinate = {3, 7, 0};
	posture.Rotation = 1.57079637050628662109375f;
	posture.Cosine = std::cos(posture.Rotation);
	posture.Sine = std::sin(posture.Rotation);
	physics.SetPosture(posture);
	// Native String reactivation resets actual particles at their new six-word posture before physical/output phases.
	physics.Initialize();
	RequireIdentity(output.Prepare(posture, 0, 1, {}));
}

static void TestActualWorldFixTargetsAndIndependentBaseline() {
	auto definition = Definition();
	sgre::scene::EmoteStringPhysics physics(definition);
	auto configuration = OutputDefinition();
	configuration.Fixes = {{0.25f, 0.75f}};
	configuration.CorrectFix = true;
	configuration.CorrectScale = true;
	sgre::scene::EmoteStringMeshOutput output(physics, configuration);
	sgre::scene::EmotePhysicalPosture posture;
	const std::array<sgre::resource::MotionPoint, 1> first_target{{{0, 0}}};
	const auto first = output.Prepare(posture, 0, 1, first_target).Controls;
	const std::array<sgre::resource::MotionPoint, 1> second_target{{{2, 4}}};
	const auto& second = output.Prepare(posture, 0, 1, second_target);
	for (std::size_t index = 0; index != first.size(); ++index) {
		RequireNear(second.Controls[index].X - first[index].X, 1, 0.000003f, "actual world fix delta converts through original width instead of being mistaken for a normalized point");
		RequireNear(second.Controls[index].Y - first[index].Y, 1, 0.000003f, "actual world fix delta converts through original height");
	}
	const auto scaled = output.Prepare(posture, 0, 2, first_target).Controls;
	Require(std::abs(scaled.front().X - first.front().X) > 0.01f, "native scale correction changes actual nonfix controls");
	const auto unscaled_fix = sgre::scene::EvaluateMotionBezierLattice({second.EncodedDegree, first}, configuration.Fixes.front());
	const auto scaled_fix = sgre::scene::EvaluateMotionBezierLattice({second.EncodedDegree, scaled}, configuration.Fixes.front());
	RequireNear(scaled_fix.X, unscaled_fix.X, 0.000005f, "native one-point alignment keeps actual fix location while correcting scale");
	RequireNear(scaled_fix.Y, unscaled_fix.Y, 0.000005f, "scale correction does not displace the one-point anchor");
}

static void TestExplicitRemainingConstraintDomains() {
	sgre::scene::EmoteStringPhysics physics(Definition());
	auto definition = OutputDefinition();
	definition.RigidRange[0] = 0.1f;
	sgre::scene::EmoteStringMeshOutput rigid_output(physics, definition);
	sgre::scene::EmotePhysicalPosture initial;
	RequireIdentity(rigid_output.Prepare(initial, 0, 1, {}));
	definition = OutputDefinition();
	definition.Fixes = {{0, 0}, {1, 1}};
	sgre::scene::EmoteStringMeshOutput two_fix_output(physics, definition);
	RequireIdentity(two_fix_output.Prepare(initial, 0, 1, {}));
	definition.Fixes.push_back({0.2f, 0.7f});
	definition.CorrectFix = true;
	sgre::scene::EmoteStringMeshOutput three_fix_output(physics, definition);
	std::vector<sgre::resource::MotionPoint> targets;
	for (const auto& fix : definition.Fixes) {
		targets.push_back({(fix.X - physics.Definition().OriginX) * physics.Definition().SizeHorizontal, (fix.Y - physics.Definition().OriginY) * physics.Definition().SizeVertical});
	}
	RequireIdentity(three_fix_output.Prepare(initial, 0, 1, targets));
	definition.Fixes.resize(1);
	definition.CorrectFix = true;
	sgre::scene::EmoteStringMeshOutput output(physics, definition);
	sgre::scene::EmotePhysicalPosture posture;
	RequireThrows([&] {
		static_cast<void>(output.Prepare(posture, 0, 1, {}));
	},
	    "actual transformed world target");
	RequireThrows([&] {
		static_cast<void>(output.Prepare(posture, 0, 1, {}));
	},
	    "invalid after a failed");
}

int main() {
	try {
		TestActualPhysicalSamplesBecomeCorrectedLattice();
		TestTranslatedRotatedActualReactivationNormalization();
		TestActualWorldFixTargetsAndIndependentBaseline();
		TestExplicitRemainingConstraintDomains();
		std::cout << "Actual String physical owner to sampled inverse, horizontal rotation, initial baseline and zero/one-point world correction passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
