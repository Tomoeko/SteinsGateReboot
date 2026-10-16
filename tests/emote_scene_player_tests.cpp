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

static void TestCommonAssignmentKeepsDestinationOwnersAndPendingMesh() {
	Fixture fixture(OwnerDocument());
	std::size_t entropy_reads = 0;
	const auto entropy = [&] {
		return static_cast<std::uint32_t>(++entropy_reads);
	};
	const auto listener = [](const auto&) {
	};
	auto source = std::make_unique<sgre::scene::EmoteScenePlayer>(*fixture.Runtime, fixture.Project, entropy, listener);
	sgre::scene::EmoteScenePlayer destination(*fixture.Runtime, fixture.Project, entropy, listener);
	const ClearPublication publication{*fixture.Runtime};
	source->SetVisible(true);
	destination.SetVisible(true);
	source->PlayTimeline("phase");
	source->Progress(1);
	source->Progress(1);
	destination.Progress(0);
	Require(entropy_reads == 4 && fixture.Runtime->PlayerCount() == 2 && source->PhysicalCounts().Updated == 6, "assignment fixture must own two real enabled Common graphs and a genuinely advanced source");
	const std::array<std::string_view, 6> uids{"bloom-uid", "string-uid", "catenary-uid", "jelly-uid", "stretch-uid", "sync-uid"};
	std::vector<sgre::resource::MotionBezierLattice> expected;
	std::vector<sgre::scene::MotionLayerUidReference> destination_layers;
	for (const auto uid : uids) {
		const auto& source_matches = source->Commands().LayerUids().Matches(uid);
		const auto& target_matches = destination.Commands().LayerUids().Matches(uid);
		Require(source_matches.size() == 1 && target_matches.size() == 1 && !(source_matches.front().Layer == target_matches.front().Layer), "actual family assignments must begin with distinct source and destination UID cells");
		auto imported = target_matches.front().Layer.CurrentMesh();
		const auto& source_lattice = source_matches.front().Layer.CurrentMesh();
		// Native2e41a0 passes null when the source degree is zero.
		static_cast<void>(sgre::scene::AssignMotionBezierLattice(imported, source_lattice.EncodedDegree != 0 ? &source_lattice : nullptr, true));
		expected.push_back(std::move(imported));
		destination_layers.push_back(target_matches.front().Layer);
	}
	const auto source_tick = source->Commands().ClockState().TickCount;
	const auto destination_tick = destination.Commands().ClockState().TickCount;
	destination.AssignState(*source);
	Require(source->Commands().ClockState().TickCount == source_tick && destination.Commands().ClockState().TickCount == destination_tick && entropy_reads == 4, "zero-time assignment must not advance either actual plain clock or reseed the destination");
	Require(destination.PhysicalCounts().Updated == 0 && destination.PhysicalCounts().AttachedPartsControls == 6, "physical assignment imports state without inventing a solver step or replacing actual PartsTree attachments");
	for (std::size_t index = 0; index != uids.size(); ++index) {
		const auto& actual_layer = destination.Commands().LayerUids().Matches(uids[index]).front().Layer;
		Require(actual_layer == destination_layers[index], "physical assignment retains each exact destination occurrence binding");
		const auto& actual = actual_layer.CurrentMesh();
		Require(actual.EncodedDegree == expected[index].EncodedDegree && actual.Controls.size() == expected[index].Controls.size(), "native2e41a0 imports the actual source pending lattice shape");
		for (std::size_t point = 0; point != actual.Controls.size(); ++point) {
			Require(std::bit_cast<std::uint32_t>(actual.Controls[point].X) == std::bit_cast<std::uint32_t>(expected[index].Controls[point].X) && std::bit_cast<std::uint32_t>(actual.Controls[point].Y) == std::bit_cast<std::uint32_t>(expected[index].Controls[point].Y), "imported mesh controls must be copied exactly into independent destination storage");
		}
	}
	destination.Draw();
	Require(!destination.CanClose() && fixture.Runtime->SceneContext().Size() != 0, "the destination's completed geometry remains independently queueable before copied pending controls are consumed");
	RequireFailure([&] {
		destination.AssignState(*source);
	},
	    "unqueued");
	fixture.Runtime->SceneContext().Clear();
	source->Close();
	source.reset();
	Require(fixture.Runtime->PlayerCount() == 1 && fixture.Manager.Contains(fixture.Project), "source-first retirement keeps only the genuine destination player and its resource registration");
	destination.Progress(1);
	Require(destination.PhysicalCounts().Updated == 6 && entropy_reads == 4, "destination-owned models, collider leases and prediction history continue after source retirement");
	destination.Draw();
	fixture.Runtime->SceneContext().Clear();
	destination.Close();
	Require(fixture.Runtime->PlayerCount() == 0 && fixture.Manager.Contains(fixture.Project), "all assigned physical and UID owners retire before the enclosing resource registration");
}

