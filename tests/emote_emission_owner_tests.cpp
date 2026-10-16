#include "sgre/scene/emote_emission_owner.hpp"
#include "sgre/scene/emote_jelly_control.hpp"
#include "sgre/scene/emote_string_control.hpp"

#include "sgre/scene/emote_variable_controls.hpp"
#include "sgre/scene/emote_velvety_frame.hpp"
#include "sgre/scene/motion_bezier_lattice.hpp"

#include "sgre/scene/motion_assets.hpp"
#include "sgre/scene/motion_draw.hpp"
#include "sgre/scene/motion_grid_draw.hpp"
#include "sgre/scene/motion_layer_mesh_local_owner.hpp"
#include "sgre/scene/motion_player_task.hpp"
#include "sgre/scene/motion_scene_draw_context.hpp"
#include "sgre/scene/motion_scene_evaluation.hpp"
#include "sgre/scene/motion_scene_player.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
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

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view message) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(message) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("mesh override accepted an invalid actual owner or lattice");
}

template <typename Value>
static sgre::resource::PsbValue Atom(Value value) {
	sgre::resource::PsbValue result;
	result.Data = std::move(value);

	return result;
}

static sgre::resource::PsbValue Layer(bool mesh) {
	sgre::resource::PsbArray frames;
	for (std::size_t index = 0; index != 4; ++index) {
		auto content = sgre::resource::PsbObject{{"mask", Atom(mesh ? std::int64_t{0x02020613} : std::int64_t{0x20603})}, {"src", Atom(std::string("atlas"))}, {"icon", Atom(std::string("tile"))}, {"coord", Atom(sgre::resource::PsbArray{Atom(mesh ? 0.0 : 2.0), Atom(0.0), Atom(0.0)})}, {"ox", Atom(0.0)}, {"oy", Atom(0.0)}, {"opa", Atom(std::int64_t{255})}, {"color", Atom(std::int64_t{0x808080ff})}, {"bm", Atom(std::int64_t{0x10})}};
		if (mesh) {
			content.emplace_back("angle", Atom(90.0));
			const auto identity = sgre::scene::IdentityMotionBezierPatch();
			sgre::resource::PsbArray coordinates;
			for (const auto& point : identity.Controls) {
				coordinates.push_back(Atom(static_cast<double>(point.X + (index == 1 ? 0.25f : 0.0f))));
				coordinates.push_back(Atom(static_cast<double>(point.Y)));
			}
			content.emplace_back("mesh", Atom(sgre::resource::PsbObject{{"cc", Atom(std::monostate{})}, {"bp", Atom(std::move(coordinates))}}));
		}
		frames.push_back(Atom(sgre::resource::PsbObject{{"time", Atom(static_cast<double>(index * 4))}, {"type", Atom(index == 0 ? std::int64_t{3} : index == 1 ? std::int64_t{2}
		                                                                                                                                                        : std::int64_t{0})},
		    {"content", Atom(std::move(content))}}));
	}
	const auto children = mesh ? sgre::resource::PsbArray{Layer(false)} : sgre::resource::PsbArray{};

	return Atom(sgre::resource::PsbObject{{"label", Atom(std::string(mesh ? "mesh" : "inherited"))}, {"uid", Atom(std::string(mesh ? "mesh-uid" : "ordinary-uid"))}, {"type", Atom(std::int64_t{0})}, {"inheritMask", Atom(std::int64_t{0x02000004})}, {"coordinate", Atom(std::int64_t{0})}, {"frameList", Atom(std::move(frames))}, {"transformOrder", Atom(sgre::resource::PsbArray{Atom(std::int64_t{0}), Atom(std::int64_t{3}), Atom(std::int64_t{2}), Atom(std::int64_t{1})})}, {"children", Atom(children)}, {"stencilType", Atom(std::int64_t{0})}, {"meshTransform", Atom(mesh ? std::int64_t{1} : std::int64_t{0})}, {"meshCombine", Atom(false)}, {"meshSyncChildMask", Atom(mesh ? std::int64_t{8} : std::int64_t{0})}, {"meshDivision", Atom(std::int64_t{4})}, {"objTriPriority", Atom(std::int64_t{0})}, {"parameterize", Atom(std::monostate{})}, {"joinTarget", Atom(false)}, {"groundCorrection", Atom(false)}, {"motionIndependentLayerInherit", Atom(false)}});
}

