#include "sgre/scene/emote_catenary_control.hpp"

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

static sgre::resource::PsbValue CatenaryMetadata() {
	const auto point = [](double x, double y) {
		return Atom(sgre::resource::PsbObject{{"x", Atom(x)}, {"y", Atom(y)}});
	};
	return Atom(sgre::resource::PsbObject{
	    {"uid", Atom(std::string("mesh-uid"))}, {"ppm", Atom(2.0)},
	    {"sizeH", Atom(2.0)}, {"sizeV", Atom(3.0)}, {"originX", Atom(.3)}, {"originY", Atom(.6)},
	    {"mass", Atom(.8)}, {"defaultRot", Atom(.15)}, {"gravity", Atom(2.5)}, {"gravityDirection", Atom(40.0)},
	    {"lengthRatio", Atom(1.25)}, {"numPerMeter", Atom(1.0)},
	    {"frictionTop", Atom(.15)}, {"frictionBottom", Atom(.3)}, {"bottomRatio", Atom(.4)},
	    {"elastic", Atom(.05)}, {"straightness", Atom(.25)}, {"deformIter", Atom(std::int64_t{3})},
	    {"coordScaleH", Atom(.9)}, {"coordScaleV", Atom(1.1)}, {"coordScaleD", Atom(1.0)}, {"coordPred", Atom(std::int64_t{3})},
	    {"fixes", Atom(sgre::resource::PsbArray{point(.2, .15), point(.8, .65), point(99, 99)})},
	    {"points", Atom(sgre::resource::PsbArray{Atom(.2), Atom(.15), Atom(.5), Atom(.5), Atom(.8), Atom(.65)})},
	    {"speedVariables", Atom(sgre::resource::PsbArray{})}, {"collisionRefs", Atom(sgre::resource::PsbArray{})}, {"emissionRefs", Atom(sgre::resource::PsbArray{})}});
}

static sgre::scene::EmoteCatenaryControlDefinition ReadDefinition(sgre::resource::PsbValue value) {
	sgre::resource::PsbDocument document;
	document.Root = Atom(sgre::resource::PsbObject{{"metadata", Atom(sgre::resource::PsbObject{{"velvetyCommonParams", Atom(sgre::resource::PsbObject{})}, {"velvetyCatenaryControl", Atom(sgre::resource::PsbArray{std::move(value)})}})}});
	auto definitions = sgre::scene::ReadEmoteCatenaryControlDefinitions(document);
	Require(definitions.size() == 1, "actual Catenary definition retains serialized control ownership");
	return std::move(definitions.front());
}

static void TestMetadata() {
	auto metadata = CatenaryMetadata();
	auto definition = ReadDefinition(metadata);
	RequireNear(definition.Physics.SizeHorizontal, 2, "physical dimensions remain physical; ppm belongs to UID conversion");
	Require(definition.Physics.Substeps == 1 && definition.Physics.Speed == 1 && definition.InheritShape && !definition.Physics.CorrectFix && !definition.Physics.CorrectScale, "native absent defaults/inherit policy remain distinct");
	Require(definition.Physics.ParticleCount == 3 && definition.Physics.RestPoints.size() == 3 && definition.Physics.Fixes[1].X == .8f, "native density truncation owns3particles and reads only the first2fixes");
	RequireNear(definition.Physics.GravityRotation, 40.0f * .0174532924f + .15f, "actual gravityDirection degrees is combined with defaultRot");
	RequireNear(definition.Physics.Length, 1.25f * std::sqrt(1.5f * 1.5f + 1.2f * 1.2f), "actual rest length derives only from the two fixes and ratio");
	Set(metadata, "points", Atom(sgre::resource::PsbArray{Atom(std::string("unused mismatched points"))}));
	Require(ReadDefinition(metadata).Physics.RestPoints.empty(), "native mismatched points extent is discarded without decoding unused entries");
	Set(metadata, "autoMass", Atom(true));
	Set(metadata, "imageArea", Atom(12.0));
	Set(metadata, "massPerSquareMeter", Atom(.25));
	Set(metadata, "mass", Atom(99.0));
	Set(metadata, "inheritShape", Atom(false));
	Set(metadata, "correctFix", Atom(true));
	Set(metadata, "correctScale", Atom(true));
	definition = ReadDefinition(metadata);
	RequireNear(definition.Physics.Mass, .75f, "native automatic mass uses actual imageArea*density/(ppm*ppm)");
	Require(definition.Physics.CorrectFix && definition.Physics.CorrectScale, "only actual noninherit enables correction phases");
	Set(metadata, "autoMass", Atom(false));
	RequireNear(ReadDefinition(metadata).Physics.Mass, 99, "presentfalse autoMass reads actual manual mass");
	Set(metadata, "mass", Atom(std::string("invalid")));
	RequireFailure([&] {
		static_cast<void>(ReadDefinition(metadata));
	},
	    "native numeric metadata");
	sgre::resource::PsbDocument absent;
	absent.Root = Atom(sgre::resource::PsbObject{{"metadata", Atom(sgre::resource::PsbObject{})}});
	Require(sgre::scene::ReadEmoteCatenaryControlDefinitions(absent).empty(), "absent common owner creates no Catenary declaration");
}

