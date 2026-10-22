#include "sgre/script/emote_player_bindings.hpp"

#include "sgre/script/motion_resource_owner.hpp"
#include "sgre/script/psb_bindings.hpp"

#include "sgre/scene/emote_color_transition.hpp"
#include "sgre/scene/emote_coordinate_transition.hpp"
#include "sgre/scene/emote_parts_tree.hpp"
#include "sgre/scene/emote_rotation_transition.hpp"
#include "sgre/scene/emote_scale_transition.hpp"
#include "sgre/scene/emote_wind.hpp"

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
#include <fstream>
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

	throw std::runtime_error("actual Emote binding accepted an invalid owner or input");
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

static sgre::script::SquirrelVmLimits Limits() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	return limits;
}

class Fixture {
public:
	explicit Fixture(bool common = true, std::string_view legacy_family = {}, sgre::scene::EmoteWindRandom* wind_random = nullptr, std::shared_ptr<sgre::resource::PsbDocument> document = {}) : Vm(Limits()), Objects(Vm), Psb(Vm, Objects), Cache([this](auto, const auto&) {
		                                                                                                                                                                                             return sgre::render::SpriteTextureHandle{static_cast<std::uint32_t>(++Uploads), 1, &Uploads};
	                                                                                                                                                                                             },
	                                                                                                                                                                                                                                              [this](auto) {
		                                                                                                                                                                                                                                              ++Releases;
	                                                                                                                                                                                                                                              }),
	                                                                                                                                                                                             Assets(Cache), Tree(8), Layers(Vm, Objects, Tree), Clock(0) {
		Vm.InstallRecoveredBaseLibrary();
		Layers.ConfigureMotionOwners(Psb, Assets, Cache);
		Leaf = Vm.Invoke(Layers.LayerClass(), std::array<sgre::script::SquirrelValue, 1>{Vm.RootTable()}).Value;
		LeafRoot = Vm.Retain(Leaf);
		LeafHandle = Layers.Handle(Leaf);
		Vm.ResetExecution();
		for (std::size_t index = 0; index < Seed.size(); ++index) {
			Seed[index] = static_cast<std::uint32_t>(index) * 0x9e3779b9u + 17;
		}
		auto owner_document = document ? std::move(document) : OwnerDocument(common);
		if (!legacy_family.empty()) {
			Set(MutableField(owner_document->Root, "metadata"), std::string(legacy_family), Atom(sgre::resource::PsbArray{Atom(sgre::resource::PsbObject{{"enabled", Atom(true)}})}));
		}
		PsbObject = Psb.WrapDocument(std::move(owner_document), std::make_shared<const std::vector<std::uint8_t>>(144, 255));
		Project.Value = std::get<std::int32_t>(Call(Leaf, "registerMotionResource", {PsbObject}));
		sgre::scene::MotionManagerRuntimeServices services;
		services.SceneAssets = &Assets;
		services.ReadRegisteredAsset = [this](auto project) -> const sgre::scene::LoadedMotionAsset& {
			return Layers.MotionResources(Leaf).BorrowAsset(project);
		};
		services.ReadSourceAliases = [this](auto project) {
			return Layers.MotionResources(Leaf).SourceAliases(project);
		};
		Primary = std::make_unique<sgre::scene::MotionManagerRuntime>(Layers.MotionResources(Leaf).Primary(), Clock, Seed, services, 32);
		Secondary = std::make_unique<sgre::scene::MotionManagerRuntime>(Layers.MotionResources(Leaf).Secondary(), Clock, Seed, services, 32);
		Layers.AttachMotionRuntime(Leaf, *Primary);
		Layers.AttachMotionRuntime(Leaf, *Secondary);
		sgre::script::ScriptEmoteBindingServices bindings;
		bindings.Motion.ResolveRuntime = [this](const auto& layer, auto source) -> sgre::scene::MotionManagerRuntime& {
			Require(layer == Leaf, "Emote factory must resolve this exact actual Layer");
			Selections.push_back(source);
			return source == sgre::script::MotionLayerSource::Primary ? *Primary : *Secondary;
		};
		bindings.Motion.ReadAsynchronousMode = [this] {
			return Asynchronous;
		};
		bindings.WindRandom = wind_random;
		bindings.ReadEntropyWord = [this] {
			return static_cast<std::uint32_t>(++EntropyReads);
		};
		Bindings = std::make_unique<sgre::script::ScriptEmoteBindings>(Vm, Objects, Layers, Psb, std::move(bindings));
	}
	~Fixture() noexcept {
		try {
			Close();
		} catch (...) {
			std::terminate();
		}
	}
	void Close() {
		if (Closed) {
			return;
		}
		if (Tree.Contains(LeafHandle)) {
			Primary->SceneContext().Clear();
			Secondary->SceneContext().Clear();
			Vm.ResetExecution();
			Layers.ReleaseInstance(Leaf);
		} else {
			Vm.ResetExecution();
		}
		LeafRoot = {};
		Vm.Close();
		Vm.RethrowReleaseFailure();
		Bindings->Close();
		Bindings.reset();
		Primary.reset();
		Secondary.reset();
		Cache.Close();
		Closed = true;
	}
	[[nodiscard]] sgre::script::SquirrelValue Call(const sgre::script::SquirrelValue& instance, std::string_view name, std::initializer_list<sgre::script::SquirrelValue> extras = {}) {
		std::vector<sgre::script::SquirrelValue> arguments{instance};
		arguments.insert(arguments.end(), extras.begin(), extras.end());
		return Vm.Invoke(Vm.ReadSlot(instance, std::string(name)), arguments).Value;
	}
	[[nodiscard]] sgre::script::SquirrelValue New(std::initializer_list<sgre::script::SquirrelValue> extras = {}) {
		std::vector<sgre::script::SquirrelValue> arguments{Vm.RootTable(), Leaf, PsbObject};
		arguments.insert(arguments.end(), extras.begin(), extras.end());
		return Vm.Invoke(Bindings->EmoteClass(), arguments).Value;
	}
	[[nodiscard]] sgre::scene::ParallelTaskSet& Second() {
		return Layers.MotionTasks(Leaf).Group(sgre::scene::MotionTaskGroup::Second);
	}
	sgre::script::SquirrelVm Vm;
	sgre::script::ScriptObjectBindings Objects;
	sgre::script::ScriptPsbBindings Psb;
	std::size_t Uploads = 0;
	std::size_t Releases = 0;
	sgre::scene::TextureCache Cache;
	sgre::scene::MotionAssetCatalog Assets;
	sgre::scene::LayerTree Tree;
	sgre::script::ScriptLayerBindings Layers;
	sgre::core::FrameClock Clock;
	std::array<std::uint32_t, sgre::scene::kMotionSharedSeedWords> Seed{};
	sgre::script::SquirrelValue Leaf;
	sgre::script::SquirrelHostRoot LeafRoot;
	sgre::scene::LayerHandle LeafHandle;
	sgre::script::SquirrelValue PsbObject;
	sgre::scene::MotionProjectId Project;
	std::unique_ptr<sgre::scene::MotionManagerRuntime> Primary;
	std::unique_ptr<sgre::scene::MotionManagerRuntime> Secondary;
	std::unique_ptr<sgre::script::ScriptEmoteBindings> Bindings;
	std::vector<sgre::script::MotionLayerSource> Selections;
	std::size_t EntropyReads = 0;
	bool Asynchronous = false;
	bool Closed = false;
};

static void TestActualFactorySixFamiliesAndDeferredRetirement() {
	Fixture fixture;
	auto instance = fixture.New();
	auto task = fixture.Bindings->Task(instance);
	Require(fixture.Second().Count() == 1 && fixture.Secondary->PlayerCount() == 1 && fixture.Primary->PlayerCount() == 0 && fixture.EntropyReads == 2, "actual Emote constructor creates one distinct Second payload on selected native manager and consumes both real entropy streams");
	Require(fixture.Layers.MotionResources(fixture.Leaf).ReferenceCount(fixture.Project) == 1 && fixture.Uploads == 1, "constructor borrows the already-registered PSB and texture owners without adding resource registration or source copies");
	Require(task.Player().PhysicalCounts().AttachedPartsControls == 6 && std::get<bool>(fixture.Call(instance, "getVisible")) && !task.Player().Root().Overrides().Visible, "actual six-family containing owner coexists with native initially-unpaused task and initially-hidden inner root");
	static_cast<void>(fixture.Call(instance, "setVariable", {std::string("probe"), 7.0f}));
	static_cast<void>(fixture.Call(instance, "playTimeline", {std::string("phase")}));
	Require(std::get<bool>(fixture.Call(instance, "getMotionPlaying", {std::string("phase")})), "native Motion/Timeline names share this actual scheduler, not a surrogate plain motion selector");
	static_cast<void>(fixture.Call(instance, "setVisible", {true}));
	const auto before = task.Player().Commands().ClockState().TickCount;
	fixture.Second().Update();
	Require(task.Player().Commands().ClockState().TickCount == before + 1 && task.Player().PhysicalCounts().Updated == 6, "one actual Layer task update drives shared clock delta then genuine controls/plain/physical phases once");
	fixture.Second().Draw(0xdeadbeef);
	Require(fixture.Secondary->SceneContext().Size() != 0, "actual task draw publishes the containing Emote's real six-family scene queue");
	RequireFailure([&] {
		fixture.Bindings->ReleaseInstance(instance);
	},
	    "unqueued");
	fixture.Secondary->SceneContext().Clear();
	const auto identity = fixture.Vm.BorrowObjectIdentity(instance);
	fixture.Vm.ResetExecution();
	instance = {};
	fixture.Vm.RethrowReleaseFailure();
	Require(!fixture.Vm.ResolveObjectIdentity(identity) && fixture.Bindings->InstanceCount() == 0 && task.IsLive() && fixture.Second().State(task.Handle()).Status == sgre::scene::TaskStatus::Retire, "wrapper finalization is nonrooting, marks actual status2 and retains physical player until the native group traversal");
	fixture.Second().Update();
	Require(!task.IsLive() && fixture.Secondary->PlayerCount() == 0 && fixture.Second().Count() == 0, "actual deferred retirement removes real UID/physics/scene ownership and runtime borrow");
	fixture.Close();
	Require(fixture.Releases == fixture.Uploads, "actual Layer managers retire shared textures in their containing close phase");
}

static void TestNativeOptionalArgumentAliasAndParentDetach() {
	Fixture fixture;
	auto first = fixture.New({1.9f});
	auto second = fixture.New({std::int32_t{1}, std::int32_t{0}});
	const auto first_task = fixture.Bindings->Task(first);
	const auto second_task = fixture.Bindings->Task(second);
	Require(fixture.Primary->PlayerCount() == 2 && fixture.Secondary->PlayerCount() == 0 && fixture.Selections.size() == 2, "native physical4 numeric conversion selects actual primary manager for both constructors");
	std::array<sgre::scene::ParallelTaskHandle, 2> order;
	Require(fixture.Second().WriteOrder(order) == 2 && order[0] == second_task.Handle() && order[1] == first_task.Handle(), "fifth constructor argument is ignored: physical4 is reused as actual prepend policy");
	fixture.Layers.ReleaseInstance(fixture.Leaf);
	Require(!first_task.IsLive() && !second_task.IsLive() && !std::get<bool>(fixture.Call(first, "getVisible")) && std::get<bool>(fixture.Call(first, "getBlinkEnabled")), "real parent retirement precedes wrapper detach; detached visibilityfalse and blinktrue preserve native defaults");
	Require(std::get<float>(fixture.Call(first, "getVariable", {std::string("probe")})) == 0 && std::get<float>(fixture.Call(first, "getTimelineBlendRatio", {std::string("phase")})) == 0, "detached scalar getters return recovered zero without touching expired scene/runtime");
	static_cast<void>(fixture.Call(first, "setVariable"));
	fixture.Bindings->ReleaseInstance(first);
	fixture.Bindings->ReleaseInstance(second);
	Require(fixture.Bindings->CanClose(), "detached wrappers release independently of their already-retired actual Layer");
}

static void TestRejectedActualProducersAndRollback() {
	Fixture fixture;
	fixture.Asynchronous = true;
	RequireFailure([&] {
		static_cast<void>(fixture.New());
	},
	    "genuine worker");
	fixture.Asynchronous = false;
	Require(fixture.Bindings->InstanceCount() == 0 && fixture.Second().Count() == 0 && fixture.EntropyReads == 0, "unrecovered worker admission leaves no wrapper/partial player/random phase");
	const auto other = fixture.Psb.WrapDocument(OwnerDocument(), std::make_shared<const std::vector<std::uint8_t>>(144, 255));
	RequireFailure([&] {
		const std::array<sgre::script::SquirrelValue, 3> arguments{fixture.Vm.RootTable(), fixture.Leaf, other};
		static_cast<void>(fixture.Vm.Invoke(fixture.Bindings->EmoteClass(), arguments));
	},
	    "exact actual Layer registration");
	Require(fixture.Second().Count() == 0 && fixture.Primary->PlayerCount() == 0 && fixture.Secondary->PlayerCount() == 0 && fixture.Layers.MotionResources(fixture.Leaf).ReferenceCount(fixture.Project) == 1, "unregistered document identity rejects before actual task allocation or resource mutation");
	auto instance = fixture.New();
	const auto root = fixture.Vm.Retain(instance);
	fixture.Vm.ResetExecution();
	static_cast<void>(fixture.Vm.CollectGarbage());
	Require(fixture.Bindings->InstanceCount() == 1 && fixture.Bindings->Task(instance).IsLive(), "explicit actual VM root, rather than adapter registry, keeps wrapper alive across collection");
	RequireFailure([&] {
		static_cast<void>(fixture.Call(instance, "getSpeed"));
	},
	    "actual MEmote");
	fixture.Bindings->ReleaseInstance(instance);
	fixture.Second().Update();
}