static std::shared_ptr<sgre::resource::PsbDocument> Document() {
	const auto priority = Atom(sgre::resource::PsbObject{{"time", Atom(0.0)}, {"type", Atom(std::int64_t{1})}, {"content", Atom(sgre::resource::PsbArray{Atom(std::int64_t{0}), Atom(std::int64_t{1})})}});
	const auto terminal = Atom(sgre::resource::PsbObject{{"time", Atom(12.0)}, {"type", Atom(std::int64_t{1})}, {"content", Atom(std::monostate{})}});
	const auto timeline = Atom(sgre::resource::PsbObject{{"type", Atom(std::int64_t{0})}, {"bounds", Atom(sgre::resource::PsbObject{{"left", Atom(0.0)}, {"top", Atom(0.0)}, {"right", Atom(16.0)}, {"bottom", Atom(8.0)}})}, {"layerIndexMap", Atom(sgre::resource::PsbObject{{"move_LR", Atom(std::int64_t{0})}})}, {"lastTime", Atom(12.0)}, {"loopTime", Atom(-1.0)}, {"layer", Atom(sgre::resource::PsbArray{Layer(true)})}, {"priority", Atom(sgre::resource::PsbArray{priority, terminal})}, {"tag", Atom(sgre::resource::PsbArray{})}, {"parameter", Atom(std::monostate{})}, {"variable", Atom(std::monostate{})}, {"parameterize", Atom(std::monostate{})}});
	sgre::resource::PsbChunkReference chunk;
	auto texture = Atom(sgre::resource::PsbObject{{"type", Atom(std::string("A8"))}, {"width", Atom(std::int64_t{4})}, {"height", Atom(std::int64_t{4})}, {"truncated_width", Atom(std::int64_t{4})}, {"truncated_height", Atom(std::int64_t{4})}, {"pixel", Atom(chunk)}});
	texture.SerializedOffset = 64;
	const auto icon = Atom(sgre::resource::PsbObject{{"left", Atom(0.0)}, {"top", Atom(0.0)}, {"width", Atom(2.0)}, {"height", Atom(2.0)}, {"originX", Atom(0.0)}, {"originY", Atom(0.0)}, {"attr", Atom(std::int64_t{2})}});
	const auto source = Atom(sgre::resource::PsbObject{{"texture", texture}, {"icon", Atom(sgre::resource::PsbObject{{"tile", icon}})}});
	const auto object = Atom(sgre::resource::PsbObject{{"type", Atom(std::int64_t{0})}, {"motion", Atom(sgre::resource::PsbObject{{"main", timeline}})}});
	auto document = std::make_shared<sgre::resource::PsbDocument>();
	document->Header.Version = 4;
	document->Header.RootOffset = 44;
	document->Header.DataOffset = 128;
	document->SerializedSize = 144;
	document->Chunks = {{128, 16}};
	document->Root = Atom(sgre::resource::PsbObject{{"id", Atom(std::string("motion"))}, {"spec", Atom(std::string("win"))}, {"version", Atom(3.03)}, {"label", Atom(std::string("Mesh override owner fixture"))}, {"easing", Atom(sgre::resource::PsbArray{})}, {"source", Atom(sgre::resource::PsbObject{{"atlas", source}})}, {"object", Atom(sgre::resource::PsbObject{{"character", object}})}, {"sourceIconRenameMap", Atom(sgre::resource::PsbObject{})}});
	document->Root.SerializedOffset = 44;

	return document;
}

