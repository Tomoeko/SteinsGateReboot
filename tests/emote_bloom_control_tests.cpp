#include "sgre/scene/emote_bloom_control.hpp"

#include "../src/scene/motion_native_compared_mesh.hpp"
#include "emote_bloom_zero_native_vectors.hpp"

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

	explicit Fixture(std::shared_ptr<sgre::resource::PsbDocument> document = Document()) : Cache([this](auto, const auto&) {
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

static sgre::resource::PsbValue BloomMetadata() {
	return Atom(sgre::resource::PsbObject{{"uid", Atom(std::string("mesh-uid"))}, {"ppm", Atom(2.0)}, {"sizeH", Atom(2.0)}, {"sizeV", Atom(4.0)}, {"originX", Atom(0.4)}, {"originY", Atom(0.55)}, {"centerX", Atom(0.5)}, {"centerY", Atom(0.5)}, {"mass", Atom(0.4)}, {"defaultRot", Atom(0.15)}, {"limitLengthRatio", Atom(2.0)}, {"elastic", Atom(2.5)}, {"friction", Atom(0.25)}, {"straightness", Atom(0.25)}, {"rotateWeight", Atom(0.4)}, {"twistWeight", Atom(0.3)}, {"trapeziumWeight", Atom(0.2)}, {"pitchWeight", Atom(0.1)}, {"coordScaleH", Atom(0.75)}, {"coordScaleV", Atom(1.25)}, {"coordScaleD", Atom(0.1)}, {"coordPred", Atom(std::int64_t{3})}, {"tresses", Atom(sgre::resource::PsbArray{Atom(sgre::resource::PsbObject{{"x", Atom(0.2)}, {"y", Atom(0.2)}})})}, {"fixes", Atom(sgre::resource::PsbArray{})}, {"speedVariables", Atom(sgre::resource::PsbArray{})}, {"collisionRefs", Atom(sgre::resource::PsbArray{})}, {"emissionRefs", Atom(sgre::resource::PsbArray{})}});
}
static sgre::scene::EmoteBloomControlDefinition ReadDefinition(sgre::resource::PsbValue value) {
	sgre::resource::PsbDocument document;
	document.Root = Atom(sgre::resource::PsbObject{{"metadata", Atom(sgre::resource::PsbObject{{"velvetyCommonParams", Atom(sgre::resource::PsbObject{})}, {"velvetyBloomControl", Atom(sgre::resource::PsbArray{std::move(value)})}})}});
	auto definitions = sgre::scene::ReadEmoteBloomControlDefinitions(document);
	Require(definitions.size() == 1, "actual Bloom definition retains serialized control ownership");
	return std::move(definitions.front());
}
static void TestMetadata() {
	auto metadata = BloomMetadata();
	auto definition = ReadDefinition(metadata);
	RequireNear(definition.Physics.SizeHorizontal, 2, "physical dimensions remain physical; ppm belongs to UID conversion");
	Require(definition.Physics.Substeps == 1 && definition.Physics.Speed == 1 && definition.InheritShape && !definition.Physics.CorrectFix && !definition.Physics.CorrectScale, "native absent defaults and inheritShape policy are distinct from missing producers");
	Set(metadata, "autoMass", Atom(true));
	Set(metadata, "imageArea", Atom(12.0));
	Set(metadata, "massPerSquareMeter", Atom(0.25));
	Set(metadata, "mass", Atom(99.0));
	Set(metadata, "inheritShape", Atom(false));
	Set(metadata, "correctFix", Atom(true));
	Set(metadata, "correctScale", Atom(true));
	definition = ReadDefinition(metadata);
	RequireNear(definition.Physics.Mass, 0.75f, "Bloom autoMass uses actual imageArea*areaDensity/(ppm*ppm)");
	Require(definition.Physics.CorrectFix && definition.Physics.CorrectScale, "only explicit noninherit policy enables actual correction phases");
	Set(metadata, "autoMass", Atom(false));
	RequireNear(ReadDefinition(metadata).Physics.Mass, 99, "presentfalse autoMass reads mass rather than automatic expression");
	Set(metadata, "mass", Atom(std::string("invalid")));
	RequireFailure([&] {
		static_cast<void>(ReadDefinition(metadata));
	},
	    "native numeric metadata");
	sgre::resource::PsbDocument absent;
	absent.Root = Atom(sgre::resource::PsbObject{{"metadata", Atom(sgre::resource::PsbObject{})}});
	Require(sgre::scene::ReadEmoteBloomControlDefinitions(absent).empty(), "no actual common owner yields no invented Bloom definitions");
}
static std::vector<sgre::resource::MotionPoint> Positions(const sgre::scene::MotionSceneEvaluation& scene) {
	for (std::size_t index = 0; index != scene.PacketRows().size(); ++index) {
		if (scene.PacketRows()[index]->Node().Raw->Label == "mesh") {
			const auto& command = scene.Packets().Nodes()[index].Command;
			Require(command.Geometry && !command.Hidden, "actual target retains completed geometry");
			return {command.Geometry->Positions.begin(), command.Geometry->Positions.end()};
		}
	}
	throw std::runtime_error("actual Bloom target absent");
}
static void RequirePoints(const std::vector<sgre::resource::MotionPoint>& left, const std::vector<sgre::resource::MotionPoint>& right, std::string_view context) {
	Require(left.size() == right.size(), context);
	for (std::size_t index = 0; index != left.size(); ++index) {
		Require(left[index].X == right[index].X && left[index].Y == right[index].Y, context);
	}
}
static void TestActualTailAndRetirement() {
	Fixture fixture;
	auto task = sgre::scene::MotionPlayerTask::Attach(*fixture.Runtime, fixture.Tasks, false, "character", [](const auto&) {
	});
	sgre::scene::EmoteBloomControl declaration(task.Commands(), ReadDefinition(BloomMetadata()));
	Require(declaration.InstanceCount() == 0, "control declaration does not invent a selected UID before Play");
	declaration.Close();
	task.SetVisible(true);
	task.Play("main");
	task.Progress(0);
	const auto& matches = task.Commands().LayerUids().Matches("mesh-uid");
	Require(matches.size() == 1, "genuine Added creates the actual owned Bloom point solver");
	const auto layer = matches.front().Layer;
	layer.InitializeMeshOrigin({0.125f, -0.25f}, {0.75f, 0.625f});
	task.Progress(0);
	const auto mesh_origin = layer.MeshPhysicalOrigin();
	sgre::scene::EmoteBloomControl control(task.Commands(), ReadDefinition(BloomMetadata()));
	Require(control.InstanceCount() == 1 && layer.MeshPhysicalOrigin().X == mesh_origin.X && layer.MeshPhysicalOrigin().Y == mesh_origin.Y, "late Bloom Added preserves the actual retained mesh origin");
	const auto* plain = task.GenericPlayer();
	Require(plain && plain->Scene(), "Bloom consumes the true selected scene under its actual task");
	sgre::scene::EmoteVelvetyFrameDefinition frame_definition;
	frame_definition.PixelsPerMeter = 2;
	sgre::scene::EmoteVelvetyFrameOwner global(task.Root(), task.Commands(), *plain, frame_definition, 720);
	sgre::scene::EmoteVelvetyGlobalControls globals;
	globals.Flags = 0x200;
	sgre::scene::EmotePartsTree tree(task.Commands(), Atom(sgre::resource::PsbArray{Atom(sgre::resource::PsbObject{{"uid", Atom(std::string("mesh-uid"))}})}));
	Require(tree.AttachBloom(control) && tree.BloomCount() == 1, "native tree indexes and borrows the real typed Bloom callback");
	for (std::size_t frame = 0; frame != 3; ++frame) {
		if (frame != 0) {
			task.Progress(1);
		}
		const auto before = Positions(*plain->Scene());
		tree.UpdateControls(1, global.Prepare(1, globals));
		Require(control.LastUpdatedCount() == 1 && control.LastMeshWriteCount() <= 1, "one real active UID advances once after actual plain publication");
		RequirePoints(Positions(*plain->Scene()), before, "Bloom tail sets pending mesh without overwriting completed packet geometry or running another plain step");
		Require(layer.MeshPhysicalOrigin().X == mesh_origin.X && layer.MeshPhysicalOrigin().Y == mesh_origin.Y, "Bloom does not invoke String origin reset");
	}
	task.SetTickCount(9);
	task.Progress(0);
	tree.UpdateControls(1, global.Prepare(1, globals));
	Require(control.LastUpdatedCount() == 0, "actual hidden interval retains/reset-marks physical owner without fabricated writes");
	tree.Close();
	task.Play("main", sgre::scene::MotionPlayFlags::Force);
	control.RequireReady();
	Require(control.InstanceCount() == 1, "true selected replacement retires old occurrence before new actual owner");
	control.Close();
	Require(task.Commands().LayerUids().SubscriptionCount() == 0, "exact unsubscribe precedes selected/task retirement");
	fixture.Tasks.Clear();
}
static void TestMissingProducersAndAddedFailure() {
	Fixture fixture;
	auto task = sgre::scene::MotionPlayerTask::Attach(*fixture.Runtime, fixture.Tasks, false, "character", [](const auto&) {
	});
	auto definition = ReadDefinition(BloomMetadata());
	definition.RequiresEmissions = true;
	RequireFailure([&] {
		sgre::scene::EmoteBloomControl invalid(task.Commands(), definition);
	},
	    "actual emission force owner");
	definition.RequiresEmissions = false;
	definition.CollisionReferences.push_back({"actual-collision", 0});
	RequireFailure([&] {
		sgre::scene::EmoteBloomControl invalid(task.Commands(), definition);
	},
	    "unmasked base-collider");
	Require(task.Commands().LayerUids().SubscriptionCount() == 0, "unimplemented required producer rejects before UID mutation");
	definition.CollisionReferences.clear();
	definition.Physics.Mass = 0;
	sgre::scene::EmoteBloomControl control(task.Commands(), definition);
	task.Play("main");
	RequireFailure([&] {
		control.RequireReady();
	},
	    "positive physical");
	Require(control.InstanceCount() == 0, "noexcept Added retains first construction failure without a partial point owner");
	control.Close();
	fixture.Tasks.Clear();
}
static void TestInactiveZeroSpeedActualUid() {
	Fixture fixture;
	auto task = sgre::scene::MotionPlayerTask::Attach(*fixture.Runtime, fixture.Tasks, false, "character", [](const auto&) {
	});
	auto metadata = BloomMetadata();
	Set(metadata, "speed", Atom(0.0));
	sgre::scene::EmoteBloomControl control(task.Commands(), ReadDefinition(metadata));
	Require(control.InstanceCount() == 0, "declaration keeps real zero-speed metadata without fabricating a UID owner");
	task.SetVisible(true);
	task.Play("main");
	task.SetTickCount(9);
	task.Progress(0);
	control.RequireReady();
	Require(control.InstanceCount() == 1, "actual UID Added must initialize the native zero-speed point owner");
	const auto& matches = task.Commands().LayerUids().Matches("mesh-uid");
	Require(matches.size() == 1 && !matches.front().Layer.Active(), "real timeline hidden interval must precede inactive driver admission");
	const auto layer = matches.front().Layer;
	const auto* plain = task.GenericPlayer();
	Require(plain && plain->Scene(), "inactive Bloom still borrows its real selected plain scene");
	sgre::scene::EmoteVelvetyFrameDefinition frame_definition;
	frame_definition.PixelsPerMeter = 2;
	sgre::scene::EmoteVelvetyFrameOwner global(task.Root(), task.Commands(), *plain, frame_definition, 720);
	sgre::scene::EmoteVelvetyGlobalControls globals;
	globals.Flags = 0x200;
	for (std::size_t frame = 0; frame != 2; ++frame) {
		control.Update(1, global.Prepare(1, globals));
		Require(control.LastUpdatedCount() == 0 && control.LastMeshWriteCount() == 0, "native inactive zero-speed occurrence retains reset state without solver or pending mesh writes");
	}
	task.SetTickCount(0);
	task.Progress(0);
	Require(layer.Active(), "actual positive interval reactivates the same retained UID occurrence");
	const auto completed = Positions(*plain->Scene());
	for (std::size_t step = 0; step != 3; ++step) {
		control.Update(1, global.Prepare(1, globals));
		Require(control.LastUpdatedCount() == 1 && control.LastMeshWriteCount() <= 1, "real active zero-speed Bloom runs its solver/output and exact pending comparison");
		for (const auto& point : layer.CurrentMesh().Controls) {
			Require(std::isfinite(point.X) && std::isfinite(point.Y), "zero-time Bloom may never copy unordered coordinates into the pending finite mesh");
		}
		RequirePoints(Positions(*plain->Scene()), completed, "Bloom zero-time tail retains already-completed finite packet geometry");
	}
	Require(layer.CurrentMesh().EncodedDegree == 0, "second and later native unordered Bloom outputs compare equal to identity and clear the finite pending override");
	auto deformed = sgre::scene::IdentityMotionBezierLattice(0x33);
	deformed.Controls[0].X += 0.25f;
	Require(layer.SetMesh(1, &deformed, true), "zero-time fixture installs an actual finite nonidentity override");
	control.Update(1, global.Prepare(1, globals));
	Require(control.LastUpdatedCount() == 1 && control.LastMeshWriteCount() == 1 && layer.CurrentMesh().EncodedDegree == 0, "actual unordered comparison clears the real finite override without copying NaNs");
	control.Update(1, global.Prepare(1, globals));
	Require(control.LastUpdatedCount() == 1 && control.LastMeshWriteCount() == 0, "repeated unordered Bloom output preserves the genuinely empty override");
	task.SetTickCount(9);
	task.Progress(0);
	control.Update(1, global.Prepare(1, globals));
	Require(control.LastUpdatedCount() == 0, "native hidden test still precedes zero-time numeric work");
	task.SetTickCount(0);
	task.Progress(0);
	control.Update(1, global.Prepare(1, globals));
	Require(control.LastUpdatedCount() == 1, "reactivation resets the actual physical owner before its next zero-time step");
	task.Play("main", sgre::scene::MotionPlayFlags::Force);
	task.Progress(0);
	control.RequireReady();
	control.Update(1, global.Prepare(1, globals));
	Require(control.InstanceCount() == 1 && control.LastUpdatedCount() == 1, "real Force retires the old Bloom occurrence and creates one fresh owner");
	control.Close();
	fixture.Tasks.Clear();
}

static void TestZeroTimeSubstepsAndFixes() {
	for (std::size_t fixes = 0; fixes != 4; ++fixes) {
		Fixture fixture;
		auto task = sgre::scene::MotionPlayerTask::Attach(*fixture.Runtime, fixture.Tasks, false, "character", [](const auto&) {
		});
		task.SetVisible(true);
		task.Play("main");
		task.Progress(0);
		auto definition = ReadDefinition(BloomMetadata());
		definition.Physics.Speed = 0;
		definition.Physics.Substeps = 2;
		const std::array<sgre::resource::MotionPoint, 3> authored_fixes{{{0.2f, 0.3f}, {0.8f, 0.35f}, {0.6f, 0.9f}}};
		definition.Physics.Fixes.assign(authored_fixes.begin(), authored_fixes.begin() + static_cast<std::ptrdiff_t>(fixes));
		definition.Physics.CorrectFix = fixes != 0;
		definition.Physics.CorrectScale = true;
		definition.InheritShape = false;
		sgre::scene::EmoteBloomControl control(task.Commands(), definition);
		const auto layer = task.Commands().LayerUids().Matches("mesh-uid").front().Layer;
		layer.SetMesh(1, nullptr, false);
		const auto* plain = task.GenericPlayer();
		sgre::scene::EmoteVelvetyFrameDefinition frame_definition;
		frame_definition.PixelsPerMeter = 2;
		sgre::scene::EmoteVelvetyFrameOwner global(task.Root(), task.Commands(), *plain, frame_definition, 720);
		sgre::scene::EmoteVelvetyGlobalControls globals;
		globals.Flags = 0x200;
		const auto completed = Positions(*plain->Scene());
		for (std::size_t phase = 0; phase != 3; ++phase) {
			control.Update(1, global.Prepare(1, globals));
			Require(control.LastUpdatedCount() == 1 && control.LastMeshWriteCount() == 0 && layer.CurrentMesh().EncodedDegree == 0, "two native zero-time subdivisions propagate through all fix counts to an unordered identity no-op");
			RequirePoints(Positions(*plain->Scene()), completed, "unordered correction never replaces completed finite geometry");
		}
		control.Close();
		fixture.Tasks.Clear();
	}
}

static void TestOrdinaryZeroTimeBloomRemainsRejected() {
	auto definition = ReadDefinition(BloomMetadata()).Physics;
	for (const auto speed : std::array<float, 2>{0, 1}) {
		definition.Speed = speed;
		sgre::scene::EmoteBloomPhysics physics(definition);
		sgre::scene::EmotePhysicalPosture posture;
		physics.SetPosture(posture);
		physics.Initialize();
		RequireFailure([&] {
			physics.Update(speed == 0 ? 1 : 0);
		},
		    "positive elapsed ticks");
		for (const auto& point : physics.PrepareLattice().Controls) {
			Require(std::isfinite(point.X) && std::isfinite(point.Y), "ordinary zero-time rejection preserves the real initialized finite output owner");
		}
	}
}

static void TestCapturedNativeZeroBloomComparison() {
	for (const auto& capture : kNativeZeroBloomOutputs) {
		auto output = sgre::scene::IdentityMotionBezierLattice(0x33);
		bool finite = true;
		for (std::size_t index = 0; index != output.Controls.size(); ++index) {
			output.Controls[index] = {std::bit_cast<float>(capture.Words[index * 2]), std::bit_cast<float>(capture.Words[index * 2 + 1])};
			finite = finite && std::isfinite(output.Controls[index].X) && std::isfinite(output.Controls[index].Y);
		}
		sgre::resource::MotionBezierLattice current;
		const auto changed = sgre::scene::AssignNativeComparedMotionBezierLattice(current, output);
		Require(changed == finite && current.EncodedDegree == (finite ? 0x33 : 0), "captured whole Bloom output must preserve original finite-copy versus unordered-identity result");
		current = sgre::scene::IdentityMotionBezierLattice(0x33);
		current.Controls[0].X = 0.25f;
		Require(sgre::scene::AssignNativeComparedMotionBezierLattice(current, output) && current.EncodedDegree == (finite ? 0x33 : 0), "captured Bloom output clears a real finite override only under the original unordered comparison");
		if (!finite) {
			output.Controls.front().X = 0.125f;
			RequireFailure([&] {
				static_cast<void>(sgre::scene::AssignNativeComparedMotionBezierLattice(current, output));
			},
			    "finite");
		}
	}
}

int main() {
	try {
		TestMetadata();
		TestActualTailAndRetirement();
		TestMissingProducersAndAddedFailure();
		TestInactiveZeroSpeedActualUid();
		TestZeroTimeSubstepsAndFixes();
		TestOrdinaryZeroTimeBloomRemainsRejected();
		TestCapturedNativeZeroBloomComparison();
		std::cout << "Bloom actual metadata/UID/posture/physics/pending lattice/PartsTree/retirement PASS\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