static void TestStringConversionAndReceiverRecheck() {
	Fixture fixture;
	auto instance = fixture.New();
	const auto task = fixture.Bindings->Task(instance);
	std::size_t conversions = 0;
	const auto name_class = fixture.Vm.CreateClass();
	fixture.Vm.NewSlot(name_class, "_tostring", fixture.Vm.CreateNativeClosure("Emote-name-growth", [&](auto& active, auto) {
		++conversions;
		for (std::size_t index = 0; index != 4096; ++index) {
			active.PushNativeValue(static_cast<std::int32_t>(index));
		}
		return sgre::script::SquirrelNativeResult::ReturnValue(std::string("probe"));
	}));
	const auto name = fixture.Vm.CreateInstance(name_class);
	static_cast<void>(fixture.Call(instance, "setVariable", {name, 37.0f, 0.0f, 0.0f}));
	Require(std::get<float>(fixture.Call(instance, "getVariable", {name})) == 37.0f && conversions == 2, "native string conversion may relocate operand storage without changing the snapshotted scalar arguments");
	RequireFailure([&] {
		static_cast<void>(fixture.Call(instance, "playTimeline", {name}));
	},
	    "invalid argument");
	Require(conversions == 2, "direct Timeline Play requires an actual string and cannot implicitly invoke ToString");
	const auto retiring_class = fixture.Vm.CreateClass();
	fixture.Vm.NewSlot(retiring_class, "_tostring", fixture.Vm.CreateNativeClosure("Emote-name-retires-receiver", [&](auto&, auto) {
		fixture.Bindings->ReleaseInstance(instance);
		return sgre::script::SquirrelNativeResult::ReturnValue(std::string("probe"));
	}));
	const auto retiring_name = fixture.Vm.CreateInstance(retiring_class);
	RequireFailure([&] {
		static_cast<void>(fixture.Call(instance, "setVariable", {retiring_name, 99.0f}));
	},
	    "actual live native wrapper");
	Require(task.IsLive() && fixture.Bindings->InstanceCount() == 0 && task.Player().ReadVariable("probe") == 37.0f, "post-conversion owner check rejects retired wrapper before mutating its deferred physical player");
	fixture.Second().Update();
	Require(!task.IsLive(), "real group traversal completes the callback-requested physical retirement");
}

static void TestInvalidServicesDoNotPublishAClass() {
	sgre::script::SquirrelVm vm;
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptPsbBindings psb(vm, objects);
	sgre::scene::LayerTree tree(4);
	sgre::script::ScriptLayerBindings layers(vm, objects, tree);
	RequireFailure([&] {
		sgre::script::ScriptEmoteBindings rejected(vm, objects, layers, psb, {});
	},
	    "runtime/entropy services");
	Require(!vm.HasSlot(vm.RootTable(), std::string("Emote")), "invalid native services reject before publishing a partially usable declaration class");
	vm.Close();
}

static std::uint32_t PublishedColor(Fixture& fixture, const sgre::script::SquirrelValue& instance) {
	return std::bit_cast<std::uint32_t>(std::get<std::int32_t>(fixture.Call(instance, "getColor")));
}

static void TestColorConstructorSequenceAndPropertyPublication() {
	Fixture fixture;
	const auto instance = fixture.New();
	const auto task = fixture.Bindings->Task(instance);
	const auto color = std::bit_cast<std::int32_t>(0x10203040u);
	// Independent actual-API fixture of EmotePicture's constructor -> setColor sequence.
	Require(std::holds_alternative<std::monostate>(fixture.Call(instance, "setColor", {color})), "actual setColor returns physical zero values");
	Require(PublishedColor(fixture, instance) == 0x808080ffu, "setColor stores a native transition without premature plain publication");
	static_cast<void>(fixture.Call(instance, "setVariable", {std::string("probe"), 23.0f}));
	const auto before = task.Player().Commands().ClockState().TickCount;
	task.Player().Progress(0);
	Require(PublishedColor(fixture, instance) == 0x10203040u && task.Player().Root().ModulationColor() == 0x10203040u, "actual zero-delta dirty property phase publishes full packed color");
	Require(task.Player().Commands().ClockState().TickCount == before && task.Player().ReadVariable("probe") == 23.0f, "property publication preserves actual clock and existing scalar variable join");
	Require(fixture.Layers.MotionResources(fixture.Leaf).ReferenceCount(fixture.Project) == 1 && fixture.Secondary->PlayerCount() == 1, "color join does not invent a resource registration or replacement player");
	static_cast<void>(fixture.Call(instance, "setColor", {std::int32_t{0}}));
	task.Player().Progress(0);
	Require(PublishedColor(fixture, instance) == 0x10203040u, "native ColorFilter0 comparison suppresses packed0 instead of clearing old color");
	static_cast<void>(fixture.Call(instance, "setColor", {true}));
	task.Player().Progress(0);
	Require(PublishedColor(fixture, instance) == 1, "native integer extraction accepts bool before storing color components");
	static_cast<void>(fixture.Call(instance, "setColor", {std::string("unsupported-integer")}));
	task.Player().Progress(0);
	Require(PublishedColor(fixture, instance) == 1, "failed native integer extraction zero-initializes color and retains filter equality quirk");
}

static void TestColorRetargetBoundsAndQueuedGuards() {
	Fixture fixture;
	const auto instance = fixture.New();
	const auto task = fixture.Bindings->Task(instance);
	static_cast<void>(fixture.Call(instance, "setColor", {std::bit_cast<std::int32_t>(0x00ff00ffu), 4.0f, 0.0f}));
	task.Player().Progress(2);
	Require(PublishedColor(fixture, instance) == 0x40bf40ffu, "native color interpolation truncates byte components at the actual halfway property phase");
	static_cast<void>(fixture.Call(instance, "setColor", {std::int32_t{-1}, 2.0f, 0.0f}));
	task.Player().Progress(1);
	Require(PublishedColor(fixture, instance) == 0x9fdf9fffu, "retarget starts from current float components, retaining fractional green before packed truncation");
	const auto published = PublishedColor(fixture, instance);
	RequireFailure([&] {
		static_cast<void>(fixture.Call(instance, "setColor"));
	},
	    "invalid argument.");
	RequireFailure([&] {
		static_cast<void>(fixture.Call(instance, "setColor", {1, 2, 3, 4}));
	},
	    "invalid argument.");
	RequireFailure([&] {
		static_cast<void>(fixture.Call(instance, "setColor", {1, std::numeric_limits<float>::infinity()}));
	},
	    "finite duration");
	Require(PublishedColor(fixture, instance) == published, "rejected count/finite controls preserve actual published color");
	static_cast<void>(fixture.Call(instance, "setVisible", {true}));
	fixture.Second().Draw(0);
	RequireFailure([&] {
		static_cast<void>(fixture.Call(instance, "setColor", {1}));
	},
	    "unqueued");
	RequireFailure([&] {
		static_cast<void>(fixture.Call(instance, "clear"));
	},
	    "unqueued");
	fixture.Secondary->SceneContext().Clear();
	Require(PublishedColor(fixture, instance) == published, "rejected queued operations preserve borrowed scene and published appearance");
	static_cast<void>(fixture.Call(instance, "clear"));
	Require(task.Player().PlainPlayer().Scene() == nullptr && task.IsLive(), "warm Common clear retires the actual UID tree without retiring the containing task");
	task.Player().Progress(1);
	const auto completed_color = PublishedColor(fixture, instance);
	Require(completed_color == 0xffffffffu, "post-clear actual property step completes the previously pending native color retarget");
	Require(task.Player().PhysicalCounts().Updated == 0 && task.Player().PhysicalCounts().MeshWrites == 0, "post-clear containing phase sees no retired UID control instances");
	static_cast<void>(fixture.Call(instance, "clear"));
	Require(PublishedColor(fixture, instance) == completed_color && fixture.Layers.MotionResources(fixture.Leaf).ReferenceCount(fixture.Project) == 1, "repeated Common clear preserves its actual completed color and enclosing resource registration");
}

static void TestColorNativePackingDomain() {
	Require(sgre::scene::PackEmoteColor({255.875f, -1.75f, 512.5f, 511.5f}) == 0xffffffFFu, "native pack retains signed oversized component bits without per-byte clamp or mask");
	Require(sgre::scene::PackEmoteColor({2147483904.0f, 0, 0, 0}) == 0x80000100u, "native signed64 conversion preserves positive values beyond int32");
	Require(sgre::scene::PackEmoteColor({-2147483904.0f, 0, 0, 0}) == 0x7fffff00u, "native signed64 conversion preserves negative values beyond int32");
	Require(sgre::scene::PackEmoteColor({0x1p63f, 0, 0, 0}) == 0 && sgre::scene::PackEmoteColor({std::numeric_limits<float>::quiet_NaN(), 0, 0, 0}) == 0, "native signed64 indefinite has low32 zero without C++ out-of-range conversion");
	sgre::scene::EmoteColorTransition transition;
	Require(transition.Advance(0) == 0x808080ffu, "actual constructor color starts at recovered modulation default");
	transition.Set(0x10203040u, -1, 140);
	Require(transition.Advance(0) == 0x10203040u, "nonpositive duration directly changes transition components");
	RequireFailure([&] {
		transition.Set(0, 1, std::numeric_limits<float>::quiet_NaN());
	},
	    "finite duration");
	RequireFailure([&] {
		static_cast<void>(transition.Advance(-1));
	},
	    "finite nonnegative");
	Require(transition.Advance(0) == 0x10203040u, "invalid transition control never partially changes a component");
}

static void TestPauseAndPlainClearOwners() {
	Fixture fixture(false);
	const auto instance = fixture.New();
	const auto task = fixture.Bindings->Task(instance);
	static_cast<void>(fixture.Call(instance, "setVisible", {true}));
	static_cast<void>(fixture.Call(instance, "pause", {true, std::int32_t{97}}));
	const auto before = task.Player().Commands().ClockState().TickCount;
	fixture.Second().Update();
	Require(fixture.Second().State(task.Handle()).Status == sgre::scene::TaskStatus::Complete && task.Player().Commands().ClockState().TickCount == before && task.IsLive(), "native pause status1 keeps the actual task owned while preventing Update");
	fixture.Second().Draw(0);
	Require(fixture.Secondary->SceneContext().Size() != 0, "native paused status1 remains drawable independently of draw pause");
	fixture.Secondary->SceneContext().Clear();
	static_cast<void>(fixture.Call(instance, "pause", {false}));
	fixture.Second().Update();
	Require(fixture.Second().State(task.Handle()).Status == sgre::scene::TaskStatus::Active && task.Player().Commands().ClockState().TickCount > before, "unpause restores the exact live task's normal clock update");
	const auto parameters = task.Player().Commands().ParameterValues().ReadLocal("probe");
	static_cast<void>(fixture.Call(instance, "clear", {std::int32_t{7}}));
	Require(task.Player().PlainPlayer().Scene() == nullptr && task.IsLive() && fixture.Secondary->PlayerCount() == 1 && fixture.Second().State(task.Handle()).Status == sgre::scene::TaskStatus::Active, "native clear retires the actual selected tree without inventing task retirement");
	Require(fixture.Layers.MotionResources(fixture.Leaf).ReferenceCount(fixture.Project) == 1, "plain clear does not unregister the enclosing original resource");
	static_cast<void>(fixture.Call(instance, "clear"));
	fixture.Second().Update();
	Require(task.Player().Commands().ParameterValues().ReadLocal("probe") == parameters, "idempotent no-Common clear preserves real variable and plain control owners through a later update");
	static_cast<void>(fixture.Call(instance, "setVisible", {false}));
}

static void TestDetachedColorClearAndPause() {
	Fixture fixture;
	const auto instance = fixture.New();
	const auto task = fixture.Bindings->Task(instance);
	fixture.Layers.ReleaseInstance(fixture.Leaf);
	Require(!task.IsLive() && PublishedColor(fixture, instance) == 0x808080ffu, "detached color getter returns original packed default without dead owner access");
	static_cast<void>(fixture.Call(instance, "setColor"));
	static_cast<void>(fixture.Call(instance, "setColor", {1, 2, 3, 4, 5}));
	static_cast<void>(fixture.Call(instance, "pause", {true}));
	static_cast<void>(fixture.Call(instance, "clear"));
	fixture.Bindings->ReleaseInstance(instance);
	Require(fixture.Bindings->CanClose(), "detached direct setters return before argument validation and retire wrapper without task resurrection");
}

static void TestStoredPropertyPhaseAndReentry() {
	Fixture fixture(false);
	sgre::scene::MotionPlayerCommands commands(fixture.Layers.MotionResources(fixture.Leaf).Secondary());
	commands.ParameterValues().WriteLocal("probe", 41, 0);
	sgre::scene::EmoteVariableProgram program;
	program.Immediate.push_back("probe");
	std::vector<float> steps;
	sgre::scene::EmoteVariableControls* active = nullptr;
	sgre::scene::EmoteVariableControls controls(commands, [] {
	},
	    std::move(program), {}, [&](float step) {
		    steps.push_back(step);
		    Require(commands.ParameterValues().ReadLocal("probe") == 41, "containing property phase precedes final plain variable publication");
		    RequireFailure([&] {
			    active->MarkPropertiesDirty();
		    },
		        "idle containing publisher");
	    });
	active = &controls;
	controls.Set("probe", 7);
	controls.Update(2.5f);
	Require(steps.size() == 3 && steps[0] == 1.1f && steps[1] == 1.1f && std::abs(steps[2] - .3f) < .000001f && commands.ParameterValues().ReadLocal("probe") == 7, "actual stored property callback participates in each bounded native step before scalar output commits");
	steps.clear();
	controls.Update(0);
	Require(steps.empty(), "clean zero elapsed cannot fabricate a property phase");
	commands.ParameterValues().WriteLocal("probe", 41, 0);
	controls.MarkPropertiesDirty();
	controls.Update(0);
	Require(steps.size() == 1 && steps[0] == 0, "actual property dirty owner admits exactly one zero elapsed step");
}

static std::array<float, 2> RetainedCoordinate(Fixture& fixture, const sgre::script::SquirrelValue& instance) {
	return {std::get<float>(fixture.Call(instance, "getLeft")), std::get<float>(fixture.Call(instance, "getTop"))};
}

