#include "sgre/scene/emote_jelly_definition.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <class Value>
static sgre::resource::PsbValue Atom(Value value) {
	sgre::resource::PsbValue result;
	result.Data = std::move(value);
	return result;
}

static void Set(sgre::resource::PsbValue& object, std::string name, sgre::resource::PsbValue value) {
	auto& fields = std::get<sgre::resource::PsbObject>(object.Data);
	for (auto& [key, field] : fields) {
		if (key == name) {
			field = std::move(value);
			return;
		}
	}
	fields.emplace_back(std::move(name), std::move(value));
}

static void Erase(sgre::resource::PsbValue& object, std::string_view name) {
	auto& fields = std::get<sgre::resource::PsbObject>(object.Data);
	std::erase_if(fields, [name](const auto& field) {
		return field.first == name;
	});
}

template <class Operation>
static void RequireFailure(Operation operation, std::string_view part) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(part) != std::string_view::npos, error.what());
		return;
	}
	throw std::runtime_error("missing expected actual Jelly metadata rejection");
}

static sgre::resource::PsbValue Vector3(double x, double y, double z) {
	return Atom(sgre::resource::PsbObject{{"x", Atom(x)}, {"y", Atom(y)}, {"z", Atom(z)}});
}

static sgre::resource::PsbValue Control() {
	sgre::resource::PsbObject fields;
	fields.emplace_back("uid", Atom(std::string("actual-jelly")));
	fields.emplace_back("ppm", Atom(100.0));
	fields.emplace_back("density", Atom(1.0));
	fields.emplace_back("sizeH", Atom(2.0));
	fields.emplace_back("sizeV", Atom(3.0));
	fields.emplace_back("num", Atom(std::int64_t{3}));
	fields.emplace_back("heightRate", Atom(0.5));
	fields.emplace_back("friction", Atom(0.1));
	fields.emplace_back("elasticH", Atom(2.0));
	fields.emplace_back("elasticV", Atom(99.0));
	fields.emplace_back("coordScaleH", Atom(1.0));
	fields.emplace_back("coordScaleV", Atom(1.0));
	fields.emplace_back("coordScaleD", Atom(1.0));
	fields.emplace_back("tipTilt", Vector3(0, 0, 1));
	fields.emplace_back("rigidRange", Atom(sgre::resource::PsbObject{{"l", Atom(0.0)}, {"t", Atom(0.0)}, {"r", Atom(1.0)}, {"b", Atom(1.0)}}));
	fields.emplace_back("fixes", Atom(sgre::resource::PsbArray{}));
	fields.emplace_back("tresses", Atom(sgre::resource::PsbArray{Atom(sgre::resource::PsbObject{{"x", Atom(0.5)}, {"y", Atom(0.5)}, {"r", Atom(0.25)}, {"jellyTilt", Vector3(0, 0, 1)}})}));
	fields.emplace_back("speedVariables", Atom(sgre::resource::PsbArray{}));
	fields.emplace_back("collisionRefs", Atom(sgre::resource::PsbArray{}));
	fields.emplace_back("emissionRefs", Atom(sgre::resource::PsbArray{}));
	return Atom(std::move(fields));
}

static sgre::scene::EmoteJellyControlDefinition Read(sgre::resource::PsbValue control) {
	sgre::resource::PsbDocument document;
	document.Root = Atom(sgre::resource::PsbObject{{"metadata", Atom(sgre::resource::PsbObject{{"velvetyCommonParams", Atom(sgre::resource::PsbObject{})}, {"velvetyJellyControl", Atom(sgre::resource::PsbArray{std::move(control)})}})}});
	auto definitions = sgre::scene::ReadEmoteJellyControlDefinitions(document);
	Require(definitions.size() == 1, "actual serialized list must yield exactly one owned definition");
	return std::move(definitions.front());
}

