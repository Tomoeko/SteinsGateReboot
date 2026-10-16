#include "sgre/scene/emote_collision_owner.hpp"
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

static sgre::resource::PsbValue ControlMetadata() {
	const auto tress = Atom(sgre::resource::PsbObject{{"topXRate", Atom(0.5)}, {"topYRate", Atom(0.0)}, {"bottomXRate", Atom(0.5)}, {"bottomYRate", Atom(1.0)}});
	return Atom(sgre::resource::PsbObject{{"uid", Atom(std::string("mesh-uid"))}, {"ppm", Atom(2.0)}, {"boneDirection", Atom(std::int64_t{0})}, {"gravity", Atom(1.0)}, {"roughness", Atom(0.0)}, {"rigidMode", Atom(std::int64_t{0})}, {"sizeH", Atom(2.0)}, {"sizeV", Atom(4.0)}, {"originX", Atom(0.25)}, {"originY", Atom(0.75)}, {"splitLeft", Atom(0.5)}, {"splitRight", Atom(0.5)}, {"mass", Atom(1.0)}, {"numPerMeter", Atom(1.0)}, {"frictionTop", Atom(0.0)}, {"frictionBottom", Atom(0.0)}, {"elastic", Atom(0.0)}, {"straightness", Atom(0.0)}, {"coordScaleH", Atom(1.0)}, {"coordScaleV", Atom(1.0)}, {"coordScaleD", Atom(0.0)}, {"compressWiden", Atom(0.0)}, {"controlPointSize", Atom(0.0)}, {"adhocSim", Atom(false)}, {"tresses", Atom(sgre::resource::PsbArray{tress})}, {"rigidRange", Atom(sgre::resource::PsbObject{{"l", Atom(0.0)}, {"t", Atom(0.0)}, {"r", Atom(1.0)}, {"b", Atom(1.0)}})}, {"rigidIter", Atom(std::int64_t{0})}, {"fixes", Atom(sgre::resource::PsbArray{})}, {"coordPred", Atom(std::int64_t{3})}, {"collisionRefs", Atom(sgre::resource::PsbArray{})}, {"emissionRefs", Atom(sgre::resource::PsbArray{})}, {"speedVariables", Atom(sgre::resource::PsbArray{})}});
}