static void TestCoordinateConstructorAndPropertyPublication() {
	Fixture fixture;
	const auto instance = fixture.New();
	const auto task = fixture.Bindings->Task(instance);
	static_cast<void>(fixture.Call(instance, "setColor", {std::bit_cast<std::int32_t>(0x10203040u)}));
	Require(std::holds_alternative<std::monostate>(fixture.Call(instance, "setCoord", {12.5f, -7.25f})), "actual setCoord returns physical zero values after the genuine constructor and color call");
	Require(RetainedCoordinate(fixture, instance) == std::array<float, 2>{12.5f, -7.25f}, "native coordinate getters read immediate transition components before publication");
	Require(task.Player().Root().Overrides().Coordinate[0] == 0 && task.Player().Root().Overrides().Coordinate[1] == 0 && PublishedColor(fixture, instance) == 0x808080ffu, "setCoord and setColor retain the actual prior published root until property phase");
	const auto tick = task.Player().Commands().ClockState().TickCount;
	task.Player().Progress(0);
	Require(task.Player().Root().Overrides().Coordinate[0] == 12.5f && task.Player().Root().Overrides().Coordinate[1] == -7.25f && PublishedColor(fixture, instance) == 0x10203040u, "dirty zero-delta phase publishes coordinate and accepted color together");
	Require(task.Player().Commands().ClockState().TickCount == tick && fixture.Secondary->PlayerCount() == 1 && fixture.Layers.MotionResources(fixture.Leaf).ReferenceCount(fixture.Project) == 1, "coordinate publication preserves actual player identity, registrations and native zero-time clock");
	Require(std::get<float>(fixture.Call(instance, "getLeft", {std::int32_t{77}})) == 12.5f && std::get<float>(fixture.Call(instance, "getTop", {false})) == -7.25f, "native typed getters ignore extras and return actual SQFloat");
}

static void TestCoordinateRetargetAndNativeConversions() {
	Fixture fixture(false);
	const auto instance = fixture.New();
	const auto task = fixture.Bindings->Task(instance);
	static_cast<void>(fixture.Call(instance, "setCoord", {16, -8, 4.0f}));
	Require(RetainedCoordinate(fixture, instance) == std::array<float, 2>{0, 0}, "positive duration retains current coordinate until genuine advancement");
	task.Player().Progress(1);
	Require(RetainedCoordinate(fixture, instance) == std::array<float, 2>{4, -2}, "actual property step advances both coordinate components by the same native ratio");
	static_cast<void>(fixture.Call(instance, "setCoord", {8.0f, 4.0f, 2.0f, 0.0f}));
	Require(RetainedCoordinate(fixture, instance) == std::array<float, 2>{4, -2}, "retarget starts from actual current components without premature target publication");
	task.Player().Progress(1);
	Require(RetainedCoordinate(fixture, instance) == std::array<float, 2>{6, 1} && task.Player().Root().Overrides().Coordinate[0] == 6 && task.Player().Root().Overrides().Coordinate[1] == 1, "retarget and plain publication share the real property phase");
	static_cast<void>(fixture.Call(instance, "setCoord", {true, false}));
	Require(RetainedCoordinate(fixture, instance) == std::array<float, 2>{1, 0}, "native required coordinate extraction accepts bool without string conversion");
	static_cast<void>(fixture.Call(instance, "setCoord", {std::string("not-a-number"), std::monostate{}, std::string("ignored-duration"), false}));
	Require(RetainedCoordinate(fixture, instance) == std::array<float, 2>{0, 0}, "unsupported required and optional numeric values zero-initialize rather than coercing text");
	static_cast<void>(fixture.Call(instance, "setCoord", {3.5f, -9.25f, -1.0f, 140.0f}));
	Require(RetainedCoordinate(fixture, instance) == std::array<float, 2>{3.5f, -9.25f}, "nonpositive duration updates retained coordinates immediately with an optional native easing value");
}

static void TestCoordinateAdmissionAndQueuedOwner() {
	Fixture fixture;
	const auto instance = fixture.New();
	const auto task = fixture.Bindings->Task(instance);
	static_cast<void>(fixture.Call(instance, "setCoord", {7.0f, -3.0f}));
	task.Player().Progress(0);
	const auto before = RetainedCoordinate(fixture, instance);
	for (const auto count : {0, 1, 5}) {
		std::vector<sgre::script::SquirrelValue> arguments(static_cast<std::size_t>(count) + 1, 3.0f);
		arguments[0] = instance;
		RequireFailure([&] {
			static_cast<void>(fixture.Vm.Invoke(fixture.Vm.ReadSlot(instance, std::string("setCoord")), arguments));
		},
		    "invalid argument.");
	}
	RequireFailure([&] {
		static_cast<void>(fixture.Call(instance, "setCoord", {1.0f, std::numeric_limits<float>::infinity()}));
	},
	    "finite components");
	RequireFailure([&] {
		static_cast<void>(fixture.Call(instance, "setCoord", {1.0f, 2.0f, std::numeric_limits<float>::infinity()}));
	},
	    "finite duration");
	RequireFailure([&] {
		static_cast<void>(fixture.Call(instance, "setCoord", {1.0f, 2.0f, 1.0f, std::numeric_limits<float>::quiet_NaN()}));
	},
	    "finite duration");
	Require(RetainedCoordinate(fixture, instance) == before, "rejected argument or finite control preserves both retained components");
	static_cast<void>(fixture.Call(instance, "setVisible", {true}));
	fixture.Second().Draw(0);
	RequireFailure([&] {
		static_cast<void>(fixture.Call(instance, "setCoord", {11.0f, 12.0f}));
	},
	    "unqueued");
	fixture.Secondary->SceneContext().Clear();
	Require(RetainedCoordinate(fixture, instance) == before && task.Player().Root().Overrides().Coordinate[0] == 7 && task.Player().Root().Overrides().Coordinate[1] == -3, "queued owner rejection preserves transition and already borrowed published geometry");
}

static void TestCoordinateUnitsBoundsAndSignedZero() {
	sgre::scene::EmoteCoordinateTransition pixels;
	pixels.Set({-0.0f, 0.0f});
	Require(std::bit_cast<std::uint32_t>(pixels.Current()[0]) == 0x80000000u, "coordinate getter retains native immediate negative zero");
	const auto published = sgre::scene::PrepareEmoteDefaultCoordinatePublication(pixels.Advance(0));
	Require(std::bit_cast<std::uint32_t>(published[0]) == 0, "native default origin addition clears negative zero before root publication");
	sgre::scene::EmoteCoordinateTransition meters(sgre::scene::EmoteCoordinateMode::Meters, 3.25f);
	meters.Set({1.25f, -2.75f});
	Require(meters.Current() == std::array<float, 2>{1.25f, -2.75f} && meters.Advance(0) == std::array<float, 2>{4.0625f, -8.9375f}, "native mode1 applies unit scale on Set and reciprocal getter conversion without changing physical pixel units");
	const auto before = meters.Current();
	RequireFailure([&] {
		meters.Set({std::numeric_limits<float>::max(), 1});
	},
	    "finite components");
	RequireFailure([&] {
		static_cast<void>(meters.Advance(-1));
	},
	    "finite nonnegative");
	Require(meters.Current() == before, "rejected unit overflow and negative step cannot partially mutate coordinate state");
	RequireFailure([&] {
		sgre::scene::EmoteCoordinateTransition rejected(sgre::scene::EmoteCoordinateMode::Meters, 0);
	},
	    "positive unit scale");
}

static void TestDetachedCoordinateReceiverAndRetirement() {
	Fixture fixture;
	const auto instance = fixture.New();
	const auto task = fixture.Bindings->Task(instance);
	static_cast<void>(fixture.Call(instance, "setCoord", {12.5f, -7.25f}));
	fixture.Layers.ReleaseInstance(fixture.Leaf);
	Require(!task.IsLive() && RetainedCoordinate(fixture, instance) == std::array<float, 2>{0, 0}, "detached coordinate getters return native zero without dead task access");
	static_cast<void>(fixture.Call(instance, "setCoord"));
	static_cast<void>(fixture.Call(instance, "setCoord", {1, 2, 3, 4, 5}));
	fixture.Bindings->ReleaseInstance(instance);
	Require(fixture.Bindings->CanClose(), "detached coordinate setter returns before arity checks and never resurrects a task");
}

static float RetainedScale(Fixture& fixture, const sgre::script::SquirrelValue& instance) {
	return std::get<float>(fixture.Call(instance, "getScale"));
}

static void TestScaleConstructorChainAndPropertyPublication() {
	Fixture fixture;
	const auto instance = fixture.New();
	const auto task = fixture.Bindings->Task(instance);
	Require(RetainedScale(fixture, instance) == 1 && task.Player().Root().Overrides().ZoomX == 1 && task.Player().Root().Overrides().ZoomY == 1, "actual scalar scale starts at native1 independently of coordinate units and metadata calibration");
	static_cast<void>(fixture.Call(instance, "setColor", {std::bit_cast<std::int32_t>(0x10203040u)}));
	static_cast<void>(fixture.Call(instance, "setCoord", {12.5f, -7.25f}));
	Require(std::holds_alternative<std::monostate>(fixture.Call(instance, "setScale", {2.5f})), "original positioning chain setScale uses the raw no-values result after setCoord");
	Require(RetainedScale(fixture, instance) == 2.5f && task.Player().Root().Overrides().ZoomX == 1 && PublishedColor(fixture, instance) == 0x808080ffu, "scale getter reads immediate current before actual dirty property publication");
	const auto tick = task.Player().Commands().ClockState().TickCount;
	task.Player().Progress(0);
	const auto& root = task.Player().Root().Overrides();
	Require(root.ZoomX == 2.5f && root.ZoomY == 2.5f && root.Coordinate[0] == 12.5f && root.Coordinate[1] == -7.25f && PublishedColor(fixture, instance) == 0x10203040u, "one genuine dirty zero phase publishes coordinate, color and both scalar zoom axes together");
	Require(task.Player().Commands().ClockState().TickCount == tick && fixture.Secondary->PlayerCount() == 1 && fixture.Layers.MotionResources(fixture.Leaf).ReferenceCount(fixture.Project) == 1, "scale publication retains actual player, resource and zero-time clock ownership");
	Require(std::get<float>(fixture.Call(instance, "getScale", {false, std::int32_t{97}})) == 2.5f, "native typed scale getter ignores extra arguments and returns SQFloat");
}

static void TestScaleRetargetAndNumericConversions() {
	sgre::scene::EmoteScaleTransition crossing;
	crossing.Set(5, 4);
	Require(crossing.Advance(1) == 2, "raw scalar transition reaches the genuine retarget start");
	crossing.Set(-2, 2);
	Require(crossing.Advance(1) == 0, "native scalar interpolation retains a real zero without clamping it to1");
	crossing.Set(-0.0f);
	Require(std::bit_cast<std::uint32_t>(crossing.Advance(0)) == 0x80000000u, "raw scalar advance retains negative zero independently of singular scene composition");

	Fixture fixture(false);
	const auto instance = fixture.New();
	const auto task = fixture.Bindings->Task(instance);
	static_cast<void>(fixture.Call(instance, "setScale", {5, 4.0f}));
	Require(RetainedScale(fixture, instance) == 1, "positive scale duration retains native current1 until property advancement");
	task.Player().Progress(1);
	Require(RetainedScale(fixture, instance) == 2 && task.Player().Root().Overrides().ZoomX == 2, "dim1 transition advances and publishes with actual elapsed ticks");
	static_cast<void>(fixture.Call(instance, "setScale", {6.0f, 2.0f, 0.0f}));
	Require(RetainedScale(fixture, instance) == 2, "retarget preserves current value before the next native step");
	task.Player().Progress(1);
	Require(RetainedScale(fixture, instance) == 4 && task.Player().Root().Overrides().ZoomX == 4 && task.Player().Root().Overrides().ZoomY == 4, "retarget publishes a nonzero scalar through the genuine selected deformation tree");
	static_cast<void>(fixture.Call(instance, "setScale", {-2.0f, -1.0f, 140.0f}));
	task.Player().Progress(0);
	Require(RetainedScale(fixture, instance) == -2 && task.Player().Root().Overrides().ZoomX == -2, "negative target and nonpositive duration retain native signed scale behavior");
	static_cast<void>(fixture.Call(instance, "setScale", {true}));
	Require(RetainedScale(fixture, instance) == 1, "required scale bool extraction is numeric without string conversion");
	static_cast<void>(fixture.Call(instance, "setScale", {std::string("unconverted"), std::string("duration"), std::monostate{}}));
	Require(RetainedScale(fixture, instance) == 0, "unsupported required and optional scale numbers zero-initialize instead of coercing text");
	static_cast<void>(fixture.Call(instance, "setScale", {-0.0f}));
	Require(std::bit_cast<std::uint32_t>(RetainedScale(fixture, instance)) == 0x80000000u, "retained scalar getter preserves immediate negative zero");
	Require(task.Player().Root().Overrides().ZoomX == -2, "zero targets remain retained before publication; the selected deformation still requires an invertible root");
	static_cast<void>(fixture.Call(instance, "setScale", {2.0f}));
	task.Player().Progress(0);
	Require(task.Player().Root().Overrides().ZoomX == 2 && RetainedScale(fixture, instance) == 2, "an invertible target can replace the pending scalar before the actual next scene composition");
}

static void TestScaleAdmissionAndQueuedOwners() {
	Fixture fixture(false);
	const auto instance = fixture.New();
	const auto task = fixture.Bindings->Task(instance);
	static_cast<void>(fixture.Call(instance, "setScale", {2.5f}));
	task.Player().Progress(0);
	for (const auto count : {0, 4}) {
		std::vector<sgre::script::SquirrelValue> arguments(static_cast<std::size_t>(count) + 1, 3.0f);
		arguments[0] = instance;
		RequireFailure([&] {
			static_cast<void>(fixture.Vm.Invoke(fixture.Vm.ReadSlot(instance, std::string("setScale")), arguments));
		},
		    "invalid argument.");
	}
	for (const auto arguments : {std::array<float, 3>{std::numeric_limits<float>::infinity(), 0, 0}, std::array<float, 3>{2, std::numeric_limits<float>::infinity(), 0}, std::array<float, 3>{2, 1, std::numeric_limits<float>::quiet_NaN()}}) {
		RequireFailure([&] {
			static_cast<void>(fixture.Call(instance, "setScale", {arguments[0], arguments[1], arguments[2]}));
		},
		    "finite scalar");
	}
	Require(RetainedScale(fixture, instance) == 2.5f && task.Player().Root().Overrides().ZoomX == 2.5f, "invalid arity and nonfinite control cannot mutate retained scale or published root");
	static_cast<void>(fixture.Call(instance, "setVisible", {true}));
	fixture.Second().Draw(0);
	RequireFailure([&] {
		static_cast<void>(fixture.Call(instance, "setScale", {7.0f}));
	},
	    "unqueued");
	fixture.Secondary->SceneContext().Clear();
	Require(RetainedScale(fixture, instance) == 2.5f && task.Player().Root().Overrides().ZoomX == 2.5f, "queued borrowed geometry rejects scale mutation without changing its current owner");
}