static std::shared_ptr<sgre::resource::PsbDocument> AnimationQueryDocument(bool scalar_transitions) {
	auto document = OwnerDocument(false);
	auto& metadata = MutableField(document->Root, "metadata");
	auto& timelines = std::get<sgre::resource::PsbArray>(MutableField(metadata, "timelineControl").Data);
	auto steady = timelines.front();
	Set(steady, "label", Atom(std::string("steady")));
	Set(steady, "loopBegin", Atom(0.0));
	Set(steady, "loopEnd", Atom(4.0));
	timelines.push_back(steady);
	if (scalar_transitions) {
		Set(metadata, "immediateControl", Atom(sgre::resource::PsbArray{}));
		Set(metadata, "transitionControl", Atom(sgre::resource::PsbArray{Atom(sgre::resource::PsbObject{{"label", Atom(std::string("probe"))}, {"enabled", Atom(true)}}), Atom(sgre::resource::PsbObject{{"label", Atom(std::string("free"))}, {"enabled", Atom(true)}})}));
		Set(steady, "label", Atom(std::string("steady-free")));
		auto& tracks = std::get<sgre::resource::PsbArray>(MutableField(steady, "variableList").Data);
		Set(tracks.front(), "label", Atom(std::string("free")));
		timelines.push_back(std::move(steady));
	}

	return document;
}

static void TestAnimatingFiniteTimelineLoopAndBlendOwners() {
	Fixture fixture(AnimationQueryDocument(false));
	sgre::scene::EmoteScenePlayer owner(*fixture.Runtime, fixture.Project, [] {
		return std::uint32_t{1};
	},
	    [](const auto&) {
	    });
	const ClearPublication publication{*fixture.Runtime};
	Require(!owner.Animating(), "idle real scene has no transition or active timeline contribution");
	owner.PlayTimeline("phase");
	Require(owner.Animating(), "an active finite timeline counts even when every named variable is an immediate control");
	owner.Progress(5);
	Require(!owner.TimelinePlaying("phase") && !owner.Animating(), "finite timeline retirement removes its query contribution");
	owner.PlayTimeline("steady", 2);
	Require(owner.TimelinePlaying("steady") && !owner.Animating(), "steady loop activity alone is excluded by native LoopBegin semantics");
	owner.Progress(5);
	Require(owner.TimelinePlaying("steady") && !owner.Animating(), "crossing the actual loop boundary cannot turn steady looping into finite animation");
	owner.SetTimelineBlendRatio("steady", .25f, 2, 0);
	Require(owner.Animating(), "the active loop's real blend transition counts independently of perpetual timeline playback");
	owner.Progress(3);
	Require(owner.TimelinePlaying("steady") && owner.TimelineBlendRatio("steady") == .25f && !owner.Animating(), "completed blend returns to native steady-loop false while retaining playback");
	owner.StopTimeline();
	Require(!owner.Animating(), "stopping the actual active list leaves the real scene idle");
	owner.Close();
}

static void TestAnimatingTimelineNameUnionAndUncoveredScalar() {
	Fixture fixture(AnimationQueryDocument(true));
	sgre::scene::EmoteScenePlayer owner(*fixture.Runtime, fixture.Project, [] {
		return std::uint32_t{1};
	},
	    [](const auto&) {
	    });
	const ClearPublication publication{*fixture.Runtime};
	owner.PlayTimeline("steady");
	owner.SetVariable("probe", 12, 8, 0);
	const auto before = owner.Variables().TransitionValue("probe");
	Require(before.has_value() && !owner.Animating(), "a covered named scalar transition is suppressed by the actual active loop's track-name set");
	owner.Progress(1);
	const auto advanced = owner.Variables().TransitionValue("probe");
	Require(advanced && *advanced > *before && !owner.Animating(), "covered transition genuinely advances even though native animation query excludes it");
	owner.SetVariable("free", 16, 8, 0);
	Require(owner.Animating(), "a changing scalar outside the active loop's exact names remains visible to native animation query");
	owner.PlayTimeline("steady-free", 1);
	Require(owner.TimelinePlaying("steady") && owner.TimelinePlaying("steady-free") && !owner.Animating(), "query suppression uses the union of every active loop's actual track names");
	owner.StopTimeline("steady-free");
	Require(owner.Animating(), "removing one active label invalidates its cached name union and exposes the still-changing scalar");
	owner.StopTimeline();
	Require(owner.Animating(), "stopping all timelines cannot discard an unfinished uncovered control");
	owner.Progress(10);
	Require(!owner.Animating(), "genuine control completion leaves no named scalar or active timeline contribution");
	owner.Close();
}

