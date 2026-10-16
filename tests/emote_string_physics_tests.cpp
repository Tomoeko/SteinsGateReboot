#include "sgre/scene/emote_string_physics.hpp"

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

static void TestMetadataToActualPhysicalSamples() {
	auto definition = Definition();
	Require(definition.Uid == "physical-string" && definition.LongitudinalDegree == 4 && definition.Substeps == 1 && definition.Speed == 1, "native UID C-string boundary and optional defaults must remain actual metadata inputs");
	sgre::scene::EmoteStringPhysics owner(std::move(definition));
	Require(owner.TressCount() == 1 && owner.Uid() == "physical-string" && owner.EncodedDegree() == 0x43 && !owner.HorizontalBones(), "real String must own its actual tress and physical lattice orientation");
	const auto samples = owner.PreparePhysicalSamples();
	Require(samples.size() == 20, "actual transverse cubic and longitudinal quartic require twenty physical samples");
	for (std::size_t row = 0; row != 5; ++row) {
		for (std::size_t column = 0; column != 4; ++column) {
			const auto& point = samples[row * 4 + column];
			RequireNear(point[0], static_cast<float>(column) * (2.0f / 3.0f) - 0.5f, 0.00001f, "noncentral horizontal origin is subtracted on ordinary vertical bones");
			RequireNear(point[1], static_cast<float>(row) - 3.0f, 0.00001f, "real two-sided physical initialization preserves authored vertical origin");
		}
	}
	const std::array<float, 5> uniform{0, 0.25f, 0.5f, 0.75f, 1};
	Require(std::equal(owner.LongitudinalKnots().begin(), owner.LongitudinalKnots().end(), uniform.begin()), "central actual fix yields native uniform longitudinal knots");
	const std::array<float, 2> previous_end{(samples[17][0] + samples[18][0]) * 0.5f, (samples[17][1] + samples[18][1]) * 0.5f};
	owner.ClearForcesAndMeasureBounds();
	owner.AddForce(0, false, 2, {20, 0, 0});
	owner.Update(1);
	const auto moved = owner.PreparePhysicalSamples();
	Require((moved[17][0] + moved[18][0]) * 0.5f > previous_end[0], "actual force-integrated second-side endpoint must reach String physical output");
	RequireNear((moved[9][0] + moved[10][0]) * 0.5f, 0.5f, 0.00001f, "real joined fix remains fixed while free endpoint moves");
	RequireNear((moved[9][1] + moved[10][1]) * 0.5f, -1, 0.00001f, "String sampling must not replace the genuine fixed point");
}

static void TestCustomPivotAndHorizontalBoneDomain() {
	auto definition = Definition();
	definition.SplitLeft = 0.2f;
	definition.SplitRight = 0.2f;
	sgre::scene::EmoteStringPhysics custom(definition);
	const auto knots = custom.LongitudinalKnots();
	const std::array<float, 5> expected{0, 0.2f, (0.8f / 3.0f) + 0.2f, (1.6f / 3.0f) + 0.2f, 1};
	const auto samples = custom.PreparePhysicalSamples();
	for (std::size_t row = 0; row != knots.size(); ++row) {
		RequireNear(knots[row], expected[row], 0, "native split-aware basis knots must retain authored noncentral pivot");
		RequireNear(samples[row * 4][1], expected[row] * 4.0f - 3.0f, 0.00001f, "actual physical spline sampled at retained nonuniform longitudinal knots");
	}
	definition = Definition();
	definition.HorizontalBones = true;
	sgre::scene::EmoteStringPhysics horizontal(definition);
	Require(horizontal.HorizontalBones() && horizontal.EncodedDegree() == 0x34, "horizontal native bone changes final lattice degree, not intermediate point count");
	const auto horizontal_samples = horizontal.PreparePhysicalSamples();
	for (std::size_t row = 0; row != 5; ++row) {
		for (std::size_t column = 0; column != 4; ++column) {
			RequireNear(horizontal_samples[row * 4 + column][0], static_cast<float>(row) * 0.5f - 0.5f, 0.00001f, "horizontal actual physical longitudinal axis and origin");
			RequireNear(horizontal_samples[row * 4 + column][1], 1.0f - static_cast<float>(column) * (4.0f / 3.0f), 0.00001f, "horizontal bone perpendicular uses actual coordinate rotation before final inverse-grid transpose");
		}
	}
}