static void TestScalePauseClearAndDetachedLifetime() {
	Fixture fixture(false);
	const auto instance = fixture.New();
	const auto task = fixture.Bindings->Task(instance);
	static_cast<void>(fixture.Call(instance, "setScale", {5.0f, 4.0f}));
	static_cast<void>(fixture.Call(instance, "pause", {true}));
	fixture.Second().Update();
	Require(RetainedScale(fixture, instance) == 1 && task.Player().Root().Overrides().ZoomX == 1, "native pause retains pending scale without an invented property clock");
	static_cast<void>(fixture.Call(instance, "pause", {false}));
	fixture.Second().Update();
	Require(RetainedScale(fixture, instance) > 1 && task.Player().Root().Overrides().ZoomX == RetainedScale(fixture, instance), "unpause resumes actual task property advancement and root publication");
	const auto before = RetainedScale(fixture, instance);
	static_cast<void>(fixture.Call(instance, "clear"));
	Require(task.IsLive() && RetainedScale(fixture, instance) == before, "plain clear preserves the actual retained scale transition owner");
	fixture.Layers.ReleaseInstance(fixture.Leaf);
	Require(!task.IsLive() && RetainedScale(fixture, instance) == 1, "detached getScale returns native1 without accessing the retired task");
	static_cast<void>(fixture.Call(instance, "setScale"));
	static_cast<void>(fixture.Call(instance, "setScale", {1, 2, 3, 4}));
	fixture.Bindings->ReleaseInstance(instance);
	Require(fixture.Bindings->CanClose(), "detached scale setter returns before argument validation and does not resurrect task state");
}

static void TestColdCommonClearThenActualTaskRetirement() {
	Fixture fixture;
	const auto instance = fixture.New();
	const auto task = fixture.Bindings->Task(instance);
	const auto project = fixture.Project;
	static_cast<void>(fixture.Call(instance, "clear"));
	Require(task.Player().PlainPlayer().Scene() == nullptr && task.IsLive(), "Common clear before any positive physics phase performs native tree retirement without invented anchor initialization");
	static_cast<void>(fixture.Call(instance, "clear"));
	fixture.Bindings->ReleaseInstance(instance);
	Require(fixture.Second().State(task.Handle()).Status == sgre::scene::TaskStatus::Retire, "actual release requests deferred task retirement after cold Common clear");
	fixture.Second().Update();
	Require(!task.IsLive() && fixture.Secondary->PlayerCount() == 0 && fixture.Bindings->CanClose(), "real status2 retirement skips cold physics and releases every containing player/UID owner");
	Require(fixture.Layers.MotionResources(fixture.Leaf).ReferenceCount(project) == 1, "task retirement is separate from actual PSB registration lifetime");
}

static float PublishedRotation(Fixture& fixture, const sgre::script::SquirrelValue& instance) {
	return std::get<float>(fixture.Call(instance, "getRot"));
}

static void TestRotationConstructorChainAndPublication() {
	Fixture fixture(false);
	const auto instance = fixture.New();
	const auto task = fixture.Bindings->Task(instance);
	Require(PublishedRotation(fixture, instance) == 0 && task.Player().Root().AngleDegrees() == 0, "rotation starts at the actual published plain root0");
	static_cast<void>(fixture.Call(instance, "setCoord", {12.5f, -7.25f}));
	static_cast<void>(fixture.Call(instance, "setScale", {2.5f}));
	Require(std::holds_alternative<std::monostate>(fixture.Call(instance, "setRot", {1.57079637f})), "original setCoord/setScale/setRot chain returns no native values");
	Require(PublishedRotation(fixture, instance) == 0 && task.Player().Root().AngleDegrees() == 0, "getRot reads the published root rather than immediate angular current");
	const auto tick = task.Player().Commands().ClockState().TickCount;
	task.Player().Progress(0);
	const auto& root = task.Player().Root();
	Require(root.AngleDegrees() == 90 && PublishedRotation(fixture, instance) == 1.57079637f, "zero-time dirty property phase publishes radians as actual root degrees");
	Require(root.Overrides().Coordinate[0] == 12.5f && root.Overrides().Coordinate[1] == -7.25f && root.Overrides().ZoomX == 2.5f && root.Overrides().ZoomY == 2.5f, "default pivot rotation preserves separately published coordinate and scalar zoom");
	Require(task.Player().Commands().ClockState().TickCount == tick && fixture.Secondary->PlayerCount() == 1, "rotation has no invented clock or extra player");
	Require(std::get<float>(fixture.Call(instance, "getRot", {false, 97})) == PublishedRotation(fixture, instance), "typed getRot ignores extra ordinary arguments");
}

static void TestRotationShortestArcRetargetAndBoundedWork() {
	sgre::scene::EmoteRotationTransition rotation;
	rotation.Set(5, 4);
	Require(rotation.Current() == 0, "pending positive-duration rotation retains the old current before advancement");
	const auto first = rotation.Advance(1);
	Require(first > 5 && first < 6.28318548f, "native shortest arc reaches angle5 through the negative direction");
	rotation.Set(5, 7, 140);
	const auto second = rotation.Advance(1);
	Require(second < first && second > 5, "same-target longer-duration setter preserves the active native transition");
	Require(rotation.Advance(2) == 5, "ignored same-target request does not extend completion");
	// Original retarget_current constructs a fresh owner; angle5 above has a different shortest arc.
	sgre::scene::EmoteRotationTransition retarget;
	retarget.Set(1, 4, .5f);
	const auto retarget_start = retarget.Advance(1);
	Require(retarget_start == .125f, "original retarget case reaches the captured quarter-progress eased current");
	retarget.Set(2, 2, -.5f);
	Require(retarget.Current() == retarget_start && retarget.Advance(0) == retarget_start, "retarget waits for the next property advance and retains current at zero time");
	Require(std::bit_cast<std::uint32_t>(retarget.Advance(.5f)) == 0x3f5e7cf6u, "retarget intermediate current equals the unchanged captured native word");
	Require(retarget.Advance(1.5f) == 2, "retarget completes at the independently captured normalized target");
	rotation.Set(-0.0f);
	Require(std::bit_cast<std::uint32_t>(rotation.Current()) == 0x80000000u, "immediate angular control retains native negative zero before root equality publication");
	RequireFailure([&] {
		rotation.Set(std::numeric_limits<float>::max());
	},
	    "bounded angular work");
	Require(std::bit_cast<std::uint32_t>(rotation.Current()) == 0x80000000u, "normalization bound failure cannot alter retained angular state");
}

static void TestRotationNativeConversionsAndQueuedOwner() {
	Fixture fixture(false);
	const auto instance = fixture.New();
	const auto task = fixture.Bindings->Task(instance);
	static_cast<void>(fixture.Call(instance, "setRot", {true}));
	task.Player().Progress(0);
	Require(PublishedRotation(fixture, instance) > .99f && PublishedRotation(fixture, instance) < 1.01f, "required native angle bool becomes one radian");
	static_cast<void>(fixture.Call(instance, "setRot", {std::string("unconverted"), std::string("duration"), std::monostate{}}));
	Require(PublishedRotation(fixture, instance) > .99f, "zero fallback remains delayed behind the actual published getter");
	task.Player().Progress(0);
	Require(PublishedRotation(fixture, instance) == 0, "unsupported required and optional numbers initialize to0 without text coercion");
	for (const auto count : {0, 4}) {
		std::vector<sgre::script::SquirrelValue> arguments(static_cast<std::size_t>(count) + 1, 3.0f);
		arguments[0] = instance;
		RequireFailure([&] {
			static_cast<void>(fixture.Vm.Invoke(fixture.Vm.ReadSlot(instance, std::string("setRot")), arguments));
		},
		    "invalid argument.");
	}
	for (const auto arguments : {std::array<float, 3>{std::numeric_limits<float>::infinity(), 0, 0}, std::array<float, 3>{2, std::numeric_limits<float>::infinity(), 0}, std::array<float, 3>{2, 1, std::numeric_limits<float>::quiet_NaN()}}) {
		RequireFailure([&] {
			static_cast<void>(fixture.Call(instance, "setRot", {arguments[0], arguments[1], arguments[2]}));
		},
		    "finite angular");
	}
	static_cast<void>(fixture.Call(instance, "setVisible", {true}));
	fixture.Second().Draw(0);
	RequireFailure([&] {
		static_cast<void>(fixture.Call(instance, "setRot", {2.0f}));
	},
	    "unqueued");
	fixture.Secondary->SceneContext().Clear();
	Require(PublishedRotation(fixture, instance) == 0 && task.Player().Root().AngleDegrees() == 0, "invalid controls and queued mutations cannot change actual root publication");
}

static void TestRotationPauseClearAndDetachedLifetime() {
	Fixture fixture(false);
	const auto instance = fixture.New();
	const auto task = fixture.Bindings->Task(instance);
	static_cast<void>(fixture.Call(instance, "setRot", {1.0f, 4.0f}));
	static_cast<void>(fixture.Call(instance, "pause", {true}));
	fixture.Second().Update();
	Require(PublishedRotation(fixture, instance) == 0, "pause retains pending angular state before actual task property update");
	static_cast<void>(fixture.Call(instance, "pause", {false}));
	fixture.Second().Update();
	const auto before = PublishedRotation(fixture, instance);
	Require(before > 0 && before < 1, "unpause advances the original task angular property phase");
	static_cast<void>(fixture.Call(instance, "clear"));
	Require(task.IsLive() && PublishedRotation(fixture, instance) == before, "plain clear preserves the containing angular owner and published transform");
	fixture.Layers.ReleaseInstance(fixture.Leaf);
	Require(!task.IsLive() && PublishedRotation(fixture, instance) == 0, "detached getRot returns native0 without dereferencing retired task");
	static_cast<void>(fixture.Call(instance, "setRot"));
	static_cast<void>(fixture.Call(instance, "setRot", {1, 2, 3, 4}));
	fixture.Bindings->ReleaseInstance(instance);
	Require(fixture.Bindings->CanClose(), "detached setter returns before count validation and never recreates ownership");
}

static std::array<std::uint32_t, 6> RotationMatrixWords(const sgre::resource::MotionMeshMatrix& matrix) {
	return {std::bit_cast<std::uint32_t>(matrix.XX), std::bit_cast<std::uint32_t>(matrix.XY), std::bit_cast<std::uint32_t>(matrix.TranslationX), std::bit_cast<std::uint32_t>(matrix.YX), std::bit_cast<std::uint32_t>(matrix.YY), std::bit_cast<std::uint32_t>(matrix.TranslationY)};
}

static void TestPrimaryRotationPreservesSeparateCommonInput() {
	Fixture fixture;
	const auto instance = fixture.New();
	const auto task = fixture.Bindings->Task(instance);
	RequireFailure([&] {
		static_cast<void>(task.Player().PhysicsToWorld());
	},
	    "completed actual global frame");
	const auto& asset = fixture.Layers.MotionResources(fixture.Leaf).BorrowAsset(fixture.Project);
	const auto definition = sgre::scene::ReadEmoteVelvetyFrameDefinition(*asset.Document);
	Require(definition.has_value(), "actual containing rotation fixture requires its real Common definition");
	const auto* metadata = asset.Document->Root.Find("metadata");
	const auto* profile = metadata == nullptr ? nullptr : metadata->Find("charaProfile");
	const auto* height = profile == nullptr ? nullptr : profile->Find("height");
	Require(height != nullptr, "actual Common comparison requires the containing document height");
	const auto view_height = static_cast<float>(std::get<double>(height->Data));
	sgre::scene::EmoteVelvetyFrameOwner expected(task.Player().Root(), task.Player().Commands(), task.Player().PlainPlayer(), *definition, view_height);
	sgre::scene::EmoteVelvetyGlobalControls controls;
	static_cast<void>(fixture.Call(instance, "setVisible", {true}));
	static_cast<void>(fixture.Call(instance, "setCoord", {8.0f, -4.0f}));
	static_cast<void>(fixture.Call(instance, "setRot", {2.0f, 4.0f, 133.0f}));
	// Primary easing_133 publishes both phases; the independent extended owner remains at its native zero default.
	const std::array<float, 2> deltas{.4f, .6f};
	const std::array<std::uint32_t, 2> native_getter{0x3b800000u, 0x40c8b55au};
	for (std::size_t index = 0; index != deltas.size(); ++index) {
		task.Player().Progress(deltas[index]);
		Require(std::bit_cast<std::uint32_t>(PublishedRotation(fixture, instance)) == native_getter[index], "primary publication retains both independently captured native getter words");
		static_cast<void>(expected.Prepare(deltas[index], controls));
		Require(RotationMatrixWords(task.Player().PhysicsToWorld()) == RotationMatrixWords(expected.PhysicsToWorld()), "actual Common conversion must preserve its separate default-zero extended rotation with genuine root/reference/history owners");
		Require(task.Player().PhysicalCounts().Updated == 6, "positive rotation frame must visit the actual six-family physical owner");
	}
	const auto actual = task.Player().PhysicsToWorld();
	const auto published = PublishedRotation(fixture, instance);
	Require(std::bit_cast<std::uint32_t>(published) == 0x40c8b55au && controls.Rotation == 0, "nonzero primary root publication cannot replace the independently owned extended physics angle");
	// Original two-owner witness proves both primary Current and its published roundtrip are distinct from the extension.
	for (const auto primary_word : {0x40c8b559u, 0x40c8b55au}) {
		sgre::scene::EmoteVelvetyFrameOwner wrong_input(task.Player().Root(), task.Player().Commands(), task.Player().PlainPlayer(), *definition, view_height);
		auto wrong_controls = controls;
		wrong_controls.Rotation = std::bit_cast<float>(primary_word);
		static_cast<void>(wrong_input.Prepare(deltas.back(), wrong_controls));
		const auto& substituted = wrong_input.PhysicsToWorld();
		Require(actual.XX != substituted.XX || actual.XY != substituted.XY || actual.YX != substituted.YX || actual.YY != substituted.YY, "actual Common linear conversion must reject either primary-angle substitution");
	}
	static_cast<void>(fixture.Call(instance, "setVisible", {true}));
	fixture.Second().Draw(0);
	RequireFailure([&] {
		static_cast<void>(task.Player().PhysicsToWorld());
	},
	    "unqueued");
	fixture.Secondary->SceneContext().Clear();
	Require(RotationMatrixWords(task.Player().PhysicsToWorld()) == RotationMatrixWords(actual), "read-only completed frame observation cannot change physical state during rejected queued access");
	static_cast<void>(fixture.Call(instance, "clear"));
	Require(RotationMatrixWords(task.Player().PhysicsToWorld()) == RotationMatrixWords(actual), "native Clear preserves the completed owned conversion without manufacturing another frame");
	fixture.Bindings->ReleaseInstance(instance);
	fixture.Second().Update();
	Require(!task.IsLive(), "rotation physical frame owner retires through actual deferred task release");
}