static void TestNativeDefaultsAndRealPhysicalOwner() {
	const auto definition = Read(Control());
	const auto& physics = definition.Physics;
	Require(physics.Uid == "actual-jelly" && definition.PixelsPerMeter == 100, "parser keeps actual UID and separate coordinate units");
	Require(physics.SizeHorizontal == 2 && physics.SizeVertical == 3, "native reader preserves physical dimensions rather than dividing them by ppm");
	Require(physics.Tresses.front().Coordinate.X == 0.5f && physics.Tresses.front().Radius == 0.25f, "native tress coordinates/radius remain normalized input values");
	Require(physics.ParticleCount == 3 && physics.Substeps == 1 && physics.Speed == 1 && physics.Gravity == 9.8f, "native num/default count and timing values survive parsing");
	Require(physics.AutoMass && physics.Mass == 0 && physics.AdhocSimulation && physics.SampleDensity == 2 && physics.LimitLengthRatio == 2 && physics.ReverseSampleOrder, "native missing-option branches retain exact physical defaults");
	Require(physics.ElasticHorizontal == 2 && physics.ElasticVertical == 2, "both native spring fields read elasticH even when elasticV differs");
	Require(physics.DefaultRotation == 0 && !physics.CorrectFix && !physics.CorrectScale && definition.InheritShape, "native rotation and inheritShape default suppress separate correction fields");
	sgre::scene::EmoteJellyPhysics owner(physics);
	sgre::scene::EmotePhysicalPosture posture;
	owner.SetPosture(posture);
	owner.Initialize();
	Require(owner.TressCount() == 1 && owner.Particles(0).size() == 3 && !owner.InitialRings(0).empty(), "real parsed metadata constructs actual particle/ring owners without substitute state");
	Require(owner.Definition().SizeHorizontal == 2 && owner.Origin().X == 0.5f && owner.Origin().Y == 0.5f, "actual physical owner derives its own native origin from retained fixes");
}

static void TestNativeConditionalInputsAndOwnedReferences() {
	auto control = Control();
	Set(control, "autoMass", Atom(true));
	Set(control, "mass", Atom(4.0));
	Set(control, "defaultRotAsGravity", Atom(true));
	Set(control, "defaultRot", Atom(0.7));
	Set(control, "sampleOrder", Atom(std::int64_t{-7}));
	Set(control, "num", Atom(3.999999999));
	Set(control, "correctFix", Atom(true));
	Set(control, "correctScale", Atom(true));
	Set(control, "speedVariables", Atom(sgre::resource::PsbArray{Atom(std::string("speed\0ignored", 13))}));
	Set(control, "collisionRefs", Atom(sgre::resource::PsbArray{Atom(sgre::resource::PsbObject{{"uid", Atom(std::string("collider"))}, {"ignoreMask", Atom(std::int64_t{-1})}})}));
	Set(control, "emissionRefs", Atom(sgre::resource::PsbArray{Atom(std::monostate{})}));
	auto definition = Read(control);
	Require(definition.Physics.Mass == 4 && definition.Physics.DefaultRotation == 0.7f && definition.Physics.ParticleCount == 3, "present native metadata uses direct scalar/integer conversion");
	Require(definition.Physics.ReverseSampleOrder && !definition.Physics.CorrectFix && !definition.Physics.CorrectScale, "nonzero sampleOrder stays reverse and inheritShape suppresses explicit correction flags");
	Require(definition.SpeedVariables == std::vector<std::string>{"speed"} && definition.CollisionReferences.front().Uid == "collider" && definition.CollisionReferences.front().IgnoreMask == 0xffffffffu && definition.RequiresEmissions, "owned native names/masks preserve real populated requirements after document retirement");
	Set(control, "inheritShape", Atom(false));
	Set(control, "sampleOrder", Atom(std::int64_t{0}));
	definition = Read(control);
	Require(definition.Physics.CorrectFix && definition.Physics.CorrectScale && !definition.Physics.ReverseSampleOrder, "only native non-inherited branch reads both independent correction options");
	Erase(control, "mass");
	RequireFailure([&] {
		static_cast<void>(Read(control));
	},
	    "actual field mass");
}

static void TestExactNamesAndSafetyDomain() {
	auto control = Control();
	Erase(control, "num");
	Set(control, "numParticles", Atom(std::int64_t{3}));
	RequireFailure([&] {
		static_cast<void>(Read(control));
	},
	    "actual field num");
	control = Control();
	Set(control, "coordPred", Atom(std::int64_t{4}));
	RequireFailure([&] {
		static_cast<void>(Read(control));
	},
	    "counts/prediction");
	control = Control();
	Set(control, "ppm", Atom(std::numeric_limits<double>::infinity()));
	RequireFailure([&] {
		static_cast<void>(Read(control));
	},
	    "finite supported domain");
	sgre::resource::PsbDocument absent;
	absent.Root = Atom(sgre::resource::PsbObject{{"metadata", Atom(sgre::resource::PsbObject{})}});
	Require(sgre::scene::ReadEmoteJellyControlDefinitions(absent).empty(), "native absent Common does not parse an invented physical family");
}

int main() {
	try {
		TestNativeDefaultsAndRealPhysicalOwner();
		TestNativeConditionalInputsAndOwnedReferences();
		TestExactNamesAndSafetyDomain();
		std::cout << "Actual Jelly metadata: native defaults/units/conditional flags, owned references and connected physical construction passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