struct Fixture {
	std::size_t Uploads = 0;
	std::size_t Releases = 0;
	std::array<std::uint32_t, sgre::scene::kMotionSharedSeedWords> Seed{};
	sgre::scene::TextureCache Cache;
	sgre::scene::MotionAssetCatalog Catalog;
	std::shared_ptr<const sgre::scene::LoadedMotionAsset> Asset;
	sgre::scene::MotionResourceManager Manager;
	sgre::scene::MotionProjectId Project;
	sgre::core::FrameClock Clock;
	std::unique_ptr<sgre::scene::MotionManagerRuntime> Runtime;
	sgre::scene::MotionLayerTasks Tasks;

	explicit Fixture(std::shared_ptr<sgre::resource::PsbDocument> actual_document = Document()) : Cache([this](auto, const auto&) {
		                                                                                              return sgre::render::SpriteTextureHandle{static_cast<std::uint32_t>(++Uploads), 1, &Uploads};
	                                                                                              },
	                                                                                                  [this](auto) {
		                                                                                                  ++Releases;
	                                                                                                  }),
	                                                                                              Catalog(Cache), Manager(Cache), Clock(0), Tasks(8) {
		for (std::size_t index = 0; index != Seed.size(); ++index) {
			Seed[index] = static_cast<std::uint32_t>(index) * 0x9e3779b9u + 17;
		}
		Asset = Catalog.Prepare(actual_document, std::make_shared<const std::vector<std::uint8_t>>(144, 255));
		Project = Manager.Register(Asset->Identity, Asset->Model, *Asset->Backing);
		sgre::scene::MotionManagerRuntimeServices services;
		services.SceneAssets = &Catalog;
		services.ReadSourceAliases = [this](auto project) {
			Require(project == Project, "mesh aliases require their actual registration");

			return std::span<const sgre::scene::MotionSourceAlias>(Asset->Aliases);
		};
		services.ReadRegisteredAsset = [this](auto project) -> const sgre::scene::LoadedMotionAsset& {
			Require(project == Project, "mesh asset requires its actual registration");

			return *Asset;
		};
		Runtime = std::make_unique<sgre::scene::MotionManagerRuntime>(Manager, Clock, Seed, std::move(services), 32);
	}

	~Fixture() {
		Runtime->SceneContext().Clear();
		Tasks.Clear();
		Runtime.reset();
	}
};