static std::uint32_t RotationReplayWord(std::istream& input) {
	std::uint64_t word = 0;
	if (!(input >> word) || word > std::numeric_limits<std::uint32_t>::max()) {
		throw std::runtime_error("rotation native replay requires a bounded unsigned word");
	}
	return static_cast<std::uint32_t>(word);
}

static void RequireRotationReplayWord(float actual, std::uint32_t expected, std::size_t case_index, std::size_t phase_index, std::string_view field) {
	const auto actual_word = std::bit_cast<std::uint32_t>(actual);
	if (actual_word != expected) {
		throw std::runtime_error("rotation native replay case " + std::to_string(case_index) + " phase " + std::to_string(phase_index) + " " + std::string(field) + " expected " + std::to_string(expected) + " actual " + std::to_string(actual_word));
	}
}

static void ReplayNativeRotation(const char* path) {
	std::ifstream input(path);
	std::string magic;
	Require(static_cast<bool>(input >> magic) && magic == "ERN1", "rotation native replay header");
	const auto count = RotationReplayWord(input);
	Require(count > 0 && count <= 256, "rotation native replay case bound");
	std::size_t phases = 0;
	for (std::size_t case_index = 0; case_index < count; ++case_index) {
		Fixture fixture(false);
		const auto instance = fixture.New();
		const auto task = fixture.Bindings->Task(instance);
		sgre::scene::EmoteRotationTransition component;
		static_cast<void>(fixture.Call(instance, "setCoord", {8.0f, -4.0f}));
		const auto phase_count = RotationReplayWord(input);
		Require(phase_count > 0 && phase_count <= 64 && phase_count <= 16384 - phases, "rotation native replay phase bound");
		for (std::size_t phase = 0; phase < phase_count; ++phase) {
			const auto operation = RotationReplayWord(input);
			const auto value = std::bit_cast<float>(RotationReplayWord(input));
			const auto duration = std::bit_cast<float>(RotationReplayWord(input));
			const auto easing = std::bit_cast<float>(RotationReplayWord(input));
			const auto current = RotationReplayWord(input);
			const auto degrees = RotationReplayWord(input);
			const auto getter = RotationReplayWord(input);
			std::array<std::uint32_t, 4> transform{};
			for (auto& word : transform) {
				word = RotationReplayWord(input);
			}
			if (operation == 0) {
				component.Set(value, duration, easing);
				static_cast<void>(fixture.Call(instance, "setRot", {value, duration, easing}));
			} else {
				Require(operation == 1 && duration == 0 && easing == 0, "rotation native replay operation");
				static_cast<void>(component.Advance(value));
				task.Player().Progress(value);
			}
			RequireRotationReplayWord(component.Current(), current, case_index, phase, "angular current");
			RequireRotationReplayWord(task.Player().Root().AngleDegrees(), degrees, case_index, phase, "root degrees");
			RequireRotationReplayWord(PublishedRotation(fixture, instance), getter, case_index, phase, "VM published radians");
			if (operation == 1) {
				const auto& root = task.Player().Root().Overrides();
				RequireRotationReplayWord(root.Coordinate[0], transform[0], case_index, phase, "coordinate x");
				RequireRotationReplayWord(root.Coordinate[1], transform[1], case_index, phase, "coordinate y");
				RequireRotationReplayWord(root.ZoomX, transform[2], case_index, phase, "zoom x");
				RequireRotationReplayWord(root.ZoomY, transform[3], case_index, phase, "zoom y");
			}
			++phases;
		}
	}
	input >> std::ws;
	Require(input.eof(), "rotation native replay trailing input");
	std::cout << "unchanged native rotation plans=" << count << " phases=" << phases << " exact current/root/actual VM words passed\n";
}

static void RequireMeshPositions(const sgre::scene::EmoteScenePlayer& player, std::size_t positions) {
	const auto* scene = player.PlainPlayer().Scene();
	Require(scene != nullptr && scene->GlobalRows().size() == 6, "mesh policy fixture must retain all six genuine mesh rows");
	for (const auto* row : scene->GlobalRows()) {
		const auto& command = scene->Packets().Nodes()[scene->Packets().Index(*row)].Command;
		Require(command.Geometry != nullptr && command.Geometry->Positions.size() == positions, "actual mesh policy did not reach completed scene vertex ownership");
	}
}

static void TestMeshDivisionActualGeometryAndTypedSurface() {
	Fixture fixture(false);
	const auto instance = fixture.New();
	const auto task = fixture.Bindings->Task(instance);
	Require(std::get<float>(fixture.Call(instance, "getMeshDivisionRatio", {1, 2})) == 1, "native plain mesh policy starts at1 and typed getter ignores extras");
	static_cast<void>(fixture.Call(instance, "setVisible", {true}));
	task.Player().Progress(0);
	RequireMeshPositions(task.Player(), 16);
	static_cast<void>(fixture.Call(instance, "setMeshDivisionRatio", {.5f, std::string("ignored"), 99}));
	Require(task.Player().Commands().MeshDivisionRatio() == .5f, "typed scalar setter changes the real plain policy immediately");
	RequireMeshPositions(task.Player(), 16);
	// Ratio alone does not dirty retained geometry. The actual next changing frame owns tessellation.
	task.Player().Progress(1);
	RequireMeshPositions(task.Player(), 9);
	static_cast<void>(fixture.Call(instance, "setMeshDivisionRatio", {2}));
	task.Player().Progress(1);
	RequireMeshPositions(task.Player(), 36);
	static_cast<void>(fixture.Call(instance, "setMeshDivisionRatio"));
	Require(std::get<float>(fixture.Call(instance, "getMeshDivisionRatio")) == 0, "missing typed float sees userdata and preserves initialized zero");
	static_cast<void>(fixture.Call(instance, "setMeshDivisionRatio", {true}));
	Require(std::get<float>(fixture.Call(instance, "getMeshDivisionRatio")) == 1, "typed bool mesh ratio uses numeric extraction");
	static_cast<void>(fixture.Call(instance, "setMeshDivisionRatio", {-0.0f}));
	Require(std::bit_cast<std::uint32_t>(std::get<float>(fixture.Call(instance, "getMeshDivisionRatio"))) == 0x80000000u, "mesh policy getter retains native signed zero");
}

