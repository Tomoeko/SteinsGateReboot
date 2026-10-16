#include "sgre/scene/emote_scene_player.hpp"

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

static sgre::resource::PsbValue BloomMetadata() {
	return Atom(sgre::resource::PsbObject{{"uid", Atom(std::string("mesh-uid"))}, {"ppm", Atom(2.0)}, {"sizeH", Atom(2.0)}, {"sizeV", Atom(4.0)}, {"originX", Atom(0.4)}, {"originY", Atom(0.55)}, {"centerX", Atom(0.5)}, {"centerY", Atom(0.5)}, {"mass", Atom(0.4)}, {"defaultRot", Atom(0.15)}, {"limitLengthRatio", Atom(2.0)}, {"elastic", Atom(2.5)}, {"friction", Atom(0.25)}, {"straightness", Atom(0.25)}, {"rotateWeight", Atom(0.4)}, {"twistWeight", Atom(0.3)}, {"trapeziumWeight", Atom(0.2)}, {"pitchWeight", Atom(0.1)}, {"coordScaleH", Atom(0.75)}, {"coordScaleV", Atom(1.25)}, {"coordScaleD", Atom(0.1)}, {"coordPred", Atom(std::int64_t{3})}, {"tresses", Atom(sgre::resource::PsbArray{Atom(sgre::resource::PsbObject{{"x", Atom(0.2)}, {"y", Atom(0.2)}})})}, {"fixes", Atom(sgre::resource::PsbArray{})}, {"speedVariables", Atom(sgre::resource::PsbArray{})}, {"collisionRefs", Atom(sgre::resource::PsbArray{})}, {"emissionRefs", Atom(sgre::resource::PsbArray{})}});
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

static sgre::resource::PsbValue& MutableField(sgre::resource::PsbValue& object, std::string_view name) {
	for (auto& [key, value] : std::get<sgre::resource::PsbObject>(object.Data)) {
		if (key == name) {
			return value;
		}
	}
	throw std::runtime_error("missing actual fixture field");
}

static std::shared_ptr<sgre::resource::PsbDocument> OwnerDocument(bool common = true) {
	auto document = Document();
	const std::array<std::string, 6> uids{"bloom-uid", "string-uid", "catenary-uid", "jelly-uid", "stretch-uid", "sync-uid"};
	auto& timeline = MutableField(MutableField(MutableField(MutableField(document->Root, "object"), "character"), "motion"), "main");
	sgre::resource::PsbArray layers, indices, parts;
	for (std::size_t index = 0; index != uids.size(); ++index) {
		auto layer = Layer(true);
		Set(layer, "uid", Atom(uids[index]));
		Set(layer, "label", Atom(uids[index]));
		Set(layer, "children", Atom(sgre::resource::PsbArray{}));
		layers.push_back(std::move(layer));
		indices.push_back(Atom(static_cast<std::int64_t>(index)));
		parts.push_back(Atom(sgre::resource::PsbObject{{"uid", Atom(uids[index])}, {"label", Atom(uids[index])}}));
	}
	Set(timeline, "layer", Atom(std::move(layers)));
	auto& priorities = std::get<sgre::resource::PsbArray>(MutableField(timeline, "priority").Data);
	Set(priorities.front(), "content", Atom(std::move(indices)));
	const auto active_timeline = Atom(sgre::resource::PsbObject{{"label", Atom(std::string("phase"))}, {"loopBegin", Atom(-1.0)}, {"loopEnd", Atom(-1.0)}, {"lastTime", Atom(4.0)}, {"variableList", Atom(sgre::resource::PsbArray{Atom(sgre::resource::PsbObject{{"label", Atom(std::string("probe"))}, {"frameList", Atom(sgre::resource::PsbArray{Atom(sgre::resource::PsbObject{{"time", Atom(0.0)}, {"type", Atom(std::int64_t{1})}, {"content", Atom(sgre::resource::PsbObject{{"value", Atom(7.0)}, {"easing", Atom(0.0)}})}}), Atom(sgre::resource::PsbObject{{"time", Atom(2.0)}, {"type", Atom(std::int64_t{1})}, {"content", Atom(sgre::resource::PsbObject{{"value", Atom(9.0)}, {"easing", Atom(0.0)}})}}), Atom(sgre::resource::PsbObject{{"time", Atom(4.0)}, {"type", Atom(std::int64_t{0})}})})}})})}});
	const auto empty = [] {
		return Atom(sgre::resource::PsbArray{});
	};
	sgre::resource::PsbObject fields{
	    {"base", Atom(sgre::resource::PsbObject{{"chara", Atom(std::string("character"))}, {"motion", Atom(std::string("main"))}})},
	    {"mirror", Atom(false)}, {"scale", Atom(2.0)},
	    {"charaProfile", Atom(sgre::resource::PsbObject{{"height", Atom(171.0)}, {"pixelMarker", Atom(sgre::resource::PsbObject{})}})},
	    {"immediateControl", Atom(sgre::resource::PsbArray{Atom(sgre::resource::PsbObject{{"label", Atom(std::string("probe"))}, {"enabled", Atom(true)}})})}, {"variableList", empty()}, {"transitionControl", empty()}, {"eyeControl", empty()}, {"eyebrowControl", empty()}, {"mouthControl", empty()},
	    {"bustControl", empty()}, {"hairControl", empty()}, {"partsControl", empty()}, {"clampControl", empty()}, {"timelineControl", Atom(sgre::resource::PsbArray{active_timeline})},
	    {"celGroup", Atom(sgre::resource::PsbObject{{"groups", empty()}})}, {"collisionMap", Atom(sgre::resource::PsbObject{})},
	    {"emissionMap", Atom(sgre::resource::PsbObject{{"auto", empty()}})}, {"markerList", empty()}};
	if (common) {
		fields.emplace_back("velvetyCommonParams", Atom(sgre::resource::PsbObject{{"ppm", Atom(2.0)}}));
		fields.emplace_back("velvetyPartsTree", Atom(std::move(parts)));
		auto bloom = BloomMetadata();
		Set(bloom, "uid", Atom(uids[0]));
		fields.emplace_back("velvetyBloomControl", Atom(sgre::resource::PsbArray{std::move(bloom)}));
		auto string = ControlMetadata();
		Set(string, "uid", Atom(uids[1]));
		fields.emplace_back("velvetyStringControl", Atom(sgre::resource::PsbArray{std::move(string)}));
		auto catenary = CatenaryMetadata();
		Set(catenary, "uid", Atom(uids[2]));
		fields.emplace_back("velvetyCatenaryControl", Atom(sgre::resource::PsbArray{std::move(catenary)}));
		auto jelly = JellyMetadata();
		Set(jelly, "uid", Atom(uids[3]));
		Set(jelly, "ppm", Atom(2.0));
		fields.emplace_back("velvetyJellyControl", Atom(sgre::resource::PsbArray{std::move(jelly)}));
		const auto stretch = Atom(sgre::resource::PsbObject{{"uid", Atom(uids[4])}, {"ppm", Atom(2.0)}, {"sizeH", Atom(2.0)}, {"sizeV", Atom(3.0)}, {"originX", Atom(.25)}, {"originY", Atom(.75)}, {"fixes", empty()}, {"inheritShape", Atom(true)}});
		fields.emplace_back("velvetyStretchControl", Atom(sgre::resource::PsbArray{stretch}));
		const auto sync = Atom(sgre::resource::PsbObject{{"uid", Atom(uids[5])}, {"sourceUid", Atom(uids[1])}, {"ppm", Atom(2.0)}, {"sizeH", Atom(2.0)}, {"sizeV", Atom(3.0)}, {"originX", Atom(.25)}, {"originY", Atom(.75)}, {"weight", Atom(.5)}, {"weightHorizontal", Atom(.8)}, {"weightVertical", Atom(.6)}, {"fixes", empty()}, {"inheritShape", Atom(true)}});
		fields.emplace_back("velvetySynchronizeControl", Atom(sgre::resource::PsbArray{sync}));
	}
	Set(document->Root, "metadata", Atom(std::move(fields)));
	return document;
}

struct ClearPublication {
	sgre::scene::MotionManagerRuntime& Runtime;
	~ClearPublication() {
		Runtime.SceneContext().Clear();
	}
};

static void TestConnectedContainingOwnerAndNativePhases() {
	Fixture fixture(OwnerDocument());
	std::size_t entropy_reads = 0;
	sgre::scene::EmoteScenePlayer owner(*fixture.Runtime, fixture.Project, [&] {
		return static_cast<std::uint32_t>(++entropy_reads);
	},
	    [](const auto&) {
	    });
	const ClearPublication publication{*fixture.Runtime};
	Require(entropy_reads == 2 && fixture.Runtime->PlayerCount() == 1, "actual Eye stream and separate Emission stream consume exactly two real words, with one distinct inner plain player");
	Require(owner.MetadataScale() == 2 && owner.Root().Overrides().ZoomX == 1 && !owner.Root().Overrides().Visible, "metadata calibration must not invent zoom or visible pose");
	const auto initialized = owner.PhysicalCounts();
	Require(initialized.Bloom == 1 && initialized.String == 1 && initialized.Catenary == 1 && initialized.Jelly == 1 && initialized.Stretch == 1 && initialized.Synchronize == 1 && initialized.AttachedPartsControls == 6, "real registered metadata creates all six actual UID owner families and attaches their typed PartsTree callbacks");
	Require(owner.Commands().LayerUids().Matches("string-uid").size() == 1 && owner.PlainPlayer().Scene() != nullptr, "all controls share this genuine primary selected scene while retaining their distinct UID targets");
	Require(!owner.TimelinePlaying(), "actual timeline catalogue remains inactive until explicit playback");
	owner.PlayTimeline("phase");
	Require(owner.TimelinePlaying("phase") && owner.ReadVariable("probe") == 7, "real containing play seeks actual track through the ordinary variable owner");
	owner.Progress(0);
	Require(owner.Commands().ParameterValues().Read("probe") == 7, "actual zero-delta plain progress receives scheduled initial variable publication");
	Require(owner.PhysicalCounts().Updated == 0 && !owner.Root().Overrides().Visible, "native zero-delta initialization publishes controls/plain without a fabricated physics step or visibility override");
	owner.SetVisible(true);
	const auto clock_before = owner.Commands().ClockState().TickCount;
	owner.Progress(1);
	const auto updated = owner.PhysicalCounts();
	Require(updated.Updated == 6 && updated.MeshWrites <= 6, "one actual plain Progress is followed by the six real physical callbacks, not a test-only producer");
	Require(owner.Commands().ClockState().TickCount == clock_before + 1 && entropy_reads == 2, "physics performs no second plain update and never reseeds constructor streams");
	owner.Draw();
	Require(fixture.Runtime->SceneContext().Size() != 0 && !owner.CanClose(), "actual manager publication borrows the enclosing selected scene through GPU/input retirement");
	RequireFailure([&] {
		owner.Progress(1);
	},
	    "unqueued");
	fixture.Runtime->SceneContext().Clear();
	owner.Progress(0);
	owner.Progress(1);
	Require(owner.ReadVariable("probe") == 9 && owner.Commands().ParameterValues().Read("probe") == 9, "active scheduler crosses before real channel publication and one actual plain update");
	owner.StopTimeline();
	Require(!owner.TimelinePlaying(), "containing stop retires active scheduling without erasing retained plain values");
	owner.SetVisible(false);
	owner.Progress(1);
	Require(owner.PhysicalCounts().Updated == 0, "native visibility false retains real instances without publishing active physical output");
	owner.Close();
	Require(owner.IsClosed() && fixture.Runtime->PlayerCount() == 0 && fixture.Manager.Contains(fixture.Project), "real controls/UID subscriptions retire before the distinct plain/runtime borrow, without unregistering Layer resources");
}

static void TestAbsentDriverAndFailureRollback() {
	{
		Fixture fixture(OwnerDocument(false));
		std::size_t entropy_reads = 0;
		sgre::scene::EmoteScenePlayer owner(*fixture.Runtime, fixture.Project, [&] {
			return static_cast<std::uint32_t>(++entropy_reads);
		},
		    [](const auto&) {
		    });
		Require(entropy_reads == 1 && owner.PhysicalCounts().String == 0, "absent Common follows native disabled driver and does not invent emission entropy/pools");
		owner.Progress(1);
		owner.Close();
	}
	{
		auto document = OwnerDocument();
		auto metadata = *document->Root.Find("metadata");
		Set(metadata, "markerList", Atom(sgre::resource::PsbArray{Atom(std::string("unrecovered-marker"))}));
		Set(document->Root, "metadata", std::move(metadata));
		Fixture fixture(document);
		RequireFailure([&] {
			sgre::scene::EmoteScenePlayer owner(*fixture.Runtime, fixture.Project, [] {
				return std::uint32_t{1};
			},
			    [](const auto&) {
			    });
		},
		    "actual marker UID producer");
		Require(fixture.Runtime->PlayerCount() == 0 && fixture.Runtime->SceneContext().Size() == 0 && fixture.Manager.Contains(fixture.Project), "failed mandatory-producer admission retires actual partial plain/UID graph and keeps registered resource identity");
	}
}

int main() {
	try {
		TestConnectedContainingOwnerAndNativePhases();
		TestAbsentDriverAndFailureRollback();
		std::cout << "containing Emote scene/controls/physical ownership tests passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