static void RequireNear(float actual, float expected, std::string_view message) {
	if (!std::isfinite(actual) || std::abs(actual - expected) > 0.00002f) {
		throw std::runtime_error(std::string(message) + ": expected " + std::to_string(expected) + ", actual " + std::to_string(actual));
	}
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

static sgre::resource::PsbValue Range(double value, double maximum = 100) {
	return Atom(sgre::resource::PsbObject{{"value", Atom(value)}, {"drift", Atom(0.0)}, {"minValue", Atom(0.0)}, {"maxValue", Atom(maximum)}});
}

static sgre::resource::PsbValue Group(bool prewarm = false) {
	const auto common = Atom(sgre::resource::PsbObject{{"ignoreCollision", Atom(false)}, {"zofst", Atom(0.25)}, {"trigger", Atom(std::int64_t{0})}, {"triggerEmitter", Atom(std::int64_t{-1})}, {"ignoreSourceAngle", Atom(false)}, {"angle", Range(0, 360)}, {"frequency", Range(60)}, {"generationCount", Range(1)}, {"triggerVariables", Atom(sgre::resource::PsbArray{})}});
	const auto particle = Atom(sgre::resource::PsbObject{{"followMesh", Atom(false)}, {"scale", Range(1)}, {"termScaleRatio", Range(1)}, {"density", Range(0.5)}, {"velocity", Range(2)}, {"termVelocityRatio", Range(1)}, {"lifespan", Range(0.1)}, {"fadeIn", Range(0)}, {"fadeOut", Range(0)}, {"celGroup", Atom(std::int64_t{1})}, {"startWithEmissionAngle", Atom(false)}, {"faceTravelDirection", Atom(false)}, {"angularVelocity", Range(0)}, {"angularOffset", Range(0)}, {"waveAmplitude", Range(0)}, {"termWaveAmplitudeRatio", Range(1)}, {"waveFrequency", Range(0)}, {"wavePhase", Range(0)}, {"gravityStrength", Atom(0.0)}, {"gravityDirection", Atom(0.0)}});
	const auto emitter = Atom(sgre::resource::PsbObject{{"type", Atom(std::int64_t{0})}, {"x", Atom(0.0)}, {"y", Atom(0.0)}, {"emitter", common}, {"particle", particle}});
	return Atom(sgre::resource::PsbObject{{"startWithParticles", Atom(prewarm)}, {"generateParticlesAtBack", Atom(false)}, {"collisionType", Atom(std::int64_t{0})}, {"reductionRatioAtReflection", Atom(0.5)}, {"maxParticles", Atom(std::int64_t{64})}, {"speedVariables", Atom(sgre::resource::PsbArray{})}, {"collisionRefs", Atom(sgre::resource::PsbArray{})}, {"emitters", Atom(sgre::resource::PsbArray{emitter})}});
}

static std::shared_ptr<sgre::resource::PsbDocument> EmissionDocument(bool automatic = false, bool prewarm = false) {
	auto document = Document();
	const auto cel = Atom(sgre::resource::PsbObject{{"id", Atom(std::string("actual-cel"))}, {"width", Atom(std::int64_t{4})}, {"height", Atom(std::int64_t{4})}, {"weight", Atom(std::int64_t{1})}});
	const auto cel_group = Atom(sgre::resource::PsbObject{{"id", Atom(std::int64_t{1})}, {"animType", Atom(std::int64_t{0})}, {"animInterval", Atom(0.02)}, {"particleLoopCount", Atom(std::int64_t{0})}, {"originOffsetX", Atom(std::int64_t{0})}, {"originOffsetY", Atom(std::int64_t{0})}, {"cels", Atom(sgre::resource::PsbArray{cel})}});
	Set(document->Root, "metadata", Atom(sgre::resource::PsbObject{{"velvetyCommonParams", Atom(sgre::resource::PsbObject{{"ppm", Atom(2.0)}})}, {"celGroup", Atom(sgre::resource::PsbObject{{"groups", Atom(sgre::resource::PsbArray{cel_group})}})}, {"emissionMap", Atom(sgre::resource::PsbObject{{"auto", Atom(automatic ? sgre::resource::PsbArray{Atom(std::string("ordinary-uid")), Atom(std::string("missing"))} : sgre::resource::PsbArray{})}, {"ordinary-uid", Group(prewarm)}})}}));
	return document;
}

static sgre::resource::PsbValue& Field(sgre::resource::PsbValue& value, std::string_view name) {
	for (auto& [key, field] : std::get<sgre::resource::PsbObject>(value.Data)) {
		if (key == name) {
			return field;
		}
	}
	throw std::runtime_error("fixture metadata is missing actual field " + std::string(name));
}

static void TestCatalog() {
	auto document = EmissionDocument();
	auto& metadata = Field(document->Root, "metadata");
	auto& cel_groups = std::get<sgre::resource::PsbArray>(Field(Field(metadata, "celGroup"), "groups").Data);
	auto& cel_group = cel_groups.front();
	Set(cel_group, "cels", Atom(sgre::resource::PsbArray{Atom(sgre::resource::PsbObject{{"id", Atom(std::string("a"))}, {"width", Atom(std::int64_t{5})}, {"height", Atom(std::int64_t{20})}, {"weight", Atom(std::int64_t{2})}}), Atom(sgre::resource::PsbObject{{"id", Atom(std::string("b"))}, {"width", Atom(std::int64_t{2})}, {"height", Atom(std::int64_t{3})}, {"weight", Atom(std::int64_t{3})}}), Atom(sgre::resource::PsbObject{{"id", Atom(std::string("c"))}, {"width", Atom(std::int64_t{1})}, {"height", Atom(std::int64_t{2})}, {"weight", Atom(std::int64_t{5})}})}));
	Set(cel_group, "originOffsetX", Atom(std::int64_t{4}));
	Set(cel_group, "originOffsetY", Atom(std::int64_t{-2}));
	for (std::int64_t mode = 0; mode <= 3; ++mode) {
		Set(cel_group, "animType", Atom(mode));
		sgre::scene::EmoteEmissionCatalog catalog(*document);
		const auto definition = catalog.Read("ordinary-uid");
		Require(definition && definition->Emitters.size() == 1, "catalogue must preserve the actual UID's full emitter definition");
		const auto& particle = definition->Emitters.front().Definition.Particle;
		Require(particle.SourceSize == 5 && particle.SourceOrigin.X == 2 && particle.SourceOrigin.Y == -1, "native cel-group last-height overwrite and actual ppm origin conversion must remain literal");
		if (mode == 0) {
			Require(particle.Motions.size() == 3 && particle.Motions[0].Motion == "a" && particle.Motions[0].Weight == 2 && particle.Motions[0].SourceSize == 20 && particle.Motions[1].Motion == "b" && particle.Motions[1].Weight == 3 && particle.Motions[2].Weight == 5, "unanimated cels retain actual names, individual source size and weights");
		} else {
			Require(particle.Motions.size() == 1 && particle.Motions[0].Motion == (mode == 1 ? "1s" : mode == 2 ? "1r"
			                                                                                                    : "1o") &&
			        particle.Motions[0].SourceSize == 5 && particle.Motions[0].Frames == (mode == 2 ? 13u : 10u),
			    "native animated cel records retain actual generated Motion name/count");
		}
		Require(!catalog.Read("not-present"), "missing actual group remains absent rather than inventing a source");
	}
	auto& emitter = std::get<sgre::resource::PsbArray>(Field(Field(Field(metadata, "emissionMap"), "ordinary-uid"), "emitters").Data).front();
	Set(emitter, "type", Atom(std::int64_t{2}));
	sgre::scene::EmoteEmissionCatalog bezier(*document);
	RequireFailure([&] {
		static_cast<void>(bezier.Read("ordinary-uid"));
	},
	    "Bezier/type");
}

static void TestActualOwner(bool automatic, bool prewarm) {
	auto document = EmissionDocument(automatic, prewarm);
	if (automatic) {
		auto& common = Field(std::get<sgre::resource::PsbArray>(Field(Field(Field(Field(document->Root, "metadata"), "emissionMap"), "ordinary-uid"), "emitters").Data).front(), "emitter");
		Set(common, "ignoreCollision", Atom(true));
	}
	Fixture fixture(document);
	auto task = sgre::scene::MotionPlayerTask::Attach(*fixture.Runtime, fixture.Tasks, false, "character", [](const auto&) {
	});
	sgre::scene::EmoteEmissionOwner emissions(task.Commands(), *document, nullptr, 0xf1234567u);
	Require(emissions.GroupCount() == (automatic ? 1u : 0u), "actual auto array eagerly pins only resolved emissionMap UIDs");
	const auto initial = emissions.RandomState();
	const auto increment = (~std::uint64_t{0xf1234567u} * 2) | 1;
	Require(initial.Increment == increment && initial.State == (increment + 0xf1234567u) * std::uint64_t{0x5851f42d4c957f2d} + increment, "separate emission stream must use actual native rand_s word with zero extension");
	auto group = emissions.Acquire("ordinary-uid");
	Require(group && emissions.GroupCount() == 1 && !emissions.Acquire("absent-uid"), "real physical acquisition resolves actual lazy metadata only");
	RequireFailure([&] {
		static_cast<void>(group->Particles());
	},
	    "completed emission phase");
	RequireFailure([&] {
		emissions.Close();
	},
	    "physical group leases");
	task.SetCoordinate3({10, 20, 3});
	task.SetVisible(true);
	task.Play("main");
	task.Progress(0);
	const auto* plain = task.GenericPlayer();
	Require(plain && plain->Scene(), "emission phase requires its actual selected plain scene");
	const auto definition = sgre::scene::ReadEmoteVelvetyFrameDefinition(*document);
	Require(definition.has_value(), "emission source retains actual global ppm metadata");
	sgre::scene::EmoteVelvetyFrameOwner global(task.Root(), task.Commands(), *plain, *definition, 720);
	sgre::scene::EmoteVelvetyGlobalControls controls;
	controls.Flags = 0x200;
	emissions.Prepare(global.Prepare(1, controls), 1);
	Require(group->PreparedFrameSerial() == 1 && !group->Particles().empty(), "actual source/cadence/pool phase must own real force-producing particles");
	const auto first_count = group->Particles().size();
	Require(prewarm ? first_count > 1 : first_count == 1, "initial-particle prewarm executes actual one-tick producer phases only on first activation");
	const auto& particle = group->Particles().back();
	RequireNear(particle.Birth.Coordinate.X, 5, "source anchor uses actual UID transformed XY through global physical units");
	RequireNear(particle.Birth.Coordinate.Y, 11, "source anchor uses actual containing hierarchy rather than local authored coordinates");
	RequireNear(particle.Birth.Depth, 3.25f, "source depth reads actual composed UID Z plus authored zofst, not global DepthScale");
	Require(particle.Birth.IgnoreCollision == automatic, "actual common emitter collision flag reaches genuine owned births unchanged");
	auto expected_random = initial;
	for (std::uint32_t draw = 0; draw != (particle.Id + 1) * 2; ++draw) {
		expected_random.State = expected_random.State * std::uint64_t{0x5851f42d4c957f2d} + (expected_random.Increment | 1);
	}
	Require(emissions.RandomState().State == expected_random.State && emissions.RandomState().Increment == expected_random.Increment, "automatic/prewarm births consume the actual shared angle and cel RNG draws only");
	Require(particle.Birth.Motion == "actual-cel" && particle.Birth.SourceSize == 4, "real cel selection is cloned into the owned particle, not rendered as a placeholder");
	RequireNear(particle.Birth.Velocity.X, 2, "coordinate-only inheritance retains the actual child source basis instead of inheriting parent angle");
	RequireNear(particle.Birth.Velocity.Y, 0, "actual native source orientation reads its own completed numeric matrix");
	RequireFailure([&] {
		static_cast<void>(group->SamplePoint({}, 1));
	},
	    "bounds selection");
	group->PrepareBounds(-100, -100, 100, 100, 0);
	const auto point = particle.ActualPosition;
	const auto force = group->SamplePoint(point, 100);
	RequireNear(force[0], static_cast<float>(group->Particles().size()), "actual living point pool contributes its true velocity-density X force");
	RequireNear(force[1], 0, "actual living point pool contributes its true velocity-density Y force");
	Require(force[2] == 0, "native point supplier publishes no invented depth force");
	emissions.Prepare(global.Prepare(0, controls), 0);
	Require(group->Particles().size() == first_count, "zero actual elapsed does not invent births or delete retained particles");
	RequireFailure([&] {
		static_cast<void>(group->SamplePoint(point, 100));
	},
	    "bounds selection");
	task.SetTickCount(8);
	task.Progress(0);
	emissions.Prepare(global.Prepare(1, controls), 1);
	Require(group->Particles().size() <= first_count, "hidden actual anchor ages its owned pool without creating new particles");
	task.SetTickCount(0);
	task.Progress(0);
	group.reset();
	emissions.Prepare(global.Prepare(1, controls), 1);
	group = emissions.Acquire("ordinary-uid");
	const auto count_after_unleased = group->Particles().size();
	Require(automatic || count_after_unleased <= first_count, "retained cache alone does not acquire an active physical emission reference");
	task.Play("main", sgre::scene::MotionPlayFlags::Force);
	task.Progress(0);
	emissions.Prepare(global.Prepare(1, controls), 1);
	Require(group->PreparedFrameSerial() == 5, "real UID replacement rebinds emitter source without replaying initialization or fabricating a player");
	group.reset();
	emissions.Close();
	Require(task.Commands().LayerUids().SubscriptionCount() == 0, "emission retirement unsubscribes its actual source before plain owner teardown");
	fixture.Tasks.Clear();
	Require(!task.IsLive() && fixture.Runtime->Identifiers().LiveCount() == 0, "actual task and all emission borrows retire in containing owner order");
}

static void TestLiveSpeedOwner() {
	auto document = EmissionDocument();
	auto& group_definition = Field(Field(Field(document->Root, "metadata"), "emissionMap"), "ordinary-uid");
	Set(group_definition, "speedVariables", Atom(sgre::resource::PsbArray{Atom(std::string("actual-speed"))}));
	Fixture fixture(document);
	sgre::scene::MotionPlayerCommands commands(fixture.Manager);
	commands.SetCharacter("character");
	sgre::scene::EmoteVariableProgram program;
	sgre::scene::EmoteVariableControls variables(commands, [] {
	},
	    std::move(program));
	sgre::scene::EmoteEmissionOwner emissions(commands, *document, &variables, 59);
	auto group = emissions.Acquire("ordinary-uid");
	Require(variables.SpeedTransitionValue("actual-speed") == 1, "native lazy group creation binds the genuine scalar speed initialized to1");
	variables.Set("actual-speed", 0);
	variables.Update(0);
	Require(commands.ParameterValues().ReadLocal("actual-speed") == 0, "group speed remains connected to actual scalar publication");
	// No selected occurrence exists in this independent scalar-owner case, so no source posture is read.
	sgre::scene::EmoteVelvetyInputFrame frame;
	emissions.Prepare(frame, 1);
	Require(group->PreparedFrameSerial() == 1 && group->Particles().empty(), "zero effective ticks still execute the real empty pool phase");
	group.reset();
	emissions.Close();
	sgre::scene::EmoteEmissionOwner missing(commands, *document, nullptr, 59);
	RequireFailure([&] {
		static_cast<void>(missing.Acquire("ordinary-uid"));
	},
	    "genuine containing scalar-variable owner");
	Require(missing.GroupCount() == 0, "absent real variable owner cannot fabricate speed values");
	missing.Close();
}

static void TestEmptyPoolAndRejectedGroup() {
	sgre::scene::EmoteEmissionPoolDefinition empty;
	empty.EmitterCount = 0;
	sgre::scene::EmoteEmissionPool pool(empty);
	pool.Update(0);
	pool.PrepareBounds(-1, -1, 1, 1, 0);
	Require(pool.SamplePoint({}, 0) == std::array<float, 3>{}, "native empty-emitter pool remains a genuine zero-output owner");
	RequireFailure([&] {
		pool.Spawn({});
	},
	    "actual bounded emitter");
	auto document = EmissionDocument();
	auto& group = Field(Field(Field(document->Root, "metadata"), "emissionMap"), "ordinary-uid");
	Set(group, "collisionRefs", Atom(sgre::resource::PsbArray{Atom(sgre::resource::PsbObject{{"uid", Atom(std::string("real-collider"))}})}));
	Fixture fixture(document);
	auto task = sgre::scene::MotionPlayerTask::Attach(*fixture.Runtime, fixture.Tasks, false, "character", [](const auto&) {
	});
	sgre::scene::EmoteEmissionOwner emissions(task.Commands(), *document, nullptr, 37);
	RequireFailure([&] {
		static_cast<void>(emissions.Acquire("ordinary-uid"));
	},
	    "particle collision/response");
	Require(emissions.GroupCount() == 0 && task.Commands().LayerUids().SubscriptionCount() == 0, "unsupported collision pool rejects before source subscription or publication");
	emissions.Close();
	fixture.Tasks.Clear();
}

int main() {
	try {
		TestCatalog();
		TestEmptyPoolAndRejectedGroup();
		TestLiveSpeedOwner();
		TestActualOwner(false, false);
		TestActualOwner(true, true);
		std::cout << "Emission metadata, actual UID source, independent RNG, cadence, prewarm, force lease and retirement PASS\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