static std::vector<sgre::resource::MotionPoint> Positions(const sgre::scene::MotionSceneEvaluation& scene) {
	for (std::size_t index = 0; index != scene.PacketRows().size(); ++index) {
		if (scene.PacketRows()[index]->Node().Raw->Label == "mesh") {
			const auto& command = scene.Packets().Nodes()[index].Command;
			Require(command.Geometry && !command.Hidden, "actual target retains completed geometry");
			return {command.Geometry->Positions.begin(), command.Geometry->Positions.end()};
		}
	}
	throw std::runtime_error("actual Catenary target absent");
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
	sgre::scene::EmoteCatenaryControl declaration(task.Commands(), ReadDefinition(CatenaryMetadata()));
	Require(declaration.InstanceCount() == 0, "control declaration does not invent a selected UID before Play");
	declaration.Close();
	task.SetVisible(true);
	task.Play("main");
	task.Progress(0);
	const auto& matches = task.Commands().LayerUids().Matches("mesh-uid");
	Require(matches.size() == 1, "genuine Added creates the actual owned Catenary point solver");
	const auto layer = matches.front().Layer;
	layer.InitializeMeshOrigin({0.125f, -0.25f}, {0.75f, 0.625f});
	task.Progress(0);
	const auto mesh_origin = layer.MeshPhysicalOrigin();
	sgre::scene::EmoteCatenaryControl control(task.Commands(), ReadDefinition(CatenaryMetadata()));
	Require(control.InstanceCount() == 1, "late actual Added constructs one genuine Catenary model");
	RequireNear(layer.MeshPhysicalOrigin().X, -.4f, "native2eec00 initializes actual physical mesh origin X");
	RequireNear(layer.MeshPhysicalOrigin().Y, .3f, "native2eec00 initializes actual physical mesh origin Y");
	Require(layer.MeshPhysicalOrigin().X != mesh_origin.X, "actual Catenary origin initialization differs from retained previous owner state");
	task.Progress(0);
	const auto* plain = task.GenericPlayer();
	Require(plain && plain->Scene(), "Catenary consumes the true selected scene under its actual task");
	sgre::scene::EmoteVelvetyFrameDefinition frame_definition;
	frame_definition.PixelsPerMeter = 2;
	sgre::scene::EmoteVelvetyFrameOwner global(task.Root(), task.Commands(), *plain, frame_definition, 720);
	sgre::scene::EmoteVelvetyGlobalControls globals;
	globals.Flags = 0x200;
	sgre::scene::EmotePartsTree tree(task.Commands(), Atom(sgre::resource::PsbArray{Atom(sgre::resource::PsbObject{{"uid", Atom(std::string("mesh-uid"))}})}));
	Require(tree.AttachCatenary(control) && tree.CatenaryCount() == 1, "native tree indexes and borrows the real typed Catenary callback");
	for (std::size_t frame = 0; frame != 3; ++frame) {
		if (frame != 0) {
			task.Progress(1);
		}
		const auto before = Positions(*plain->Scene());
		tree.UpdateControls(1, global.Prepare(1, globals));
		Require(control.LastUpdatedCount() == 1 && control.LastMeshWriteCount() <= 1, "one real active UID advances once after actual plain publication");
		RequirePoints(Positions(*plain->Scene()), before, "Catenary tail sets pending mesh without overwriting completed packet geometry or running another plain step");
		RequireNear(layer.MeshPhysicalOrigin().X, -.4f, "actual origin remains stable across physical updates");
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
	auto definition = ReadDefinition(CatenaryMetadata());
	definition.RequiresEmissions = true;
	RequireFailure([&] {
		sgre::scene::EmoteCatenaryControl invalid(task.Commands(), definition);
	},
	    "actual emission force owner");
	definition.RequiresEmissions = false;
	definition.CollisionReferences.push_back({"actual-collision", 0});
	RequireFailure([&] {
		sgre::scene::EmoteCatenaryControl invalid(task.Commands(), definition);
	},
	    "unmasked base-collider");
	Require(task.Commands().LayerUids().SubscriptionCount() == 0, "unimplemented required producer rejects before UID mutation");
	definition.CollisionReferences.clear();

	auto unmatched = definition;
	unmatched.Uid = "unmatched-catenary";
	unmatched.Physics.Speed = 0;
	sgre::scene::EmoteCatenaryControl absent(task.Commands(), unmatched);
	absent.Update(1, {});
	Require(absent.InstanceCount() == 0 && absent.LastUpdatedCount() == 0, "zero-speed unmatched declaration does not invent a physical update");
	absent.Close();
	definition.Physics.Mass = 0;
	sgre::scene::EmoteCatenaryControl control(task.Commands(), definition);
	task.Play("main");
	RequireFailure([&] {
		control.RequireReady();
	},
	    "actual dimensions/mass");
	Require(control.InstanceCount() == 0, "noexcept Added retains first construction failure without a partial point owner");
	control.Close();
	fixture.Tasks.Clear();
}
int main() {
	try {
		TestMetadata();
		TestActualTailAndRetirement();
		TestMissingProducersAndAddedFailure();
		std::cout << "Catenary actual metadata/UID/posture/physics/pending lattice/PartsTree/retirement PASS\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