static void TestPassSceneKeepsActualClocksAndPropertyControllers() {
	Fixture fixture(OwnerDocument(false));
	sgre::scene::EmoteScenePlayer owner(*fixture.Runtime, fixture.Project, [] {
		return std::uint32_t{1};
	},
	    [](const auto&) {
	    });
	const ClearPublication publication{*fixture.Runtime};
	owner.SetCoordinate({8, -4}, 8);
	owner.SetScale(2, 8);
	owner.PlayTimeline("phase");
	owner.Progress(1);
	const auto coordinate = owner.Coordinate();
	const auto scale = owner.Scale();
	const auto tick = owner.Commands().ClockState().TickCount;
	Require(owner.ReadVariable("probe") == 7 && owner.Commands().ParameterValues().Read("probe") == 7, "scene Pass must start with a real current cursor before the remaining key");
	owner.Pass();
	Require(!owner.TimelinePlaying("phase") && owner.ReadVariable("probe") == 9 && owner.Commands().ParameterValues().Read("probe") == 7, "scene Pass dispatches future visible keys while retaining deferred actual parameter publication");
	Require(owner.Coordinate() == coordinate && owner.Scale() == scale && owner.Commands().ClockState().TickCount == tick && owner.Animating(), "Pass does not finish active coordinate/scale components or step the selected plain clock");
	owner.Progress(0);
	Require(owner.Commands().ParameterValues().Read("probe") == 9 && owner.Coordinate() == coordinate && owner.Scale() == scale && owner.Commands().ClockState().TickCount == tick, "only the subsequent genuine containing zero phase publishes the stored Pass result");
	owner.Close();
}

static void TestPassSceneDifferenceFadeAndRepeatedCall() {
	auto document = OwnerDocument(false);
	auto& metadata = MutableField(document->Root, "metadata");
	auto& timelines = std::get<sgre::resource::PsbArray>(MutableField(metadata, "timelineControl").Data);
	Set(timelines.front(), "lastTime", Atom(40.0));
	Fixture fixture(document);
	sgre::scene::EmoteScenePlayer owner(*fixture.Runtime, fixture.Project, [] {
		return std::uint32_t{1};
	},
	    [](const auto&) {
	    });
	const ClearPublication publication{*fixture.Runtime};
	owner.PlayTimeline("phase", 2);
	owner.Progress(0);
	const auto tick = owner.Commands().ClockState().TickCount;
	owner.Pass();
	Require(owner.TimelinePlaying("phase") && owner.TimelineBlendRatio("phase") == 1 && owner.Commands().ClockState().TickCount == tick, "difference Pass starts actual fade0/20/0 without advancing or replacing the published blend cache");
	owner.Progress(10);
	Require(owner.TimelinePlaying("phase") && owner.TimelineBlendRatio("phase") == .5f, "genuine subsequent scheduler progression consumes the first half of native twenty-tick Pass fade");
	owner.Pass();
	Require(owner.TimelineBlendRatio("phase") == .5f, "repeated Pass on flag6 preserves the current blend instead of publishing or restarting a fade");
	owner.Progress(10);
	Require(!owner.TimelinePlaying("phase") && owner.TimelineBlendRatio("phase") == 0, "second half completes the original fade and stop policy; repeated Pass did not restart its duration");
	owner.Close();
}

int main() {
	try {
		TestPassSceneKeepsActualClocksAndPropertyControllers();
		TestPassSceneDifferenceFadeAndRepeatedCall();
		TestAnimatingFiniteTimelineLoopAndBlendOwners();
		TestAnimatingTimelineNameUnionAndUncoveredScalar();
		TestConnectedContainingOwnerAndNativePhases();
		TestAbsentDriverAndFailureRollback();
		TestCommonAssignmentKeepsDestinationOwnersAndPendingMesh();
		std::cout << "containing Emote scene/controls/physical ownership tests passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