static void TestMeshDivisionAdmissionQueueAndDetachedOwner() {
	Fixture fixture(false);
	const auto instance = fixture.New();
	const auto task = fixture.Bindings->Task(instance);
	static_cast<void>(fixture.Call(instance, "setMeshDivisionRatio", {2.0f}));
	for (const auto value : {-1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
		RequireFailure([&] {
			static_cast<void>(fixture.Call(instance, "setMeshDivisionRatio", {value}));
		},
		    "finite nonnegative");
	}
	Require(task.Player().Commands().MeshDivisionRatio() == 2, "rejected mesh policy leaves the actual plain owner unchanged");
	static_cast<void>(fixture.Call(instance, "setVisible", {true}));
	task.Player().Progress(0);
	fixture.Second().Draw(0);
	RequireFailure([&] {
		static_cast<void>(fixture.Call(instance, "setMeshDivisionRatio", {3.0f}));
	},
	    "unqueued");
	fixture.Secondary->SceneContext().Clear();
	Require(task.Player().Commands().MeshDivisionRatio() == 2, "queued geometry prevents mutation without dropping its real plain owner");
	fixture.Layers.ReleaseInstance(fixture.Leaf);
	Require(!task.IsLive() && std::get<float>(fixture.Call(instance, "getMeshDivisionRatio")) == 1, "detached typed mesh getter returns1 without a retired-player borrow");
	RequireFailure([&] {
		static_cast<void>(fixture.Call(instance, "setMeshDivisionRatio", {1.0f}));
	},
	    "no safe native property owner");
	fixture.Bindings->ReleaseInstance(instance);
}

static void TestLegacyScalarTypedConversionsAndIndependentState() {
	Fixture fixture(false);
	const auto instance = fixture.New();
	const std::array<std::string_view, 3> setters{"setBustScale", "setHairScale", "setPartsScale"};
	const std::array<std::string_view, 3> getters{"getBustScale", "getHairScale", "getPartsScale"};
	for (std::size_t index = 0; index < setters.size(); ++index) {
		Require(std::get<float>(fixture.Call(instance, getters[index], {1, 2, 3})) == 1, "empty legacy controller vector still owns its default scalar1");
		static_cast<void>(fixture.Call(instance, setters[index], {static_cast<std::int32_t>(index + 2), 500, true}));
	}
	for (std::size_t index = 0; index < getters.size(); ++index) {
		Require(std::get<float>(fixture.Call(instance, getters[index])) == static_cast<float>(index + 2), "legacy property fields are independent and ignore typed ordinary extras");
	}
	std::size_t conversions = 0;
	const auto value_class = fixture.Vm.CreateClass();
	fixture.Vm.NewSlot(value_class, "_tostring", fixture.Vm.CreateNativeClosure("legacy-scalar-no-string-conversion", [&](auto&, auto) {
		++conversions;
		throw std::runtime_error("numeric extraction must not call string conversion");
		return sgre::script::SquirrelNativeResult::NoValues();
	}));
	const auto value = fixture.Vm.CreateInstance(value_class);
	const auto value_root = fixture.Vm.Retain(value);
	for (std::size_t index = 0; index < setters.size(); ++index) {
		static_cast<void>(fixture.Call(instance, setters[index], {value}));
		Require(std::get<float>(fixture.Call(instance, getters[index])) == 0 && conversions == 0, "unsupported typed scalar zero-initializes without _tostring");
		static_cast<void>(fixture.Call(instance, setters[index], {true}));
		Require(std::get<float>(fixture.Call(instance, getters[index])) == 1, "legacy scalar bool extraction is numeric");
		static_cast<void>(fixture.Call(instance, setters[index]));
		Require(std::get<float>(fixture.Call(instance, getters[index])) == 0, "missing legacy scalar sees the captured userdata and becomes zero");
		static_cast<void>(fixture.Call(instance, setters[index], {-0.0f}));
		Require(std::bit_cast<std::uint32_t>(std::get<float>(fixture.Call(instance, getters[index]))) == 0x80000000u, "legacy scalar signed zero is not clamped or normalized");
		static_cast<void>(fixture.Call(instance, setters[index], {std::numeric_limits<float>::infinity()}));
		Require(std::isinf(std::get<float>(fixture.Call(instance, getters[index]))), "unconsumed native property bits are retained without invented clamping");
	}
}

static void TestScalarPauseClearQueueAndDetachedLifetime() {
	Fixture fixture(false);
	const auto instance = fixture.New();
	const auto task = fixture.Bindings->Task(instance);
	const std::array<std::string_view, 3> setters{"setBustScale", "setHairScale", "setPartsScale"};
	const std::array<std::string_view, 3> getters{"getBustScale", "getHairScale", "getPartsScale"};
	static_cast<void>(fixture.Call(instance, "pause", {true}));
	for (const auto method : setters) {
		static_cast<void>(fixture.Call(instance, method, {-2.5f}));
	}
	fixture.Second().Update();
	static_cast<void>(fixture.Call(instance, "setMeshDivisionRatio", {.5f}));
	static_cast<void>(fixture.Call(instance, "setVisible", {true}));
	task.Player().Progress(0);
	fixture.Second().Draw(0);
	for (const auto method : setters) {
		RequireFailure([&] {
			static_cast<void>(fixture.Call(instance, method, {7.0f}));
		},
		    "unqueued");
	}
	fixture.Secondary->SceneContext().Clear();
	static_cast<void>(fixture.Call(instance, "clear"));
	Require(task.Player().PlainPlayer().Scene() == nullptr && task.Player().MeshDivisionRatio() == .5f, "native plain Clear preserves its mesh policy independently of selected tree retirement");
	for (const auto method : getters) {
		Require(std::get<float>(fixture.Call(instance, method)) == -2.5f, "pause and Clear preserve original property values without a substitute controller");
	}
	fixture.Layers.ReleaseInstance(fixture.Leaf);
	Require(!task.IsLive(), "Layer-first retirement releases the real containing task");
	for (std::size_t index = 0; index < setters.size(); ++index) {
		static_cast<void>(fixture.Call(instance, setters[index]));
		static_cast<void>(fixture.Call(instance, setters[index], {std::string("ignored after detach"), true, 5}));
		Require(std::get<float>(fixture.Call(instance, getters[index])) == 1, "detached legacy setter/getter uses only the actual wrapper status");
	}
	fixture.Bindings->ReleaseInstance(instance);
}

static void TestLegacyControllerAdmissionRemainsExact() {
	for (const auto family : {"bustControl", "hairControl", "partsControl"}) {
		Fixture fixture(false, family);
		RequireFailure([&] {
			static_cast<void>(fixture.New());
		},
		    family);
		Require(fixture.Bindings->InstanceCount() == 0 && fixture.Second().Count() == 0 && fixture.Secondary->PlayerCount() == 0 && fixture.Layers.MotionResources(fixture.Leaf).ReferenceCount(fixture.Project) == 1, "enabled legacy controller still rejects and rolls back actual task/resource owners");
	}
}

static void TestWindSharedStreamAndActualSubsteps() {
	std::size_t seed_reads = 0;
	sgre::scene::EmoteWindRandom random([&] {
		++seed_reads;
		return std::int64_t{1700000000};
	});
	Fixture fixture(false, {}, &random);
	const auto first = fixture.New();
	const auto first_root = fixture.Vm.Retain(first);
	const auto second = fixture.New({std::int32_t{1}});
	const auto second_root = fixture.Vm.Retain(second);
	const auto first_task = fixture.Bindings->Task(first);
	const auto second_task = fixture.Bindings->Task(second);
	sgre::scene::EmoteWindRandom expected_random([] {
		return std::int64_t{1700000000};
	});
	sgre::scene::EmoteWindOwner expected_first(2, &expected_random), expected_second(2, &expected_random);
	const sgre::scene::EmoteWindParameters parameters{0, 100, 1, .5f, 1};
	expected_first.Start(parameters);
	expected_second.Start(parameters);
	static_cast<void>(fixture.Call(first, "startWind", {0, 100, 1, .5f, 1, std::string("ignored")}));
	static_cast<void>(fixture.Call(second, "startWind", {0, 100, 1, .5f, 1}));
	Require(seed_reads == 0 && !random.State() && first_task.Player().WindState().Control->BirthClock == 0, "constructing/starting separate manager wind owners cannot read the shared lazy seed");
	first_task.Player().Progress(0);
	expected_first.Advance(0);
	second_task.Player().Progress(0);
	expected_second.Advance(0);
	Require(seed_reads == 1 && random.State() == expected_random.State(), "first dirty zero-step consumes one trial per actual player on one application-wide stream");
	// Start does not mark propertyDirty: an already clean Progress0 must not perform a new trial.
	static_cast<void>(fixture.Call(first, "startWind", {0, 100, 2, .25f, 1}));
	const auto before = random.State();
	first_task.Player().Progress(0);
	Require(random.State() == before && first_task.Player().WindState().Control->BirthClock == 0, "wind reconfiguration must preserve the native clean zero-step phase");
	static_cast<void>(fixture.Call(first, "setCoord", {3, 4}));
	first_task.Player().Progress(2.5f);
	expected_first.Start({0, 100, 2, .25f, 1});
	float remaining = 2.5f;
	while (remaining > 0) {
		const float step = std::min(1.1f, remaining);
		expected_first.Advance(step);
		remaining -= step;
	}
	Require(random.State() == expected_random.State() && first_task.Player().Root().Overrides().Coordinate[0] == 3, "wind advances after genuine root publication on each native1.1 property substep, without borrowing manager random");
	Require(first_task.Player().WindState().Control->BirthClock == expected_first.Snapshot().Control->BirthClock, "wind cadence must retain per-substep rounding");
}

static void TestWindTypedBridgeStopAndServiceBoundary() {
	Fixture absent(false);
	const auto instance = absent.New();
	const auto task = absent.Bindings->Task(instance);
	static_cast<void>(absent.Call(instance, "stopWind", {std::string("ignored")}));
	static_cast<void>(absent.Call(instance, "startWind", {0, 0, 1, 1, 1}));
	Require(!task.Player().WindState().Control, "native all-zero/equal-endpoint stop remains valid without a random service");
	RequireFailure([&] {
		static_cast<void>(absent.Call(instance, "startWind", {0, 8, 1, .5f, 1}));
	},
	    "shared lazy random");
	Require(!task.Player().WindState().Control && task.Player().WindState().Parameters.Goal == 0, "missing shared service rejects before modifying native authored defaults");
	absent.Layers.ReleaseInstance(absent.Leaf);
	// Five explicit null cells discriminate typed extraction without assuming absent allocated slots are null.
	static_cast<void>(absent.Call(instance, "startWind", {{}, {}, {}, {}, {}}));
	static_cast<void>(absent.Call(instance, "stopWind", {true, 7}));
	absent.Bindings->ReleaseInstance(instance);

	std::size_t reads = 0;
	sgre::scene::EmoteWindRandom random([&] {
		++reads;
		return std::int64_t{1700000000};
	});
	Fixture fixture(false, {}, &random);
	const auto live = fixture.New();
	const auto live_task = fixture.Bindings->Task(live);
	static_cast<void>(fixture.Call(live, "startWind", {false, 16, -2, true, 2.5f}));
	auto state = live_task.Player().WindState();
	Require(state.Parameters.Start == 16 && state.Parameters.Goal == 0 && state.Parameters.Speed == 2 && state.Parameters.Minimum == 1 && state.Parameters.Maximum == 2.5f && state.Control->SignedVelocity == -1, "five-argument native bridge preserves bool/int/float conversion, negative-speed direction and metadata scale");
	static_cast<void>(fixture.Call(live, "startWind", {0, 8, std::string("not numeric"), 1, 1}));
	state = live_task.Player().WindState();
	Require(!state.Control && state.Parameters.Start == 16 && state.Parameters.Speed == 2 && reads == 0, "failed numeric extraction supplies zero speed and stops without overwriting the five authored words");
	static_cast<void>(fixture.Call(live, "startWind", {0, 8, 1, -1, 2}));
	Require(live_task.Player().WindState().Control->Minimum == -1, "native wind amplitude is not clamped to positive values");
}

static void TestWindPauseClearQueueAndLayerRetirement() {
	std::size_t reads = 0;
	sgre::scene::EmoteWindRandom random([&] {
		++reads;
		return std::int64_t{1700000000};
	});
	Fixture fixture(false, {}, &random);
	const auto instance = fixture.New();
	const auto task = fixture.Bindings->Task(instance);
	static_cast<void>(fixture.Call(instance, "startWind", {0, 100, 1, .5f, 1}));
	static_cast<void>(fixture.Call(instance, "pause", {true}));
	fixture.Second().Update();
	Require(reads == 0 && task.Player().WindState().Control->BirthClock == 0, "paused containing task must not advance or seed its wind");
	static_cast<void>(fixture.Call(instance, "clear"));
	Require(task.Player().PlainPlayer().Scene() == nullptr && task.Player().WindState().Control.has_value(), "native plain Clear preserves the separate wind controller");
	static_cast<void>(fixture.Call(instance, "pause", {false}));
	fixture.Second().Update();
	Require(reads == 1 && task.Player().WindState().Control->BirthClock < 0, "retained wind continues in the real task after plain Clear");
	// A separate selected fixture drives the actual queued draw guard.
	Fixture queued(false, {}, &random);
	const auto selected = queued.New();
	const auto selected_task = queued.Bindings->Task(selected);
	static_cast<void>(queued.Call(selected, "setVisible", {true}));
	selected_task.Player().Progress(0);
	queued.Second().Draw(0);
	RequireFailure([&] {
		static_cast<void>(queued.Call(selected, "startWind", {0, 8, 1, 1, 1}));
	},
	    "unqueued");
	RequireFailure([&] {
		static_cast<void>(queued.Call(selected, "stopWind"));
	},
	    "unqueued");
	queued.Secondary->SceneContext().Clear();
	queued.Layers.ReleaseInstance(queued.Leaf);
	Require(!selected_task.IsLive(), "Layer-first retirement closes actual wind/player before its borrowed random owner");
	static_cast<void>(queued.Call(selected, "startWind", {0, 8, 1, 1, 1}));
	static_cast<void>(queued.Call(selected, "stopWind"));
	queued.Bindings->ReleaseInstance(selected);
}

static void TestWindAssignmentRetainsDestinationControlAndSourceCache() {
	std::size_t seed_reads = 0;
	sgre::scene::EmoteWindRandom random([&] {
		++seed_reads;
		return std::int64_t{1700000000};
	});
	sgre::scene::EmoteWindOwner source(1, &random), destination(2, &random);
	source.Start({10, 30, 2, .25f, .75f});
	destination.Start({-8, 8, .001f, .5f, 1});
	destination.Advance(64);
	const auto preceding = destination.Snapshot();
	const auto random_before = random.State();
	Require(preceding.Control && std::any_of(preceding.Control->Bands.begin(), preceding.Control->Bands.end(), [](const auto& band) {
		return band.Active;
	}),
	    "assignment discriminator requires a genuinely spawned destination band");
	destination.AssignState(source);
	const auto assigned = destination.Snapshot();
	Require(assigned.Parameters.Start == 10 && assigned.Parameters.Goal == 30 && assigned.Parameters.Speed == 2 && assigned.Parameters.Minimum == .25f && assigned.Parameters.Maximum == .75f, "assignment copies all five source authored wind words");
	Require(assigned.Control && assigned.Control->Start == -4 && assigned.Control->Goal == 4 && assigned.Control->SignedVelocity == 1 && assigned.Control->BirthClock == 0, "precopy before Start retains destination physical endpoints while reconfiguring calibrated speed");
	for (std::size_t index = 0; index < assigned.Control->Bands.size(); ++index) {
		const auto& actual = assigned.Control->Bands[index];
		const auto& expected = preceding.Control->Bands[index];
		Require(actual.Active == expected.Active && actual.Position == expected.Position && actual.Strength == expected.Strength, "assignment retains destination band ownership and numeric state");
	}
	Require(seed_reads == 1 && random.State() == random_before, "wind assignment cannot reseed or consume the shared process-equivalent stream");

	sgre::scene::EmoteWindOwner stopped_defaults(1, nullptr);
	destination.AssignState(stopped_defaults);
	const auto cleared = destination.Snapshot();
	Require(!cleared.Control && cleared.Parameters.Start == 0 && cleared.Parameters.Goal == 0 && cleared.Parameters.Speed == 0 && cleared.Parameters.Minimum == 0 && cleared.Parameters.Maximum == 0, "zero source assignment stops destination and replaces its authored cache before Start");

	source.Stop();
	Require(!source.Snapshot().Control && source.Snapshot().Parameters.Goal == 30, "ordinary Stop retains nonzero source authored words");
	destination.AssignState(source);
	const auto restarted = destination.Snapshot();
	Require(restarted.Control && restarted.Control->Start == 5 && restarted.Control->Goal == 15 && restarted.Control->SignedVelocity == 1, "assigning stopped source cached words creates a destination-calibrated controller rather than copying source controller absence");
	Require(random.State() == random_before && seed_reads == 1, "restarting from assigned authored words remains lazy without wind birth work");
}

static void TestAssignStateRealControlsAndSourceFirstRetirement() {
	Fixture fixture(false);
	const auto source = fixture.New();
	const auto destination = fixture.New();
	const auto source_task = fixture.Bindings->Task(source);
	const auto destination_task = fixture.Bindings->Task(destination);
	static_cast<void>(fixture.Call(source, "setCoord", {8.0f, 16.0f, 4.0f}));
	static_cast<void>(fixture.Call(source, "setScale", {5.0f, 4.0f}));
	static_cast<void>(fixture.Call(source, "setColor", {std::int32_t{-1}, 4.0f}));
	static_cast<void>(fixture.Call(source, "setRot", {1.0f, 4.0f}));
	static_cast<void>(fixture.Call(source, "setVariable", {std::string("probe"), 16.0f, 4.0f}));
	Require(source_task.Player().ReadVariable("probe") == 16, "the actual probe metadata is immediateControl, so duration cannot create a scalar transition");
	source_task.Player().Progress(1);
	const auto source_color = PublishedColor(fixture, source);
	const auto source_tick = source_task.Player().Commands().ClockState().TickCount;
	const auto destination_tick = destination_task.Player().Commands().ClockState().TickCount;
	Require(std::holds_alternative<std::monostate>(fixture.Call(destination, "assignState", {source, std::int32_t{81}})), "typed assignState ignores extra operands and returns no physical values");
	Require(destination_task.Player().Coordinate() == source_task.Player().Coordinate() && destination_task.Player().Scale() == 2, "assignment copies real retained transition state into the existing destination owner");
	Require(PublishedColor(fixture, destination) == 0x808080ffu && destination_task.Player().Root().Overrides().Coordinate[0] == 0, "disabled Common assignment must not invent immediate plain-root publication");
	Require(destination_task.Player().Commands().ClockState().TickCount == destination_tick && source_task.Player().Commands().ClockState().TickCount == source_tick, "assignment never replaces plain owners or advances either actual clock");
	destination_task.Player().Progress(0);
	Require(PublishedColor(fixture, destination) == source_color && destination_task.Player().Root().Overrides().Coordinate[0] == 2, "the destination's retained dirty phase publishes copied controls at genuine zero progress");
	static_cast<void>(fixture.Call(destination, "pause", {true}));
	fixture.Bindings->ReleaseInstance(source);
	fixture.Second().Update();
	Require(!source_task.IsLive() && destination_task.IsLive(), "source task retirement must not retire destination controls or actual resource registration");
	static_cast<void>(fixture.Call(destination, "pause", {false}));
	destination_task.Player().Progress(2);
	Require(destination_task.Player().Coordinate() == std::array<float, 2>{6, 12} && destination_task.Player().Scale() == 4, "complete coordinate and scale transitions continue after source retirement rather than starting anew from copied current values");
	Require(destination_task.Player().ReadVariable("probe") == 16, "source immediate-control published storage remains exact after assignment and subsequent progress");
	Require(fixture.Layers.MotionResources(fixture.Leaf).ReferenceCount(fixture.Project) == 1 && fixture.Uploads == 1, "state transfer does not register or reupload the source resource");
}

static void TestAssignStateTypedOwnerDetachAndQueueGuards() {
	Fixture fixture(false);
	const auto source = fixture.New();
	const auto destination = fixture.New();
	const auto source_task = fixture.Bindings->Task(source);
	const auto destination_task = fixture.Bindings->Task(destination);
	static_cast<void>(fixture.Call(destination, "setCoord", {3.0f, 4.0f}));
	static_cast<void>(fixture.Call(destination, "assignState", {destination}));
	Require(destination_task.Player().Coordinate() == std::array<float, 2>{3, 4}, "self assignment retains owned state and skips copy phases");
	RequireFailure([&] {
		static_cast<void>(fixture.Call(destination, "assignState", {fixture.Vm.CreateTable()}));
	},
	    "typed source wrapper");
	Require(destination_task.Player().Coordinate() == std::array<float, 2>{3, 4}, "invalid typed source rejects before destination mutation");
	static_cast<void>(fixture.Call(source, "setVisible", {true}));
	source_task.Player().Progress(0);
	fixture.Second().Draw(0);
	RequireFailure([&] {
		static_cast<void>(fixture.Call(destination, "assignState", {source}));
	},
	    "unqueued");
	fixture.Secondary->SceneContext().Clear();
	Require(destination_task.Player().Coordinate() == std::array<float, 2>{3, 4}, "queued assignment rejection preserves destination state after the actual draw inputs retire");
	fixture.Layers.ReleaseInstance(fixture.Leaf);
	static_cast<void>(fixture.Call(destination, "assignState", {source}));
	static_cast<void>(fixture.Call(destination, "assignState", {std::int32_t{7}}));
	static_cast<void>(fixture.Call(destination, "assignState", {fixture.Vm.CreateInstance(fixture.Bindings->EmoteClass())}));
	Require(!source_task.IsLive() && !destination_task.IsLive(), "detached member guard skips expired task fields even with a failed source conversion");
}

static void TestAssignStateCommonPreparationAndDestinationPublication() {
	Fixture fixture;
	const auto source = fixture.New();
	const auto destination = fixture.New();
	const auto source_task = fixture.Bindings->Task(source);
	const auto destination_task = fixture.Bindings->Task(destination);
	static_cast<void>(fixture.Call(source, "setVisible", {true}));
	static_cast<void>(fixture.Call(destination, "setVisible", {true}));
	static_cast<void>(fixture.Call(source, "setCoord", {8.0f, -4.0f}));
	static_cast<void>(fixture.Call(source, "setScale", {1.5f}));
	static_cast<void>(fixture.Call(source, "setColor", {std::bit_cast<std::int32_t>(0x10203040u)}));
	source_task.Player().Progress(.25f);
	const auto source_counts = source_task.Player().PhysicalCounts();
	Require(source_counts.Updated == 6, "assignment source must genuinely advance all six active physical families before importing their state");
	const auto destination_tick = destination_task.Player().Commands().ClockState().TickCount;
	static_cast<void>(fixture.Call(destination, "assignState", {source}));
	Require(destination_task.Player().Root().Overrides().Coordinate == source_task.Player().Root().Overrides().Coordinate && PublishedColor(fixture, destination) == PublishedColor(fixture, source), "enabled Common assignment performs genuine destination zero-delta property/plain publication");
	Require(destination_task.Player().Commands().ClockState().TickCount == destination_tick && destination_task.Player().PhysicalCounts().AttachedPartsControls == source_counts.AttachedPartsControls, "Common assignment preserves actual destination ownership and the already constructed physical family graph");
	static_cast<void>(fixture.Call(destination, "pause", {true}));
	fixture.Bindings->ReleaseInstance(source);
	fixture.Second().Update();
	Require(!source_task.IsLive(), "actual source physical and listener owners retire before destination continuation");
	static_cast<void>(fixture.Call(destination, "pause", {false}));
	destination_task.Player().Progress(.25f);
	const auto conversion = destination_task.Player().PhysicsToWorld();
	Require(destination_task.Player().PhysicalCounts().Updated == 6 && std::isfinite(conversion.XX) && std::isfinite(conversion.YY), "destination continues all real physical families after source retirement");
}

static void TestAnimatingTypedBooleanExtrasAndDetachedReceiver() {
	std::size_t conversions = 0;
	Fixture fixture(false);
	const auto instance = fixture.New();
	const auto task = fixture.Bindings->Task(instance);
	const auto initial = fixture.Call(instance, "getAnimating");
	Require(std::holds_alternative<bool>(initial) && !std::get<bool>(initial), "native getAnimating returns SQBoolean false for an idle real owner, not an integer or native no-values result");
	const auto argument_class = fixture.Vm.CreateClass();
	fixture.Vm.NewSlot(argument_class, "_tostring", fixture.Vm.CreateNativeClosure("getAnimating-extra-conversion-tripwire", [&](auto&, auto) {
		++conversions;
		return sgre::script::SquirrelNativeResult::ReturnValue(std::string("unexpected conversion"));
	}));
	const auto argument = fixture.Vm.CreateInstance(argument_class);
	Require(!std::get<bool>(fixture.Call(instance, "getAnimating", {argument, std::numeric_limits<float>::quiet_NaN(), false})) && conversions == 0, "typed animation getter ignores every extra operand without string or numeric conversion");
	static_cast<void>(fixture.Call(instance, "setCoord", {8.0f, -4.0f, 4.0f}));
	Require(std::get<bool>(fixture.Call(instance, "getAnimating", {argument})), "actual live coordinate transition is visible through the typed native getter");
	fixture.Layers.ReleaseInstance(fixture.Leaf);
	const auto detached = fixture.Call(instance, "getAnimating", {argument, std::numeric_limits<float>::infinity()});
	Require(!task.IsLive() && std::holds_alternative<bool>(detached) && !std::get<bool>(detached) && conversions == 0, "detached animation getter returns native false before reading an expired task or converting ignored operands");
	fixture.Bindings->ReleaseInstance(instance);
	Require(fixture.Bindings->CanClose(), "detached animation receiver retains no independent physical owner");
}

static void TestAnimatingControllersPauseAndColorExclusion() {
	Fixture fixture(false);
	const auto instance = fixture.New();
	const auto task = fixture.Bindings->Task(instance);
	static_cast<void>(fixture.Call(instance, "setColor", {std::int32_t{-1}, 4.0f}));
	Require(!std::get<bool>(fixture.Call(instance, "getAnimating")), "native animation query excludes a newly active color transition");
	task.Player().Progress(1);
	Require(PublishedColor(fixture, instance) != 0x808080ffu && !std::get<bool>(fixture.Call(instance, "getAnimating")), "real color interpolation and publication cannot turn the color-excluding native query true");
	task.Player().Progress(3);
	static_cast<void>(fixture.Call(instance, "setCoord", {8.0f, -4.0f, 4.0f}));
	Require(std::get<bool>(fixture.Call(instance, "getAnimating")), "native coordinate current/start/target state reports active before any advance");
	static_cast<void>(fixture.Call(instance, "pause", {true}));
	const auto paused_coordinate = task.Player().Coordinate();
	fixture.Second().Update();
	Require(task.Player().Coordinate() == paused_coordinate && std::get<bool>(fixture.Call(instance, "getAnimating")), "actual task pause stops progression while the retained controller query remains true");
	static_cast<void>(fixture.Call(instance, "pause", {false}));
	fixture.Second().Update();
	Require(task.Player().Coordinate() != paused_coordinate && std::get<bool>(fixture.Call(instance, "getAnimating")), "resuming the real task advances the same unfinished coordinate transition");
	task.Player().Progress(4);
	Require(!std::get<bool>(fixture.Call(instance, "getAnimating")), "completed coordinate transition returns native false without requiring task retirement");
	static_cast<void>(fixture.Call(instance, "setScale", {2.0f, 2.0f}));
	Require(std::get<bool>(fixture.Call(instance, "getAnimating")), "the independent real scale transition participates in native animation query");
	task.Player().Progress(3);
	Require(RetainedScale(fixture, instance) == 2 && !std::get<bool>(fixture.Call(instance, "getAnimating")), "completed real scale transition leaves an idle animation query");
	static_cast<void>(fixture.Call(instance, "setRot", {.5f, 2.0f}));
	Require(std::get<bool>(fixture.Call(instance, "getAnimating")), "the primary cyclic rotation owner participates independently of Common extended rotation");
	task.Player().Progress(3);
	Require(!std::get<bool>(fixture.Call(instance, "getAnimating")), "completed primary rotation does not stay active solely because its published angle is nonzero");
}

static void TestAnimatingActualQueuedNativeAndSceneGuards() {
	Fixture fixture;
	const auto instance = fixture.New();
	const auto task = fixture.Bindings->Task(instance);
	static_cast<void>(fixture.Call(instance, "setVisible", {true}));
	task.Player().Progress(1);
	Require(task.Player().PhysicalCounts().Updated == 6 && !std::get<bool>(fixture.Call(instance, "getAnimating")), "genuine enabled Common simulation does not contribute to the native control animation predicate");
	static_cast<void>(fixture.Call(instance, "setScale", {2.0f, 4.0f}));
	task.Player().Progress(1);
	Require(std::get<bool>(fixture.Call(instance, "getAnimating")), "queued-query fixture must start with a genuine active transition");
	const auto scale = task.Player().Scale();
	fixture.Second().Draw(0);
	const auto queued = fixture.Secondary->SceneContext().Size();
	std::exception_ptr binding_failure;
	std::exception_ptr scene_failure;
	try {
		static_cast<void>(fixture.Call(instance, "getAnimating", {false, std::int32_t{77}}));
	} catch (...) {
		binding_failure = std::current_exception();
	}
	try {
		static_cast<void>(task.Player().Animating());
	} catch (...) {
		scene_failure = std::current_exception();
	}
	// End the genuine frame borrow before any assertion or guarded getter can obscure a failure during fixture teardown.
	fixture.Primary->SceneContext().Clear();
	fixture.Secondary->SceneContext().Clear();
	fixture.Primary->Context().Clear();
	fixture.Secondary->Context().Clear();
	Require(queued != 0 && binding_failure && scene_failure, "both actual native and direct scene queries must reject a genuinely queued draw occurrence");
	for (const auto& failure : {binding_failure, scene_failure}) {
		RequireFailure([&] {
			std::rethrow_exception(failure);
		},
		    "unqueued");
	}
	Require(task.Player().Scale() == scale && std::get<bool>(fixture.Call(instance, "getAnimating")) && task.Player().CanClose(), "rejected queries preserve the active controller and resume only after real frame inputs retire");
}

static void TestPassTypedVoidExtrasDetachedAndDeferredPublication() {
	std::size_t conversions = 0;
	Fixture fixture(false);
	const auto instance = fixture.New();
	const auto task = fixture.Bindings->Task(instance);
	const auto argument_class = fixture.Vm.CreateClass();
	fixture.Vm.NewSlot(argument_class, "_tostring", fixture.Vm.CreateNativeClosure("Pass-extra-conversion-tripwire", [&](auto&, auto) {
		++conversions;
		return sgre::script::SquirrelNativeResult::ReturnValue(std::string("unexpected conversion"));
	}));
	const auto argument = fixture.Vm.CreateInstance(argument_class);
	static_cast<void>(fixture.Call(instance, "setCoord", {8.0f, -4.0f, 8.0f}));
	static_cast<void>(fixture.Call(instance, "setScale", {2.0f, 8.0f}));
	static_cast<void>(fixture.Call(instance, "setColor", {std::int32_t{-1}, 8.0f}));
	static_cast<void>(fixture.Call(instance, "playTimeline", {std::string("phase")}));
	task.Player().Progress(1);
	const auto coordinate = task.Player().Coordinate();
	const auto scale = task.Player().Scale();
	const auto root_coordinate = task.Player().Root().Overrides().Coordinate;
	const auto color = PublishedColor(fixture, instance);
	const auto tick = task.Player().Commands().ClockState().TickCount;
	const auto entropy_reads = fixture.EntropyReads;
	Require(task.Player().ReadVariable("probe") == 7 && task.Player().Commands().ParameterValues().Read("probe") == 7, "Pass fixture must begin before the genuine finite timeline's remaining visible key");
	const auto result = fixture.Call(instance, "pass", {argument, std::numeric_limits<float>::quiet_NaN(), false});
	Require(std::holds_alternative<std::monostate>(result) && conversions == 0, "typed void Pass ignores extra operands without conversion and exposes native no-values through Invoke");
	Require(!task.Player().TimelinePlaying("phase") && task.Player().ReadVariable("probe") == 9, "ordinary finite Pass dispatches the remaining actual key then removes its active label");
	Require(task.Player().Commands().ParameterValues().Read("probe") == 7 && task.Player().Root().Overrides().Coordinate == root_coordinate && PublishedColor(fixture, instance) == color, "Pass stores deferred variable state without publishing a plain frame or root properties");
	Require(task.Player().Coordinate() == coordinate && task.Player().Scale() == scale && task.Player().Commands().ClockState().TickCount == tick && fixture.EntropyReads == entropy_reads, "Pass cannot advance or finish retained property transitions, clocks, or constructor random streams");
	task.Player().Progress(0);
	Require(task.Player().Commands().ParameterValues().Read("probe") == 9 && task.Player().Coordinate() == coordinate && task.Player().Scale() == scale && task.Player().Commands().ClockState().TickCount == tick, "the following genuine zero phase publishes Pass writes without inventing elapsed time");
	Require(std::holds_alternative<std::monostate>(fixture.Call(instance, "pass")) && !task.Player().TimelinePlaying(), "repeated Pass on an empty active list is a real no-values operation");
	fixture.Layers.ReleaseInstance(fixture.Leaf);
	Require(!task.IsLive() && std::holds_alternative<std::monostate>(fixture.Call(instance, "pass", {argument, std::numeric_limits<float>::infinity()})) && conversions == 0, "detached typed Pass returns before accessing expired task state or converting ignored operands");
	fixture.Bindings->ReleaseInstance(instance);
}

static void TestPassQueuedGuardAndRetainedCompletedCommonFrame() {
	Fixture fixture;
	const auto instance = fixture.New();
	const auto task = fixture.Bindings->Task(instance);
	static_cast<void>(fixture.Call(instance, "setVisible", {true}));
	static_cast<void>(fixture.Call(instance, "playTimeline", {std::string("phase")}));
	task.Player().Progress(1);
	Require(task.Player().PhysicalCounts().Updated == 6, "Pass Common fixture requires a genuine completed six-family physical phase");
	const auto tick = task.Player().Commands().ClockState().TickCount;
	const auto conversion = task.Player().PhysicsToWorld();
	const auto root_coordinate = task.Player().Root().Overrides().Coordinate;
	fixture.Second().Draw(0);
	const auto queued = fixture.Secondary->SceneContext().Size();
	std::exception_ptr failure;
	try {
		static_cast<void>(fixture.Call(instance, "pass"));
	} catch (...) {
		failure = std::current_exception();
	}
	fixture.Primary->SceneContext().Clear();
	fixture.Secondary->SceneContext().Clear();
	fixture.Primary->Context().Clear();
	fixture.Secondary->Context().Clear();
	Require(queued != 0 && failure, "typed Pass must reject the real queued owner before dispatching remaining timeline keys");
	RequireFailure([&] {
		std::rethrow_exception(failure);
	},
	    "unqueued");
	Require(task.Player().TimelinePlaying("phase") && task.Player().ReadVariable("probe") == 7, "queued Pass rejection preserves active playback and pending control storage");
	static_cast<void>(fixture.Call(instance, "pass"));
	const auto retained_conversion = task.Player().PhysicsToWorld();
	Require(task.Player().Commands().ClockState().TickCount == tick && task.Player().Root().Overrides().Coordinate == root_coordinate && task.Player().PhysicalCounts().Updated == 6, "unblocked Pass preserves the completed Common/plain frame rather than running physics or resetting its counts");
	Require(retained_conversion.XX == conversion.XX && retained_conversion.XY == conversion.XY && retained_conversion.YX == conversion.YX && retained_conversion.YY == conversion.YY && retained_conversion.TranslationX == conversion.TranslationX && retained_conversion.TranslationY == conversion.TranslationY, "Pass leaves the actual last completed Common coordinate conversion unchanged");
	Require(!task.Player().TimelinePlaying("phase") && task.Player().ReadVariable("probe") == 9 && task.Player().Commands().ParameterValues().Read("probe") == 7, "successful native Pass remains a deferred control operation even with real Common enabled");
}

static void TestPlayingInfoOriginalAliasesAndDetachedReceiver() {
	auto document = OwnerDocument(false);
	auto& definitions = std::get<sgre::resource::PsbArray>(MutableField(MutableField(document->Root, "metadata"), "timelineControl").Data);
	auto second = definitions.front();
	Set(second, "label", Atom(std::string("Ａ１")));
	definitions.push_back(std::move(second));
	Fixture fixture(false, {}, nullptr, std::move(document));
	const auto instance = fixture.New();
	auto task = fixture.Bindings->Task(instance);
	std::size_t conversions = 0;
	const auto ignored = fixture.Vm.CreateTable();
	fixture.Vm.NewSlot(ignored, "_tostring", fixture.Vm.CreateNativeClosure("unused.info.stringify", [&](auto&, auto) {
		++conversions;

		return sgre::script::SquirrelNativeResult::ReturnValue(std::string("unused"));
	}));
	for (const auto* name : {"getPlayingTimelineInfoList", "getPlayingMotionInfoList"}) {
		const auto empty = fixture.Call(instance, name, {ignored, true});
		Require(fixture.Vm.Type(empty) == sgre::script::SquirrelValueType::Array && fixture.Vm.Length(empty) == 0, "an attached original info query must return an actual empty array before first playback");
	}
	static_cast<void>(fixture.Call(instance, "playTimeline", {std::string("phase"), std::int32_t{2}}));
	static_cast<void>(fixture.Call(instance, "playMotion", {std::string("Ａ１"), std::int32_t{1}}));
	static_cast<void>(fixture.Call(instance, "setTimelineBlendRatio", {std::string("phase"), .25f, 4.0f}));
	for (const auto* name : {"getPlayingTimelineInfoList", "getPlayingMotionInfoList"}) {
		const auto list = fixture.Call(instance, name, {ignored, std::numeric_limits<float>::infinity()});
		Require(fixture.Vm.Length(list) == 2, "both original aliases must enumerate the same real active scheduler");
		const auto first = fixture.Vm.ReadSlot(list, std::int32_t{0});
		const auto second_info = fixture.Vm.ReadSlot(list, std::int32_t{1});
		Require(fixture.Vm.Type(first) == sgre::script::SquirrelValueType::Table && fixture.Vm.Length(first) == 3 && sgre::script::StringText(fixture.Vm.ReadSlot(first, "label")) == "phase" && std::get<std::int32_t>(fixture.Vm.ReadSlot(first, "flags")) == 3 && std::get<float>(fixture.Vm.ReadSlot(first, "blendRatio")) == 1 && sgre::script::StringText(fixture.Vm.ReadSlot(second_info, "label")) == "A1" && std::get<std::int32_t>(fixture.Vm.ReadSlot(second_info, "flags")) == 1, "actual info tables must contain only original label, flags OR1 and cached float blendRatio in playback order");
	}
	static_cast<void>(fixture.Call(instance, "clear"));
	Require(fixture.Vm.Length(fixture.Call(instance, "getPlayingTimelineInfoList")) == 2 && task.Player().PlainPlayer().Scene() == nullptr, "native plain Clear must preserve actual timeline playback information independently of selected geometry");
	fixture.Layers.ReleaseInstance(fixture.Leaf);
	for (const auto* name : {"getPlayingTimelineInfoList", "getPlayingMotionInfoList"}) {
		Require(std::holds_alternative<std::monostate>(fixture.Call(instance, name, {ignored})), "detached original raw-VM info query returns no values before borrowing expired task state");
	}
	Require(!task.IsLive() && conversions == 0, "ignored info operands must never stringify or retain a detached physical owner");
	fixture.Bindings->ReleaseInstance(instance);
	Require(fixture.Bindings->CanClose(), "the detached info-query wrapper must release without any independent physical owner");
}

static void TestSkipTypedVoidExtrasPausedCurrentAndDetachedReceiver() {
	std::size_t conversions = 0;
	Fixture fixture(false);
	const auto instance = fixture.New();
	const auto receiver_root = fixture.Vm.Retain(instance);
	const auto task = fixture.Bindings->Task(instance);
	const auto argument_class = fixture.Vm.CreateClass();
	fixture.Vm.NewSlot(argument_class, "_tostring", fixture.Vm.CreateNativeClosure("Skip-extra-conversion-tripwire", [&](auto&, auto) {
		++conversions;
		return sgre::script::SquirrelNativeResult::ReturnValue(std::string("unexpected conversion"));
	}));
	const auto argument = fixture.Vm.CreateInstance(argument_class);
	const auto argument_root = fixture.Vm.Retain(argument);
	static_cast<void>(fixture.Call(instance, "setCoord", {8.0f, -4.0f, 8.0f}));
	static_cast<void>(fixture.Call(instance, "setScale", {2.0f, 8.0f}));
	static_cast<void>(fixture.Call(instance, "setRot", {.5f, 8.0f}));
	static_cast<void>(fixture.Call(instance, "setColor", {std::bit_cast<std::int32_t>(0x10203040u), 8.0f}));
	static_cast<void>(fixture.Call(instance, "pause", {true}));
	const auto tick = task.Player().Commands().ClockState().TickCount;
	const auto entropy = fixture.EntropyReads;
	Require(task.Player().Coordinate()[0] == 0 && task.Player().Scale() == 1 && std::get<bool>(fixture.Call(instance, "getAnimating")), "Skip must begin with genuine pending property transitions on a paused actual task");
	const auto result = fixture.Call(instance, "skip", {argument, std::numeric_limits<float>::quiet_NaN(), false});
	Require(std::holds_alternative<std::monostate>(result) && conversions == 0, "typed no-values Skip ignores all extras without conversion; Invoke exposes only the outcome class");
	Require(task.Player().Coordinate() == std::array<float, 2>{8, -4} && task.Player().Scale() == 2 && !std::get<bool>(fixture.Call(instance, "getAnimating")), "Skip finishes retained coordinate, scale and angular Current without a time step");
	Require(task.Player().Root().Overrides().Coordinate == std::array<float, 3>{0, 0, 0} && task.Player().Root().Overrides().ZoomX == 1 && PublishedRotation(fixture, instance) == 0 && PublishedColor(fixture, instance) == 0x808080ffu, "native root/color/angle getters retain the completed publication until the real property phase");
	fixture.Second().Update();
	Require(fixture.Second().State(task.Handle()).Status == sgre::scene::TaskStatus::Complete && task.Player().Commands().ClockState().TickCount == tick && fixture.EntropyReads == entropy, "Skip preserves actual pause status and does not advance clock or constructor streams");
	Require(std::holds_alternative<std::monostate>(fixture.Call(instance, "skip")) && task.Player().Scale() == 2, "repeated no-argument Skip leaves completed Current intact");
	static_cast<void>(fixture.Call(instance, "pause", {false}));
	task.Player().Progress(0);
	Require(task.Player().Root().Overrides().Coordinate == std::array<float, 3>{8, -4, 0} && task.Player().Root().Overrides().ZoomX == 2 && PublishedColor(fixture, instance) == 0x10203040u && PublishedRotation(fixture, instance) == .5f, "only the genuine next dirty zero phase publishes finished property controls without restarting them");
	fixture.Layers.ReleaseInstance(fixture.Leaf);
	Require(!task.IsLive() && std::holds_alternative<std::monostate>(fixture.Call(instance, "skip", {argument, std::numeric_limits<float>::infinity()})) && conversions == 0, "detached Skip returns before borrowing retired task state or inspecting ignored extras");
	fixture.Bindings->ReleaseInstance(instance);
}

static void TestSkipQueuedGuardAndWarmCommonClearProvenance() {
	Fixture fixture;
	const auto instance = fixture.New();
	const auto receiver_root = fixture.Vm.Retain(instance);
	const auto task = fixture.Bindings->Task(instance);
	static_cast<void>(fixture.Call(instance, "setVisible", {true}));
	static_cast<void>(fixture.Call(instance, "setCoord", {8.0f, -4.0f, 8.0f}));
	static_cast<void>(fixture.Call(instance, "playTimeline", {std::string("phase")}));
	task.Player().Progress(1);
	Require(task.Player().PhysicalCounts().Updated == 6, "Skip Common preparation must complete the actual visible six-family physics phase");
	const auto coordinate = task.Player().Coordinate();
	const auto published_coordinate = task.Player().Root().Overrides().Coordinate;
	const auto tick = task.Player().Commands().ClockState().TickCount;
	const auto conversion = task.Player().PhysicsToWorld();
	const auto entropy = fixture.EntropyReads;
	fixture.Second().Draw(0);
	const auto queued = fixture.Secondary->SceneContext().Size();
	std::exception_ptr binding_failure;
	std::exception_ptr scene_failure;
	try {
		static_cast<void>(fixture.Call(instance, "skip"));
	} catch (...) {
		binding_failure = std::current_exception();
	}
	try {
		task.Player().Skip();
	} catch (...) {
		scene_failure = std::current_exception();
	}
	fixture.Primary->SceneContext().Clear();
	fixture.Secondary->SceneContext().Clear();
	fixture.Primary->Context().Clear();
	fixture.Secondary->Context().Clear();
	Require(queued != 0 && binding_failure && scene_failure, "both native and direct Skip must reject genuine queued draw owners before resetting any controller/history");
	for (const auto& failure : {binding_failure, scene_failure}) {
		RequireFailure([&] {
			std::rethrow_exception(failure);
		},
		    "unqueued");
	}
	Require(task.Player().Coordinate() == coordinate && task.Player().TimelinePlaying("phase"), "failed queued Skip preserves unfinished Current and finite playback");
	static_cast<void>(fixture.Call(instance, "skip"));
	const auto retained = task.Player().PhysicsToWorld();
	Require(task.Player().Coordinate() == std::array<float, 2>{8, -4} && !task.Player().TimelinePlaying("phase") && task.Player().Root().Overrides().Coordinate == published_coordinate, "unqueued Skip finishes Current and removes finite playback while retaining the prior completed root");
	Require(task.Player().Commands().ClockState().TickCount == tick && task.Player().PhysicalCounts().Updated == 6 && fixture.EntropyReads == entropy, "Common Skip changes reset/history state without solver, plain, time or RNG work");
	Require(retained.XX == conversion.XX && retained.XY == conversion.XY && retained.YX == conversion.YX && retained.YY == conversion.YY && retained.TranslationX == conversion.TranslationX && retained.TranslationY == conversion.TranslationY, "Common Skip retains the actual completed inverse matrix");
	static_cast<void>(fixture.Call(instance, "clear"));
	Require(task.Player().PlainPlayer().Scene() == nullptr && task.IsLive(), "warm Skip preserves raw-anchor provenance required by real Common Clear");
	task.Player().Progress(1);
	Require(task.Player().PhysicalCounts().Updated == 0 && task.Player().PhysicalCounts().MeshWrites == 0 && fixture.Layers.MotionResources(fixture.Leaf).ReferenceCount(fixture.Project) == 1, "the next genuine post-Clear phase preserves completed history and enclosing registration without inventing retired UID control instances");
}

int main(int argc, char** argv) {
	try {
		TestSkipTypedVoidExtrasPausedCurrentAndDetachedReceiver();
		TestPlayingInfoOriginalAliasesAndDetachedReceiver();
		TestSkipQueuedGuardAndWarmCommonClearProvenance();
		TestPassTypedVoidExtrasDetachedAndDeferredPublication();
		TestPassQueuedGuardAndRetainedCompletedCommonFrame();
		TestAnimatingTypedBooleanExtrasAndDetachedReceiver();
		TestAnimatingControllersPauseAndColorExclusion();
		TestAnimatingActualQueuedNativeAndSceneGuards();
		TestWindSharedStreamAndActualSubsteps();
		TestWindTypedBridgeStopAndServiceBoundary();
		TestWindPauseClearQueueAndLayerRetirement();
		TestWindAssignmentRetainsDestinationControlAndSourceCache();
		TestAssignStateRealControlsAndSourceFirstRetirement();
		TestAssignStateTypedOwnerDetachAndQueueGuards();
		TestAssignStateCommonPreparationAndDestinationPublication();
		TestMeshDivisionActualGeometryAndTypedSurface();
		TestMeshDivisionAdmissionQueueAndDetachedOwner();
		TestLegacyScalarTypedConversionsAndIndependentState();
		TestScalarPauseClearQueueAndDetachedLifetime();
		TestLegacyControllerAdmissionRemainsExact();
		TestActualFactorySixFamiliesAndDeferredRetirement();
		TestNativeOptionalArgumentAliasAndParentDetach();
		TestRejectedActualProducersAndRollback();
		TestStringConversionAndReceiverRecheck();
		TestInvalidServicesDoNotPublishAClass();
		TestColorConstructorSequenceAndPropertyPublication();
		TestColorRetargetBoundsAndQueuedGuards();
		TestColorNativePackingDomain();
		TestPauseAndPlainClearOwners();
		TestDetachedColorClearAndPause();
		TestStoredPropertyPhaseAndReentry();
		TestCoordinateConstructorAndPropertyPublication();
		TestCoordinateRetargetAndNativeConversions();
		TestCoordinateAdmissionAndQueuedOwner();
		TestCoordinateUnitsBoundsAndSignedZero();
		TestDetachedCoordinateReceiverAndRetirement();
		TestScaleConstructorChainAndPropertyPublication();
		TestScaleRetargetAndNumericConversions();
		TestScaleAdmissionAndQueuedOwners();
		TestScalePauseClearAndDetachedLifetime();
		TestRotationConstructorChainAndPublication();
		TestRotationShortestArcRetargetAndBoundedWork();
		TestRotationNativeConversionsAndQueuedOwner();
		TestRotationPauseClearAndDetachedLifetime();
		TestPrimaryRotationPreservesSeparateCommonInput();
		if (argc == 3 && std::string_view(argv[1]) == "--native-rotation") {
			ReplayNativeRotation(argv[2]);
		} else {
			Require(argc == 1, "expected optional --native-rotation expected-only fixture");
		}
		TestColdCommonClearThenActualTaskRetirement();
		std::cout << "actual SQEmote/Layer/PSB/Second-task control and deferred owner lifetime passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
