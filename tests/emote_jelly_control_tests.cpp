#include "../src/scene/motion_native_compared_mesh.hpp"
#include "emote_jelly_zero_native_vectors.hpp"
#include "sgre/scene/emote_jelly_control.hpp"
#include "sgre/scene/emote_string_control.hpp"

#include "sgre/scene/emote_parts_tree.hpp"

#include "sgre/scene/emote_parts_tree.hpp"
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

	explicit Fixture(std::shared_ptr<sgre::resource::PsbDocument> document = Document())
	    : Cache([this](auto, const auto&) {
		      return sgre::render::SpriteTextureHandle{static_cast<std::uint32_t>(++Uploads), 1, &Uploads};
	      },
	          [this](auto) {
		          ++Releases;
	          }),
	      Catalog(Cache), Manager(Cache), Clock(0), Tasks(8) {
		for (std::size_t index = 0; index != Seed.size(); ++index) {
			Seed[index] = static_cast<std::uint32_t>(index) * 0x9e3779b9u + 17;
		}
		Asset = Catalog.Prepare(std::move(document), std::make_shared<const std::vector<std::uint8_t>>(144, 255));
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

static sgre::resource::PsbValue ControlMetadata() {
	const auto tress = Atom(sgre::resource::PsbObject{{"topXRate", Atom(0.5)}, {"topYRate", Atom(0.0)}, {"bottomXRate", Atom(0.5)}, {"bottomYRate", Atom(1.0)}});
	return Atom(sgre::resource::PsbObject{{"uid", Atom(std::string("mesh-uid"))}, {"ppm", Atom(2.0)}, {"boneDirection", Atom(std::int64_t{0})}, {"gravity", Atom(1.0)}, {"roughness", Atom(0.0)}, {"rigidMode", Atom(std::int64_t{0})}, {"sizeH", Atom(2.0)}, {"sizeV", Atom(4.0)}, {"originX", Atom(0.25)}, {"originY", Atom(0.75)}, {"splitLeft", Atom(0.5)}, {"splitRight", Atom(0.5)}, {"mass", Atom(1.0)}, {"numPerMeter", Atom(1.0)}, {"frictionTop", Atom(0.0)}, {"frictionBottom", Atom(0.0)}, {"elastic", Atom(0.0)}, {"straightness", Atom(0.0)}, {"coordScaleH", Atom(1.0)}, {"coordScaleV", Atom(1.0)}, {"coordScaleD", Atom(0.0)}, {"compressWiden", Atom(0.0)}, {"controlPointSize", Atom(0.0)}, {"adhocSim", Atom(false)}, {"tresses", Atom(sgre::resource::PsbArray{tress})}, {"rigidRange", Atom(sgre::resource::PsbObject{{"l", Atom(0.0)}, {"t", Atom(0.0)}, {"r", Atom(1.0)}, {"b", Atom(1.0)}})}, {"rigidIter", Atom(std::int64_t{0})}, {"fixes", Atom(sgre::resource::PsbArray{})}, {"coordPred", Atom(std::int64_t{3})}, {"collisionRefs", Atom(sgre::resource::PsbArray{})}, {"emissionRefs", Atom(sgre::resource::PsbArray{})}, {"speedVariables", Atom(sgre::resource::PsbArray{})}});
}

static sgre::scene::EmoteStringControlDefinition ReadDefinition(sgre::resource::PsbValue metadata) {
	sgre::resource::PsbDocument document;
	document.Root = Atom(sgre::resource::PsbObject{{"metadata", Atom(sgre::resource::PsbObject{{"velvetyCommonParams", Atom(sgre::resource::PsbObject{{"ppm", Atom(2.0)}})}, {"velvetyStringControl", Atom(sgre::resource::PsbArray{std::move(metadata)})}})}});
	auto definitions = sgre::scene::ReadEmoteStringControlDefinitions(document);
	Require(definitions.size() == 1, "actual control metadata must preserve one real physical/output/driver record");
	return std::move(definitions.front());
}

static std::vector<sgre::resource::MotionPoint> Positions(const sgre::scene::MotionSceneEvaluation& scene) {
	for (std::size_t index = 0; index != scene.PacketRows().size(); ++index) {
		if (scene.PacketRows()[index]->Node().Raw->Label == "mesh") {
			const auto& command = scene.Packets().Nodes()[index].Command;
			Require(command.Geometry && !command.Hidden && command.GeometryCount != 0, "actual String target must retain completed geometry");
			return {command.Geometry->Positions.begin(), command.Geometry->Positions.end()};
		}
	}
	throw std::runtime_error("actual String target is missing from its scene packets");
}

static void RequirePoints(const std::vector<sgre::resource::MotionPoint>& actual, const std::vector<sgre::resource::MotionPoint>& expected, std::string_view operation) {
	Require(actual.size() == expected.size(), operation);
	for (std::size_t index = 0; index != actual.size(); ++index) {
		Require(std::bit_cast<std::uint32_t>(actual[index].X) == std::bit_cast<std::uint32_t>(expected[index].X) && std::bit_cast<std::uint32_t>(actual[index].Y) == std::bit_cast<std::uint32_t>(expected[index].Y), operation);
	}
}

static sgre::resource::PsbValue Part(std::string uid, sgre::resource::PsbArray children = {}) {
	return Atom(sgre::resource::PsbObject{{"uid", Atom(std::move(uid))}, {"label", Atom(std::string("actual part"))}, {"children", Atom(std::move(children))}});
}

static sgre::resource::PsbValue& MutableField(sgre::resource::PsbValue& object, std::string_view name) {
	for (auto& [key, value] : std::get<sgre::resource::PsbObject>(object.Data)) {
		if (key == name) {
			return value;
		}
	}
	throw std::runtime_error("independent parts fixture field absent: " + std::string(name));
}

static std::shared_ptr<sgre::resource::PsbDocument> TwoIndependentMeshes() {
	auto document = Document();
	auto& objects = MutableField(document->Root, "object");
	auto& timeline = MutableField(MutableField(MutableField(objects, "character"), "motion"), "main");
	auto first = Layer(true);
	Set(first, "children", Atom(sgre::resource::PsbArray{}));
	Set(first, "meshSyncChildMask", Atom(std::int64_t{0}));
	auto second = Layer(true);
	Set(second, "children", Atom(sgre::resource::PsbArray{}));
	Set(second, "meshSyncChildMask", Atom(std::int64_t{0}));
	Set(second, "uid", Atom(std::string("other-mesh-uid")));
	Set(second, "label", Atom(std::string("other-mesh")));
	for (auto& frame : std::get<sgre::resource::PsbArray>(MutableField(second, "frameList").Data)) {
		Set(MutableField(frame, "content"), "angle", Atom(0.0));
	}
	Set(timeline, "layer", Atom(sgre::resource::PsbArray{std::move(first), std::move(second)}));
	return document;
}

static sgre::resource::PsbValue JellyVector3(double x, double y, double z) {
	return Atom(sgre::resource::PsbObject{{"x", Atom(x)}, {"y", Atom(y)}, {"z", Atom(z)}});
}

static sgre::resource::PsbValue JellyMetadata() {
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
	fields.emplace_back("tipTilt", JellyVector3(0, 0, 1));
	fields.emplace_back("rigidRange", Atom(sgre::resource::PsbObject{{"l", Atom(0.0)}, {"t", Atom(0.0)}, {"r", Atom(1.0)}, {"b", Atom(1.0)}}));
	fields.emplace_back("fixes", Atom(sgre::resource::PsbArray{}));
	fields.emplace_back("tresses", Atom(sgre::resource::PsbArray{Atom(sgre::resource::PsbObject{{"x", Atom(0.5)}, {"y", Atom(0.5)}, {"r", Atom(0.25)}, {"jellyTilt", JellyVector3(0, 0, 1)}})}));
	fields.emplace_back("speedVariables", Atom(sgre::resource::PsbArray{}));
	fields.emplace_back("collisionRefs", Atom(sgre::resource::PsbArray{}));
	fields.emplace_back("emissionRefs", Atom(sgre::resource::PsbArray{}));
	return Atom(std::move(fields));
}

static sgre::scene::EmoteJellyControlDefinition ReadJellyDefinition(std::string uid = "mesh-uid") {
	auto control = JellyMetadata();
	Set(control, "uid", Atom(std::move(uid)));
	sgre::resource::PsbDocument document;
	document.Root = Atom(sgre::resource::PsbObject{{"metadata", Atom(sgre::resource::PsbObject{{"velvetyCommonParams", Atom(sgre::resource::PsbObject{})}, {"velvetyJellyControl", Atom(sgre::resource::PsbArray{std::move(control)})}})}});
	auto definitions = sgre::scene::ReadEmoteJellyControlDefinitions(document);
	Require(definitions.size() == 1, "actual Jelly metadata reader preserves exactly one physical/control definition");
	return std::move(definitions.front());
}

static void TestActualJellyOccurrenceAndTail() {
	Fixture fixture;
	auto task = sgre::scene::MotionPlayerTask::Attach(*fixture.Runtime, fixture.Tasks, false, "character", [](const auto&) {
	});
	task.SetVisible(true);
	task.Play("main");
	task.Progress(0);
	const auto* plain = task.GenericPlayer();
	Require(plain && plain->Scene(), "actual Jelly control requires the genuine selected primary scene");
	const auto& matches = task.Commands().LayerUids().Matches("mesh-uid");
	Require(matches.size() == 1, "real selected mesh publishes exactly one original UID occurrence");
	matches.front().Layer.InitializeMeshOrigin({17, 19}, {0.125f, 0.375f});
	task.Progress(0);
	const auto before_subscription = Positions(*plain->Scene());
	auto definition = ReadJellyDefinition();
	definition.Physics.Fixes = {{0.25f, 0.75f}};
	sgre::scene::EmoteJellyControl control(task.Commands(), definition);
	control.RequireReady();
	Require(control.InstanceCount() == 1 && task.Commands().LayerUids().SubscriptionCount() == 1, "native late Added creates a real particle owner for the existing UID");
	task.Progress(0);
	RequirePoints(Positions(*plain->Scene()), before_subscription, "Jelly Added must not run String's51f0f0 origin mutation or any second plain step");
	sgre::scene::EmoteVelvetyFrameDefinition frame_definition;
	frame_definition.PixelsPerMeter = 100;
	sgre::scene::EmoteVelvetyFrameOwner global(task.Root(), task.Commands(), *plain, frame_definition, 720);
	sgre::scene::EmoteVelvetyGlobalControls globals;
	globals.Flags = 0x200;
	for (std::size_t frame = 0; frame != 3; ++frame) {
		if (frame != 0) {
			task.Progress(1);
		}
		const auto actual = global.Prepare(1, globals);
		const auto before = Positions(*plain->Scene());
		control.Update(1, actual);
		Require(control.LastUpdatedCount() == 1 && control.LastMeshWriteCount() <= 1, "one active real UID performs one native ordered Jelly update/pending mesh write");
		RequirePoints(Positions(*plain->Scene()), before, "Jelly physical tail preserves already completed geometry until the next plain update");
	}
	const auto retained = control.PartPosture();
	task.SetTickCount(9);
	task.Progress(0);
	control.Update(1, global.Prepare(1, globals));
	Require(control.LastUpdatedCount() == 0 && control.LastMeshWriteCount() == 0, "inactive real UID resets reactivation state without fabricated physics/output");
	RequireNear(control.PartPosture().Rotation, retained.Rotation, "inactive Jelly control retains its preceding part posture");
	task.SetTickCount(0);
	task.Progress(0);
	control.Update(1, global.Prepare(1, globals));
	Require(control.LastUpdatedCount() == 1 && control.InstanceCount() == 1, "actual reactivation initializes the retained physical instance once");
	const auto prior_occurrence = matches.front().Occurrence;
	task.Play("main", sgre::scene::MotionPlayFlags::Force);
	control.RequireReady();
	Require(control.InstanceCount() == 1 && task.Commands().LayerUids().Matches("mesh-uid").front().Occurrence != prior_occurrence, "true Force selection retires the former owner and constructs only the new actual occurrence");
	control.Close();
	Require(control.InstanceCount() == 0 && task.Commands().LayerUids().SubscriptionCount() == 0, "Jelly particles retire and unsubscribe before actual Commands teardown");
	fixture.Tasks.Clear();
}

static void TestRealMixedPartsTree() {
	Fixture fixture(TwoIndependentMeshes());
	auto task = sgre::scene::MotionPlayerTask::Attach(*fixture.Runtime, fixture.Tasks, false, "character", [](const auto&) {
	});
	sgre::scene::EmoteStringControl parent(task.Commands(), ReadDefinition(ControlMetadata()));
	sgre::scene::EmoteJellyControl child(task.Commands(), ReadJellyDefinition("other-mesh-uid"));
	sgre::scene::EmotePartsTree tree(task.Commands(), Atom(sgre::resource::PsbArray{Part("mesh-uid", {Part("other-mesh-uid")})}));
	Require(tree.AttachJelly(child) && tree.AttachString(parent) && tree.JellyCount() == 1 && tree.StringCount() == 1, "actual tree registers both real physical families on exact serialized part UIDs");
	task.SetVisible(true);
	task.Play("main");
	task.Progress(0);
	const auto* plain = task.GenericPlayer();
	Require(plain && plain->Scene(), "mixed callbacks borrow one genuine selected scene");
	sgre::scene::EmoteVelvetyFrameDefinition frame_definition;
	frame_definition.PixelsPerMeter = 100;
	sgre::scene::EmoteVelvetyFrameOwner global(task.Root(), task.Commands(), *plain, frame_definition, 720);
	sgre::scene::EmoteVelvetyGlobalControls globals;
	globals.Flags = 0x200;
	const auto frame = global.Prepare(1, globals);
	RequireFailure([&] {
		tree.UpdateStrings(1, frame);
	},
	    "String-only dispatch");
	const auto before = Positions(*plain->Scene());
	tree.UpdateControls(1, frame);
	Require(parent.LastUpdatedCount() == 1 && child.LastUpdatedCount() == 1, "native preorder drives actual String parent before Jelly child exactly once");
	RequireNear(child.PartPosture().Rotation, parent.PartPosture().Rotation, "independent root Jelly takes retained true PartsTree parent rotation rather than own zero angle");
	RequireNear(child.PartPosture().Rotation, 1.57079637050628662109375f, "real parent quarter turn is physically discriminating");
	RequirePoints(Positions(*plain->Scene()), before, "mixed family dispatch produces pending input without another scene update");
	fixture.Tasks.Group(sgre::scene::MotionTaskGroup::Second).Draw(0);
	RequireFailure([&] {
		task.Progress(1);
	},
	    "queued");
	fixture.Runtime->SceneContext().Clear();
	tree.Close();
	child.Close();
	parent.Close();
	fixture.Tasks.Clear();
}

static void TestZeroSpeedRequiresActualActiveDispatch() {
	Fixture fixture;
	auto task = sgre::scene::MotionPlayerTask::Attach(*fixture.Runtime, fixture.Tasks, false, "character", [](const auto&) {
	});
	task.SetVisible(true);
	task.Play("main");
	task.Progress(0);
	const auto* plain = task.GenericPlayer();
	Require(plain && plain->Scene(), "zero-speed driver fixture uses its genuine selected plain scene");
	sgre::scene::EmoteVelvetyFrameDefinition frame_definition;
	frame_definition.PixelsPerMeter = 100;
	sgre::scene::EmoteVelvetyFrameOwner global(task.Root(), task.Commands(), *plain, frame_definition, 720);
	sgre::scene::EmoteVelvetyGlobalControls globals;
	globals.Flags = 0x200;
	{
		auto definition = ReadJellyDefinition("absent-uid");
		definition.Physics.Speed = 0;
		sgre::scene::EmoteJellyControl unmatched(task.Commands(), definition);
		unmatched.Update(1, global.Prepare(1, globals));
		Require(unmatched.InstanceCount() == 0 && unmatched.LastUpdatedCount() == 0 && unmatched.LastMeshWriteCount() == 0, "native zero-speed declaration without an actual UID occurrence must not fabricate physical dispatch");
		unmatched.Close();
	}

	task.SetTickCount(9);
	task.Progress(0);
	auto definition = ReadJellyDefinition();
	definition.Physics.Speed = 0;
	sgre::scene::EmoteJellyControl control(task.Commands(), definition);
	control.Update(1, global.Prepare(1, globals));
	Require(control.InstanceCount() == 1 && control.LastUpdatedCount() == 0 && control.LastMeshWriteCount() == 0, "genuine inactive UID must only mark reset and retain its zero-speed physical instance");
	const auto retained = control.PartPosture();
	control.Update(3, global.Prepare(3, globals));
	Require(control.PartPosture().Rotation == retained.Rotation && control.PartPosture().Scale == retained.Scale, "inactive zero-speed control must preserve retained part cache without a fake posture or mesh");
	task.SetTickCount(0);
	task.Progress(0);
	const auto before = Positions(*plain->Scene());
	RequireFailure([&] {
		control.Update(1, global.Prepare(1, globals));
	},
	    "active UID scaled time");
	Require(control.LastUpdatedCount() == 0 && control.LastMeshWriteCount() == 0, "actual active zero effective solver time must remain a precise gate rather than inventing a solver skip");
	RequirePoints(Positions(*plain->Scene()), before, "active zero-step rejection must preserve the genuine completed plain geometry");
	control.Close();
}

static void TestActualUnorderedZeroSpeedComparison() {
	for (std::size_t fix_count = 0; fix_count != 3; ++fix_count) {
		Fixture fixture;
		auto task = sgre::scene::MotionPlayerTask::Attach(*fixture.Runtime, fixture.Tasks, false, "character", [](const auto&) {
		});
		task.SetVisible(true);
		task.Play("main");
		task.Progress(0);
		const auto* plain = task.GenericPlayer();
		Require(plain && plain->Scene(), "zero comparison uses the actual selected scene");
		const auto& target = task.Commands().LayerUids().Matches("mesh-uid").front().Layer;
		target.SetMesh(1, nullptr, false);
		task.Progress(0);
		auto definition = ReadJellyDefinition();
		definition.Physics.Speed = 0;
		definition.Physics.Sensitivity = 0;
		definition.Physics.AdhocSimulation = false;
		definition.Physics.Substeps = 2;
		definition.Physics.ParticleCount = 2;
		const std::array<sgre::resource::MotionPoint, 2> fixes{{{0.25f, 0.25f}, {0.75f, 0.75f}}};
		definition.Physics.Fixes.assign(fixes.begin(), fixes.begin() + fix_count);
		definition.Physics.CorrectFix = fix_count != 0;
		definition.Physics.CorrectScale = fix_count == 2;
		sgre::scene::EmoteJellyPhysics dimensions(definition.Physics);
		sgre::scene::EmoteJellyControl control(task.Commands(), definition);
		sgre::scene::EmoteVelvetyFrameDefinition frame_definition;
		frame_definition.PixelsPerMeter = 100;
		sgre::scene::EmoteVelvetyFrameOwner global(task.Root(), task.Commands(), *plain, frame_definition, 720);
		sgre::scene::EmoteVelvetyGlobalControls globals;
		globals.Flags = 0x200;
		const auto before = Positions(*plain->Scene());
		const auto update = [&](std::string_view phase) {
			try {
				control.Update(1, global.Prepare(1, globals));
			} catch (const std::runtime_error& error) {
				throw std::runtime_error("zero Jelly fixes=" + std::to_string(fix_count) + " phase=" + std::string(phase) + " current_degree=" + std::to_string(target.CurrentMesh().EncodedDegree) + ": " + error.what());
			}
		};
		update("empty comparison");
		Require(control.LastUpdatedCount() == 1 && control.LastMeshWriteCount() == 0 && target.CurrentMesh().EncodedDegree == 0, "actual zero-second substeps and output must reach native unordered identity comparison without inventing a mesh write");
		RequirePoints(Positions(*plain->Scene()), before, "zero output comparison preserves completed finite geometry");
		auto deformed = sgre::scene::IdentityMotionBezierLattice(dimensions.EncodedDegree());
		// The no-fix native output retains finite boundary endpoints; perturb an actually unordered interior cell.
		deformed.Controls[static_cast<std::size_t>(deformed.EncodedDegree & 15) + 2].X += 0.25f;
		Require(target.SetMesh(1, &deformed, true), "causal zero comparison begins with a real nonidentity pending override");
		update("deformed comparison");
		Require(control.LastUpdatedCount() == 1 && control.LastMeshWriteCount() == 1 && target.CurrentMesh().EncodedDegree == 0, "native unordered equality clears the actual finite override instead of copying nonfinite output");
		RequirePoints(Positions(*plain->Scene()), before, "pending finite clear does not compose a second scene frame");
		task.Progress(0);
		for (const auto& point : Positions(*plain->Scene())) {
			Require(std::isfinite(point.X) && std::isfinite(point.Y), "following actual plain update must never consume nonfinite Jelly controls");
		}
		update("repeated empty comparison");
		Require(control.LastUpdatedCount() == 1 && control.LastMeshWriteCount() == 0, "repeated zero steps retain the actual unordered model state and empty override");
		task.SetTickCount(9);
		task.Progress(0);
		update("inactive reset");
		Require(control.LastUpdatedCount() == 0, "hidden zero-speed owner retains actual reset semantics");
		task.SetTickCount(0);
		task.Progress(0);
		update("reactivation");
		Require(control.LastUpdatedCount() == 1, "reactivation reinitializes the actual model before genuine zero substeps");
		task.Play("main", sgre::scene::MotionPlayFlags::Force);
		control.RequireReady();
		Require(control.InstanceCount() == 1, "Force retires unordered physical state and installs only the new true UID occurrence");
		control.Close();
	}
}

static void TestOriginalZeroOutputComparisonWords() {
	for (std::size_t index = 0; index != kJellyZeroNativeOutputs.size(); ++index) {
		const auto& native = kJellyZeroNativeOutputs[index];
		sgre::resource::MotionBezierLattice output;
		output.EncodedDegree = native.Degree;
		for (std::size_t word = 0; word != native.Words.size(); word += 2) {
			output.Controls.push_back({std::bit_cast<float>(native.Words[word]), std::bit_cast<float>(native.Words[word + 1])});
		}
		sgre::resource::MotionBezierLattice current;
		Require(!sgre::scene::AssignNativeComparedMotionBezierLattice(current, output) && current.EncodedDegree == 0, "all four original zero-speed outputs preserve an actually empty override under native unordered comparison");
		current = sgre::scene::IdentityMotionBezierLattice(native.Degree);
		current.Controls.front().X += 0.25f;
		if (index == 2) {
			const auto before = current.Controls.front();
			RequireFailure([&] {
				sgre::scene::AssignNativeComparedMotionBezierLattice(current, output);
			},
			    "finite coordinates");
			Require(current.Controls.front().X == before.X && current.Controls.front().Y == before.Y && current.EncodedDegree == native.Degree, "native mixed Sensitivity1 copy remains rejected before any current mutation");
		} else {
			Require(sgre::scene::AssignNativeComparedMotionBezierLattice(current, output) && current.EncodedDegree == 0 && current.Controls.empty(), "original all-unordered output must clear an actual matching finite deformed override");
		}
		RequireFailure([&] {
			sgre::scene::AssignMotionBezierLattice(current, &output, true);
		},
		    "finite coordinates");
	}
}

static void TestOriginalZeroInteriorAndBoundary() {
	for (std::size_t index = 0; index != kJellyZeroInteriorOutputs.size(); ++index) {
		const auto& native = kJellyZeroInteriorOutputs[index];
		sgre::resource::MotionBezierLattice output;
		output.EncodedDegree = native.Degree;
		for (std::size_t word = 0; word != native.Words.size(); word += 2) {
			output.Controls.push_back({std::bit_cast<float>(native.Words[word]), std::bit_cast<float>(native.Words[word + 1])});
		}
		const auto interior = static_cast<std::size_t>(native.Degree & 15) + 2;
		Require(!std::isfinite(output.Controls[interior].X), "actual zero-speed physical fixture publishes an unordered interior control");
		auto current = sgre::scene::IdentityMotionBezierLattice(native.Degree);
		current.Controls[interior].X += 0.25f;
		Require(sgre::scene::AssignNativeComparedMotionBezierLattice(current, output) && current.EncodedDegree == 0, "original unordered interior comparison clears the matching finite override");
		if (index == 0) {
			Require(std::isfinite(output.Controls.front().X), "actual no-fix inverse retains its finite boundary endpoint");
			current = sgre::scene::IdentityMotionBezierLattice(native.Degree);
			current.Controls.front().X += 0.25f;
			RequireFailure([&] {
				sgre::scene::AssignNativeComparedMotionBezierLattice(current, output);
			},
			    "finite coordinates");
			Require(current.EncodedDegree == native.Degree && current.Controls.front().X == 0.25f, "native no-fix boundary mismatch requires an unsafe copy and must reject without current mutation");
		}
	}
}

static void TestRequiredProducerAndNotificationFailure() {
	Fixture fixture;
	auto task = sgre::scene::MotionPlayerTask::Attach(*fixture.Runtime, fixture.Tasks, false, "character", [](const auto&) {
	});
	auto definition = ReadJellyDefinition();
	definition.CollisionReferences.push_back({"actual-collider", 0});
	RequireFailure([&] {
		sgre::scene::EmoteJellyControl unsupported(task.Commands(), definition);
	},
	    "actual Bobble collider attachment");
	Require(task.Commands().LayerUids().SubscriptionCount() == 0, "missing real collider producer rejects before any UID subscription");
	definition.CollisionReferences.clear();
	definition.RequiresEmissions = true;
	RequireFailure([&] {
		sgre::scene::EmoteJellyControl unsupported(task.Commands(), definition);
	},
	    "actual emission force owner");
	definition.RequiresEmissions = false;
	definition.SpeedVariables = {"actual-speed"};
	RequireFailure([&] {
		sgre::scene::EmoteJellyControl unsupported(task.Commands(), definition);
	},
	    "actual transition publisher");
	definition.SpeedVariables.clear();
	definition.Physics.Tresses.front().Radius = 0;
	sgre::scene::EmoteJellyControl invalid(task.Commands(), definition);
	Require(invalid.InstanceCount() == 0, "actual control declaration does not invent an occurrence before selection");
	task.Play("main");
	RequireFailure([&] {
		invalid.RequireReady();
	},
	    "finite positive");
	Require(invalid.InstanceCount() == 0, "failed true Added constructor preserves exception without retaining a partial physics owner");
	invalid.Close();
	Require(task.Commands().LayerUids().SubscriptionCount() == 0, "failed notification still permits exact subscription retirement");
	fixture.Tasks.Clear();
}

int main() {
	try {
		TestActualJellyOccurrenceAndTail();
		TestRealMixedPartsTree();
		TestZeroSpeedRequiresActualActiveDispatch();
		TestActualUnorderedZeroSpeedComparison();
		TestOriginalZeroOutputComparisonWords();
		TestOriginalZeroInteriorAndBoundary();
		TestRequiredProducerAndNotificationFailure();
		std::cout << "Actual Jelly UID owner: real registration/global frame/physics/pending mesh, mixed PartsTree and retirement passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