static void TestActualPostureSubstepsAndSerializedTressWeights() {
	auto definition = Definition();
	definition.Substeps = 4;
	sgre::scene::EmoteStringPhysics owner(definition);
	sgre::scene::EmotePhysicalPosture posture;
	posture.Coordinate = {3, 5, 0};
	owner.SetPosture(posture);
	owner.ClearForcesAndMeasureBounds();
	owner.Update(3);
	const auto samples = owner.PreparePhysicalSamples();
	RequireNear((samples[9][0] + samples[10][0]) * 0.5f, 3.5f, 0.00001f, "actual interpolated root posture reaches the joined fixed point");
	RequireNear((samples[9][1] + samples[10][1]) * 0.5f, 4, 0.00001f, "substeps preserve noncentral authored anchor rather than resetting physical geometry");
	RequireThrows([&] {
		owner.AddForce(1, false, 0, {1, 0, 0});
	},
	    "actual tress owner");
	definition = Definition();
	definition.Tresses[0].TopX = 0.75f;
	definition.Tresses[0].BottomX = 0.75f;
	definition.Tresses.push_back({0.25f, 0, 0.25f, 1});
	sgre::scene::EmoteStringPhysics two(definition);
	Require(two.TressCount() == 2, "both real serialized tresses must be owned even when spatial sort reverses them");
	const auto weighted = two.PreparePhysicalSamples();
	for (std::size_t row = 0; row != 5; ++row) {
		for (std::size_t column = 0; column != 4; ++column) {
			RequireNear(weighted[row * 4 + column][0], static_cast<float>(column) * (2.0f / 3.0f) - 0.5f, 0.00001f, "actual spatial neighbor weighting must reproduce affine rest geometry without reordering owners");
		}
	}
}

static void TestMetadataSelectsActualAdhocIntegration() {
	auto control = StringControl();
	Set(control, "adhocSim", Atom(true));
	Set(control, "elastic", Atom(1.0));
	auto definitions = sgre::scene::ReadEmoteStringPhysicsDefinitions(Document(control));
	Require(definitions.front().AdhocSimulation, "actual String metadata must retain its true solver selector");
	sgre::scene::EmoteStringPhysics adhoc(definitions.front());
	definitions.front().AdhocSimulation = false;
	sgre::scene::EmoteStringPhysics compliant(definitions.front());
	for (auto* owner : {&adhoc, &compliant}) {
		owner->ClearForcesAndMeasureBounds();
		owner->AddForce(0, false, 2, {0, 1000, 0});
		owner->Update(1);
	}
	const auto adhoc_samples = adhoc.PreparePhysicalSamples();
	const auto compliant_samples = compliant.PreparePhysicalSamples();
	Require(adhoc_samples.size() == 20 && compliant_samples.size() == 20, "both genuine String solvers produce their owned physical lattice");
	const auto adhoc_bottom = (adhoc_samples[17][1] + adhoc_samples[18][1]) * 0.5f;
	const auto compliant_bottom = (compliant_samples[17][1] + compliant_samples[18][1]) * 0.5f;
	Require(std::isfinite(adhoc_bottom) && std::isfinite(compliant_bottom) && compliant_bottom > adhoc_bottom + 0.00001f, "parsed adhocSim must reach real owned tress solvers rather than only opening the constructor gate");
}

static void TestMetadataConversionsAndExplicitUnsupportedDomain() {
	auto control = StringControl();
	Set(control, "autoMass", Atom(true));
	Set(control, "imageArea", Atom(12.0));
	Set(control, "massPerSquareMeter", Atom(2.0));
	Set(control, "defaultRotAsGravity", Atom(true));
	Set(control, "defaultRot", Atom(6.283185482025146484375));
	const auto definitions = sgre::scene::ReadEmoteStringPhysicsDefinitions(Document(control));
	RequireNear(definitions[0].Mass, 6, 0, "automatic physical mass must use each control's ppm rather than Common ppm");
	RequireNear(definitions[0].DefaultRotation, 0, 0.000001f, "native default gravity rotation normalizes through actual float pi constants");
	sgre::resource::PsbDocument absent;
	absent.Root = Atom(sgre::resource::PsbObject{{"metadata", Atom(sgre::resource::PsbObject{})}});
	Require(sgre::scene::ReadEmoteStringPhysicsDefinitions(absent).empty(), "absent native Velvety Common has no String owner");
	Set(std::get<sgre::resource::PsbObject>(absent.Root.Data)[0].second, "velvetyCommonParams", Atom(false));
	RequireThrows([&] {
		static_cast<void>(sgre::scene::ReadEmoteStringPhysicsDefinitions(absent));
	},
	    "missing velvetyStringControl");
	auto definition = Definition();
	definition.Mass = 0;
	RequireThrows([&] {
		sgre::scene::EmoteStringPhysics invalid(definition);
	},
	    "geometry, mass and substeps");
	definition = Definition();
	definition.Tresses[0].BottomX = std::numeric_limits<float>::infinity();
	RequireThrows([&] {
		sgre::scene::EmoteStringPhysics invalid(definition);
	},
	    "finite actual endpoints");
	sgre::scene::EmoteStringPhysics owner(Definition());
	RequireThrows([&] {
		owner.Update(0);
	},
	    "positive actual scaled ticks");
	RequireThrows([&] {
		owner.Update(std::numeric_limits<float>::quiet_NaN());
	},
	    "positive actual scaled ticks");
	static_cast<void>(owner.PreparePhysicalSamples());
}

int main() {
	try {
		TestMetadataToActualPhysicalSamples();
		TestCustomPivotAndHorizontalBoneDomain();
		TestActualPostureSubstepsAndSerializedTressWeights();
		TestMetadataSelectsActualAdhocIntegration();
		TestMetadataConversionsAndExplicitUnsupportedDomain();
		std::cout << "Emote String: actual metadata, owned physical tresses, native substeps, spline samples, origin, nonuniform knots and force propagation passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