[[maybe_unused]] static sgre::scene::EmoteStringControlDefinition ReadDefinition(sgre::resource::PsbValue metadata) {
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

static std::shared_ptr<sgre::resource::PsbDocument> CollisionDocument(std::int64_t ignore_mask, bool matched) {
	auto document = Document();
	auto control = ControlMetadata();
	Set(control, "collisionHardness", Atom(std::int64_t{0}));
	Set(control, "collisionRepulsion", Atom(100.0));
	Set(control, "collisionRefs", Atom(sgre::resource::PsbArray{Atom(sgre::resource::PsbObject{{"uid", Atom(std::string(matched ? "ordinary-uid" : "missing-uid"))}, {"ignoreMask", Atom(ignore_mask)}})}));
	const auto ellipse = Atom(sgre::resource::PsbObject{{"type", Atom(std::int64_t{1})}, {"x", Atom(0.3)}, {"y", Atom(0.7)}, {"rh", Atom(100.0)}, {"rv", Atom(100.0)}, {"rot", Atom(0.0)}});
	const auto group = Atom(sgre::resource::PsbObject{{"colliders", Atom(sgre::resource::PsbArray{ellipse})}});
	Set(document->Root, "metadata", Atom(sgre::resource::PsbObject{{"velvetyCommonParams", Atom(sgre::resource::PsbObject{{"ppm", Atom(2.0)}})}, {"velvetyStringControl", Atom(sgre::resource::PsbArray{std::move(control)})}, {"collisionMap", Atom(sgre::resource::PsbObject{{"ordinary-uid", group}})}}));
	return document;
}

static void TestActualUidCollisionJoin(std::int64_t ignore_mask, bool matched) {
	auto document = CollisionDocument(ignore_mask, matched);
	Fixture fixture(document);
	auto task = sgre::scene::MotionPlayerTask::Attach(*fixture.Runtime, fixture.Tasks, false, "character", [](const auto&) {
	});
	sgre::scene::EmoteCollisionOwner collisions(task.Commands(), *fixture.Asset->Document);
	auto definitions = sgre::scene::ReadEmoteStringControlDefinitions(*fixture.Asset->Document);
	Require(definitions.size() == 1 && definitions[0].RequiresColliders && definitions[0].CollisionReferences.size() == 1, "actual resource metadata must preserve its collider UID and native ignore mask");
	sgre::scene::EmoteStringControl control(task.Commands(), std::move(definitions[0]), nullptr, &collisions);
	Require(control.InstanceCount() == 0 && collisions.GroupCount() == 0, "collisionMap groups stay lazy until an actual String UID instance exists");
	task.SetCoordinate({10, 20});
	task.SetVisible(true);
	task.Play("main");
	RequireFailure([&] {
		control.RequireReady();
	},
	    "FinishLayerChanges");
	Require(collisions.GroupCount() == 0, "noexcept per-node Added must not recursively mutate the actual UID observer registry");
	control.FinishLayerChanges();
	control.RequireReady();
	Require(control.InstanceCount() == 1 && collisions.GroupCount() == (matched ? 1 : 0), "completed actual UID transition creates only a resolved native collider group");
	auto group = matched ? collisions.Acquire("ordinary-uid") : std::shared_ptr<sgre::scene::EmoteCollisionGroup>{};
	Require(!matched || (group && group->ColliderCount() == 1), "resolved group owns its actual serialized collider shape");
	if (matched) {
		RequireFailure([&] {
			collisions.Close();
		},
		    "actual physical instance leases");
	}
	task.Progress(0);
	const auto* plain = task.GenericPlayer();
	Require(plain && plain->Scene(), "actual task owns the selected nested scene before collider input");
	sgre::scene::EmoteVelvetyFrameDefinition frame_definition;
	frame_definition.PixelsPerMeter = 2;
	sgre::scene::EmoteVelvetyFrameOwner global(task.Root(), task.Commands(), *plain, frame_definition, 720);
	sgre::scene::EmoteVelvetyGlobalControls controls;
	controls.Flags = 0x200;
	const auto frame = global.Prepare(1, controls);
	RequireNear(frame.WorldToPhysics.XX, 0.5f, "actual shared frame uses the real source ppm");
	RequireFailure([&] {
		control.Update(1, frame);
	},
	    "collider-pose phase once");
	collisions.Prepare(frame);
	const auto before = Positions(*plain->Scene());
	control.Update(1, frame);
	Require(control.LastUpdatedCount() == 1, "resolved or native-unmatched reference dispatches the actual physical owner");
	RequirePoints(Positions(*plain->Scene()), before, "collision tail output cannot mutate already completed scene geometry");
	if (matched) {
		Require(group->Contacted() == (ignore_mask == 0), "native ignoreMask changes genuine Soft attachment rather than bypassing collision owner creation");
	}
	RequireFailure([&] {
		control.Update(1, frame);
	},
	    "collider-pose phase once");
	task.Progress(0);
	Require(!Positions(*plain->Scene()).empty(), "next actual plain update consumes the pending collision-affected mesh");
	task.SetTickCount(8);
	task.Progress(0);
	const auto hidden = global.Prepare(1, controls);
	collisions.Prepare(hidden);
	control.Update(1, hidden);
	Require(control.LastUpdatedCount() == 0 && control.InstanceCount() == 1, "actual hidden selected UID retains the physical owner without projecting or publishing");
	task.SetTickCount(0);
	task.Progress(0);
	const auto resumed = global.Prepare(1, controls);
	collisions.Prepare(resumed);
	control.Update(1, resumed);
	Require(control.LastUpdatedCount() == 1, "native reactivation resets real collider interpolation and real String particles");
	task.Play("main", sgre::scene::MotionPlayFlags::Force);
	control.FinishLayerChanges();
	control.RequireReady();
	task.Progress(0);
	const auto replaced = global.Prepare(1, controls);
	collisions.Prepare(replaced);
	control.Update(1, replaced);
	Require(control.InstanceCount() == 1, "selected replacement retires old physical leases before using its actual new UID occurrence");
	control.Close();
	group.reset();
	collisions.Prepare(global.Prepare(1, controls));
	collisions.Close();
	Require(task.Commands().LayerUids().SubscriptionCount() == 0, "driver retirement releases actual anchor and String subscriptions before plain owner teardown");
	fixture.Tasks.Clear();
	Require(!task.IsLive() && fixture.Runtime->Identifiers().LiveCount() == 0, "actual containing task retires without any dangling collider/UID borrower");
}

static sgre::resource::PsbValue JellyVector(double x, double y, double z) {
	return Atom(sgre::resource::PsbObject{{"x", Atom(x)}, {"y", Atom(y)}, {"z", Atom(z)}});
}

static std::shared_ptr<sgre::resource::PsbDocument> JellyCollisionDocument(std::int64_t ignore_mask, bool matched) {
	auto document = CollisionDocument(ignore_mask, matched);
	const auto tress = Atom(sgre::resource::PsbObject{{"x", Atom(0.5)}, {"y", Atom(0.5)}, {"r", Atom(0.25)}, {"jellyTilt", JellyVector(0, 0, 1)}});
	sgre::resource::PsbObject fields;
	fields.emplace_back("uid", Atom(std::string("mesh-uid")));
	fields.emplace_back("ppm", Atom(2.0));
	fields.emplace_back("density", Atom(1.0));
	fields.emplace_back("sizeH", Atom(2.0));
	fields.emplace_back("sizeV", Atom(3.0));
	fields.emplace_back("num", Atom(std::int64_t{3}));
	fields.emplace_back("heightRate", Atom(0.5));
	fields.emplace_back("friction", Atom(0.1));
	fields.emplace_back("elasticH", Atom(0.25));
	fields.emplace_back("coordScaleH", Atom(1.0));
	fields.emplace_back("coordScaleV", Atom(1.0));
	fields.emplace_back("coordScaleD", Atom(1.0));
	fields.emplace_back("tipTilt", JellyVector(0, 0, 1));
	fields.emplace_back("rigidRange", Atom(sgre::resource::PsbObject{{"l", Atom(0.0)}, {"t", Atom(0.0)}, {"r", Atom(1.0)}, {"b", Atom(1.0)}}));
	fields.emplace_back("fixes", Atom(sgre::resource::PsbArray{}));
	fields.emplace_back("tresses", Atom(sgre::resource::PsbArray{tress}));
	fields.emplace_back("speedVariables", Atom(sgre::resource::PsbArray{}));
	fields.emplace_back("collisionRefs", Atom(sgre::resource::PsbArray{Atom(sgre::resource::PsbObject{{"uid", Atom(std::string(matched ? "ordinary-uid" : "missing-uid"))}, {"ignoreMask", Atom(ignore_mask)}})}));
	fields.emplace_back("emissionRefs", Atom(sgre::resource::PsbArray{}));
	auto* metadata = document->Root.Find("metadata");
	Require(metadata != nullptr, "actual Jelly collision fixture metadata absent");
	auto combined = *metadata;
	Set(combined, "velvetyJellyControl", Atom(sgre::resource::PsbArray{Atom(std::move(fields))}));
	Set(document->Root, "metadata", std::move(combined));
	return document;
}

static void TestActualJellyCollisionJoin(std::int64_t ignore_mask, bool matched) {
	auto document = JellyCollisionDocument(ignore_mask, matched);
	Fixture fixture(document);
	auto task = sgre::scene::MotionPlayerTask::Attach(*fixture.Runtime, fixture.Tasks, false, "character", [](const auto&) {
	});
	sgre::scene::EmoteCollisionOwner collisions(task.Commands(), *fixture.Asset->Document);
	auto definitions = sgre::scene::ReadEmoteJellyControlDefinitions(*fixture.Asset->Document);
	Require(definitions.size() == 1 && definitions[0].CollisionReferences.size() == 1, "actual resource metadata must preserve its collider UID and native ignore mask");
	sgre::scene::EmoteJellyControl control(task.Commands(), std::move(definitions[0]), nullptr, &collisions);
	Require(control.InstanceCount() == 0 && collisions.GroupCount() == 0, "collisionMap groups stay lazy until an actual Jelly UID instance exists");
	task.SetCoordinate({10, 20});
	task.SetVisible(true);
	task.Play("main");
	RequireFailure([&] {
		control.RequireReady();
	},
	    "FinishLayerChanges");
	Require(collisions.GroupCount() == 0, "noexcept per-node Added must not recursively mutate the actual UID observer registry");
	control.FinishLayerChanges();
	control.RequireReady();
	Require(control.InstanceCount() == 1 && collisions.GroupCount() == (matched ? 1 : 0), "completed actual UID transition creates only a resolved native collider group");
	auto group = matched ? collisions.Acquire("ordinary-uid") : std::shared_ptr<sgre::scene::EmoteCollisionGroup>{};
	Require(!matched || (group && group->ColliderCount() == 1), "resolved group owns its actual serialized collider shape");
	if (matched) {
		RequireFailure([&] {
			collisions.Close();
		},
		    "actual physical instance leases");
	}
	task.Progress(0);
	const auto* plain = task.GenericPlayer();
	Require(plain && plain->Scene(), "actual task owns the selected nested scene before collider input");
	sgre::scene::EmoteVelvetyFrameDefinition frame_definition;
	frame_definition.PixelsPerMeter = 2;
	sgre::scene::EmoteVelvetyFrameOwner global(task.Root(), task.Commands(), *plain, frame_definition, 720);
	sgre::scene::EmoteVelvetyGlobalControls controls;
	controls.Flags = 0x200;
	const auto frame = global.Prepare(1, controls);
	RequireNear(frame.WorldToPhysics.XX, 0.5f, "actual shared frame uses the real source ppm");
	RequireFailure([&] {
		control.Update(1, frame);
	},
	    "collider-pose phase once");
	collisions.Prepare(frame);
	const auto before = Positions(*plain->Scene());
	control.Update(1, frame);
	Require(control.LastUpdatedCount() == 1, "resolved or native-unmatched reference dispatches the actual physical owner");
	RequirePoints(Positions(*plain->Scene()), before, "collision tail output cannot mutate already completed scene geometry");
	if (matched) {
		Require(group->Contacted() == (ignore_mask == 0), "native ignoreMask changes genuine Bobble attachment rather than bypassing collision owner creation");
	}
	RequireFailure([&] {
		control.Update(1, frame);
	},
	    "collider-pose phase once");
	task.Progress(0);
	Require(!Positions(*plain->Scene()).empty(), "next actual plain update consumes the pending collision-affected mesh");
	task.SetTickCount(8);
	task.Progress(0);
	const auto hidden = global.Prepare(1, controls);
	collisions.Prepare(hidden);
	control.Update(1, hidden);
	Require(control.LastUpdatedCount() == 0 && control.InstanceCount() == 1, "actual hidden selected UID retains the physical owner without projecting or publishing");
	task.SetTickCount(0);
	task.Progress(0);
	const auto resumed = global.Prepare(1, controls);
	collisions.Prepare(resumed);
	control.Update(1, resumed);
	Require(control.LastUpdatedCount() == 1, "native reactivation resets real collider interpolation and real Jelly particles");
	task.Play("main", sgre::scene::MotionPlayFlags::Force);
	control.FinishLayerChanges();
	control.RequireReady();
	task.Progress(0);
	const auto replaced = global.Prepare(1, controls);
	collisions.Prepare(replaced);
	control.Update(1, replaced);
	Require(control.InstanceCount() == 1, "selected replacement retires old physical leases before using its actual new UID occurrence");
	control.Close();
	group.reset();
	collisions.Prepare(global.Prepare(1, controls));
	collisions.Close();
	Require(task.Commands().LayerUids().SubscriptionCount() == 0, "driver retirement releases actual anchor and Jelly subscriptions before plain owner teardown");
	fixture.Tasks.Clear();
	Require(!task.IsLive() && fixture.Runtime->Identifiers().LiveCount() == 0, "actual containing task retires without any dangling collider/UID borrower");
}

int main() {
	try {
		TestActualUidCollisionJoin(0, true);
		TestActualUidCollisionJoin(-1, true);
		TestActualUidCollisionJoin(0, false);
		TestActualJellyCollisionJoin(0, true);
		TestActualJellyCollisionJoin(-1, true);
		TestActualJellyCollisionJoin(0, false);
		std::cout << "Actual String+Jelly/collisionMap/UID/Soft+Bobble owner join, ignored/unmatched refs and retained selected transitions passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
