#include "sgre/scene/motion_shape_owner.hpp"

#include "motion_shape_fixture.hpp"
#include "sgre/scene/motion_layer_uids.hpp"
#include "sgre/scene/motion_player_task.hpp"
#include "sgre/scene/motion_scene_draw_context.hpp"
#include "sgre/scene/motion_scene_evaluation.hpp"
#include "sgre/scene/motion_scene_player.hpp"
#include "sgre/scene/motion_selected_layer_nodes.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view context) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, error.what());
		return;
	}
	throw std::runtime_error("shape owner accepted a stale or foreign actual reference");
}

static sgre::resource::PsbValue ShapeLayer(std::uint32_t shape) {
	using motion_shape_fixture::Atom;
	using sgre::resource::PsbArray;
	using sgre::resource::PsbObject;
	PsbArray frames;
	for (const auto time : {0, 4, 8, 12}) {
		const auto content = Atom(PsbObject{{"mask", Atom(std::int64_t{0x463})}, {"coord", Atom(PsbArray{Atom(time == 8 ? 11.0 : 3.0), Atom(5.0), Atom(0.0)})}, {"ox", Atom(1.0)}, {"oy", Atom(2.0)}, {"zx", Atom(shape == 2 ? -2.0 : 2.0)}, {"zy", Atom(3.0)}, {"opa", Atom(std::int64_t{255})}});
		frames.push_back(Atom(PsbObject{{"time", Atom(static_cast<double>(time))}, {"type", Atom(std::int64_t{time == 4 || time == 12 ? 0 : 2})}, {"content", content}}));
	}
	const auto shape_word = shape == 1 ? Atom(true) : shape == 9 ? Atom(9.75)
	                                                             : Atom(static_cast<std::int64_t>(shape));
	return Atom(PsbObject{{"label", Atom("actual-label-" + std::to_string(shape))}, {"uid", Atom("shape-uid-" + std::to_string(shape))}, {"shape", shape_word}, {"type", Atom(std::int64_t{1})}, {"inheritMask", Atom(std::int64_t{4})}, {"coordinate", Atom(std::int64_t{0})}, {"frameList", Atom(std::move(frames))}, {"transformOrder", Atom(PsbArray{Atom(std::int64_t{0}), Atom(std::int64_t{3}), Atom(std::int64_t{2}), Atom(std::int64_t{1})})}, {"children", Atom(PsbArray{})}, {"meshTransform", Atom(std::int64_t{0})}, {"stencilType", Atom(std::int64_t{0})}, {"parameterize", Atom(std::monostate{})}, {"joinTarget", Atom(false)}, {"groundCorrection", Atom(false)}});
}

static motion_shape_fixture::Bytes ShapeDocument() {
	using motion_shape_fixture::Atom;
	using sgre::resource::PsbArray;
	using sgre::resource::PsbObject;
	PsbArray layers;
	PsbArray order;
	PsbObject index;
	for (const auto shape : {0u, 1u, 2u, 3u, 9u}) {
		index.emplace_back("lookup-" + std::to_string(shape), Atom(static_cast<std::int64_t>(layers.size())));
		order.push_back(Atom(static_cast<std::int64_t>(layers.size())));
		layers.push_back(ShapeLayer(shape));
	}
	const auto priority = Atom(PsbObject{{"time", Atom(0.0)}, {"type", Atom(std::int64_t{1})}, {"content", Atom(std::move(order))}});
	const auto terminal = Atom(PsbObject{{"time", Atom(12.0)}, {"type", Atom(std::int64_t{1})}, {"content", Atom(std::monostate{})}});
	const auto timeline = Atom(PsbObject{{"type", Atom(std::int64_t{0})}, {"bounds", Atom(PsbObject{{"left", Atom(0.0)}, {"top", Atom(0.0)}, {"right", Atom(32.0)}, {"bottom", Atom(32.0)}})}, {"lastTime", Atom(12.0)}, {"loopTime", Atom(-1.0)}, {"layer", Atom(std::move(layers))}, {"layerIndexMap", Atom(std::move(index))}, {"priority", Atom(PsbArray{priority, terminal})}, {"tag", Atom(PsbArray{})}, {"parameter", Atom(std::monostate{})}, {"variable", Atom(std::monostate{})}, {"parameterize", Atom(std::monostate{})}});
	const auto character = Atom(PsbObject{{"type", Atom(std::int64_t{0})}, {"motion", Atom(PsbObject{{"main", timeline}})}});
	const auto root = Atom(PsbObject{{"id", Atom(std::string("motion"))}, {"spec", Atom(std::string("win"))}, {"version", Atom(3.03)}, {"label", Atom(std::string("Serialized shape owner fixture"))}, {"easing", Atom(PsbArray{})}, {"source", Atom(PsbObject{})}, {"object", Atom(PsbObject{{"fixture", character}})}, {"sourceIconRenameMap", Atom(PsbObject{})}});
	return motion_shape_fixture::Writer{}.Serialize(root);
}

struct Fixture {
	std::size_t Uploads = 0;
	std::array<std::uint32_t, sgre::scene::kMotionSharedSeedWords> Seed{};
	sgre::scene::TextureCache Cache;
	sgre::scene::MotionAssetCatalog Catalog;
	std::shared_ptr<const sgre::scene::LoadedMotionAsset> Asset;
	sgre::scene::MotionResourceManager Manager;
	sgre::scene::MotionProjectId Project;
	sgre::core::FrameClock Clock;
	std::unique_ptr<sgre::scene::MotionManagerRuntime> Runtime;
	sgre::scene::MotionLayerTasks Tasks;

	Fixture()
	    : Cache([this](auto, const auto&) -> sgre::render::SpriteTextureHandle {
		      ++Uploads;
		      throw std::runtime_error("native type1 shape cannot request an atlas upload");
	      },
	          [](auto) {
	          }),
	      Catalog(Cache), Manager(Cache), Clock(0), Tasks(8) {
		const auto bytes = std::make_shared<const std::vector<std::uint8_t>>(ShapeDocument());
		const auto document = std::make_shared<const sgre::resource::PsbDocument>(sgre::resource::ReadPsbMetadata(*bytes, bytes->size()));
		Require(document->Root.SerializedOffset && document->SerializedSize == bytes->size() && document->Chunks.empty(), "fixture must cross the real PSB3 byte parser without substitute atoms or texture chunks");
		Asset = Catalog.Prepare(document, bytes);
		Project = Manager.Register(Asset->Identity, Asset->Model, *Asset->Backing);
		sgre::scene::MotionManagerRuntimeServices services;
		services.ReadSourceAliases = [this](auto project) {
			Require(project == Project, "shape fixture resolved a foreign registered source owner");
			return std::span<const sgre::scene::MotionSourceAlias>(Asset->Aliases);
		};
		services.SceneAssets = &Catalog;
		services.ReadRegisteredAsset = [this](auto project) -> const sgre::scene::LoadedMotionAsset& {
			Require(project == Project, "shape fixture resolved a foreign actual asset");
			return *Asset;
		};
		Runtime = std::make_unique<sgre::scene::MotionManagerRuntime>(Manager, Clock, Seed, std::move(services), 32);
	}

	~Fixture() noexcept {
		try {
			Runtime->SceneContext().Clear();
			Tasks.Clear();
			Runtime->Close();
			Runtime.reset();
			static_cast<void>(Manager.Unregister(Project));
		} catch (...) {
			std::terminate();
		}
	}
};

struct Watch final : sgre::scene::MotionLayerUidObserver {
	sgre::scene::MotionPlayerLayerUids& Uids;
	std::size_t Added = 0;
	std::size_t Removed = 0;
	std::size_t Subscribed = 0;
	bool Valid = true;
	std::array<std::string, 5> Names{{"shape-uid-0", "shape-uid-1", "shape-uid-2", "shape-uid-3", "shape-uid-9"}};

	explicit Watch(sgre::scene::MotionPlayerLayerUids& uids)
	    : Uids(uids) {
		try {
			for (const auto& name : Names) {
				Uids.Subscribe(name, *this);
				++Subscribed;
			}
		} catch (...) {
			while (Subscribed != 0) {
				Uids.Unsubscribe(Names[--Subscribed], *this);
			}
			throw;
		}
	}
	~Watch() noexcept {
		try {
			while (Subscribed != 0) {
				Uids.Unsubscribe(Names[--Subscribed], *this);
			}
		} catch (...) {
			std::terminate();
		}
	}
	void LayerAdded(std::string_view, const sgre::scene::MotionLayerUidMatch& match) noexcept override {
		try {
			const auto shape = match.Layer.ShapeSnapshot();
			Valid = Valid && match.Layer.Node().Prepared->Type == 1 && !match.Layer.Node().DrawRow && std::all_of(shape.Values.begin(), shape.Values.end(), [](float value) {
				return value == 0;
			});
			++Added;
		} catch (...) {
			Valid = false;
		}
	}
	void LayerRemoved(std::string_view, const sgre::scene::MotionLayerUidMatch& match) noexcept override {
		try {
			static_cast<void>(match.Layer.ShapeSnapshot());
			++Removed;
		} catch (...) {
			Valid = false;
		}
	}
};

static void TestRealSelectedShapeOwner() {
	Fixture fixture;
	auto task = sgre::scene::MotionPlayerTask::Attach(*fixture.Runtime, fixture.Tasks, false, "fixture", [](const auto&) {
	});
	Watch watch(task.Commands().LayerUids());
	task.SetVisible(true);
	task.Play("main");
	const auto* scene = task.GenericPlayer()->Scene();
	Require(scene && watch.Added == 5 && watch.Valid && fixture.Runtime->Identifiers().LiveCount() == 5, "actual selected type1 construction must own five real UID nodes while the separate root remains the zero sentinel");
	std::array<sgre::scene::MotionSceneLayerReference, 5> references;
	std::size_t index = 0;
	for (const auto type : {0u, 1u, 2u, 3u, 9u}) {
		const auto found = scene->FindLayer("lookup-" + std::to_string(type));
		Require(found && scene->LayerType(*found) == 1 && scene->Shape(*found).Type == type, "getter must use serialized layerIndexMap rather than actual label");
		references[index++] = *found;
		const auto initial = scene->Shape(*found);
		Require(std::all_of(initial.Values.begin(), initial.Values.end(), [](float value) {
			return value == 0;
		}),
		    "unupdated shape getter must copy genuine zero initialization");
	}
	Require(!scene->FindLayer("actual-label-1"), "labels cannot replace the actual layerIndexMap protocol");
	auto& group = fixture.Tasks.Group(sgre::scene::MotionTaskGroup::Second);
	task.SetTickCount(0);
	group.Update();
	const auto point = scene->Shape(references[0]);
	const auto circle = scene->Shape(references[1]);
	const auto rectangle = scene->Shape(references[2]);
	const auto quad = scene->Shape(references[3]);
	Require(point.Values == std::array<float, 8>{3, 5, 0, 0, 0, 0, 0, 0} && !point.Contains({3, 5}), "point retains real numeric coordinate but native contains is always false");
	Require(circle.Values == std::array<float, 8>{3, 5, 16, 0, 0, 0, 0, 0} && circle.Contains({19, 5}) && !circle.Contains({19.25f, 5}), "circle uses native ScaleX*16*.5 and closed radius boundary");
	Require(rectangle.Values == std::array<float, 8>{19, -19, -13, 29, 0, 0, 0, 0} && !rectangle.Contains({3, 5}), "rectangle preserves negative scale without abs or endpoint normalization");
	Require(quad.Values == std::array<float, 8>{-15, -25, 17, -25, 17, 23, -15, 23} && quad.Contains({-15, -25}) && quad.Contains({17, 23}) && !quad.Contains({17.25f, 23}), "quad uses real matrix, physical frame pivot and native closed polygon edges");
	Require(scene->Shape(references[4]).Values == std::array<float, 8>{}, "unknown shape keeps constructor payload unchanged");
	Require(scene->Packets().Nodes().empty() && scene->GlobalRows().empty() && fixture.Uploads == 0 && fixture.Cache.Count() == 0, "native shape owner must produce neither atlas geometry nor GPU rows");

	task.SetTickCount(4);
	task.SetCoordinate3({100, 200, 0});
	group.Update();
	const auto hidden_circle = scene->Shape(references[1]);
	const auto hidden_quad = scene->Shape(references[3]);
	Require(hidden_circle.Values == std::array<float, 8>{100, 200, 8, 0, 0, 0, 0, 0} && hidden_quad.Values == std::array<float, 8>{92, 192, 108, 192, 108, 208, 92, 208}, "native geometry Changed updates even hidden/inactive shapes using actual composed parent numeric state");
	group.Update();
	Require(scene->Shape(references[1]).Values == hidden_circle.Values && scene->Shape(references[3]).Values == hidden_quad.Values, "unchanged hidden geometry retains complete last payload without an activity or forced-update substitute");
	task.SetCoordinate3({0, 0, 0});
	task.SetTickCount(8);
	group.Update();
	Require(scene->Shape(references[0]).Values[0] == 11 && scene->Shape(references[1]).Values[0] == 11 && scene->Shape(references[3]).Values[0] == -7, "real visible reselection updates actual shape payload without replacing its exclusive UID node");
	Require(quad.Values[0] == -15 && circle.Values[0] == 3, "copied snapshots survive later actual owner updates");

	const auto old_reference = references[1];
	task.Play("main", sgre::scene::MotionPlayFlags::Force);
	Require(watch.Removed == 5 && watch.Added == 10 && watch.Valid, "UID removal callback must read actual last shape before node owner retirement, even after command selection changes");
	const auto* replacement = task.GenericPlayer()->Scene();
	RequireFailure([&] {
		static_cast<void>(replacement->Shape(old_reference));
	},
	    "root selection");
	Require(quad.Contains({0, 0}), "independent snapshot remains meaningful after selected owner retirement");
}

static void TestActualExclusiveOwner() {
	Fixture fixture;
	sgre::scene::MotionPlayerCommands commands(fixture.Manager);
	commands.SetCharacter("fixture");
	commands.Play("main");
	const auto& timeline = *commands.CachedTimeline();
	const auto prepared = sgre::scene::PrepareMotionTimeline(timeline, *fixture.Asset->Model);
	sgre::scene::MotionRootScene root;
	const sgre::scene::MotionPlayerRenderControls render;
	sgre::scene::MotionSelectedLayerNodes selected(commands, root, render, timeline, prepared, fixture.Runtime->Identifiers(), {}, {});
	Require(selected.ShapeOrdinals().size() == 5 && selected.SpriteCount() == 0 && selected.DrawRowCount() == 0 && selected.KindMask() == 2 && commands.LayerUids().RegisteredCount() == 5, "real type1 selection must allocate exclusive shape/UID owners without drawing slots");
	RequireFailure([&] {
		sgre::scene::MotionLayerShapeOwner duplicate(selected, 0);
	},
	    "exclusive");
	RequireFailure([&] {
		static_cast<void>(selected.Shape(5));
	},
	    "exclusive");
	const auto snapshot = selected.Shape(3).Snapshot();
	selected.Retire();
	Require(commands.LayerUids().RegisteredCount() == 0 && fixture.Runtime->Identifiers().LiveCount() == 0 && snapshot.Type == 3 && snapshot.Values == std::array<float, 8>{}, "actual retirement unregisters and returns IDs while copied constructor snapshots survive");
	RequireFailure([&] {
		static_cast<void>(selected.Shape(3));
	},
	    "exclusive");
}

static void TestNativeContainsBoundaries() {
	sgre::scene::MotionShapeSnapshot rectangle;
	rectangle.Type = 2;
	rectangle.Values = {1, 2, 5, 7, 0, 0, 0, 0};
	Require(rectangle.Contains({1, 2}) && rectangle.Contains({4.5f, 6.5f}) && !rectangle.Contains({5, 3}) && !rectangle.Contains({3, 7}), "rectangle lower bounds are inclusive and upper bounds exclusive");
	sgre::scene::MotionShapeSnapshot reversed;
	reversed.Type = 3;
	reversed.Values = {0, 0, 0, 4, 6, 4, 6, 0};
	Require(reversed.Contains({3, 2}) && reversed.Contains({6, 4}) && !reversed.Contains({-1, 2}), "native quad orientation handles reversed winding without edge normalization");
	RequireFailure([&] {
		static_cast<void>(rectangle.Contains({std::numeric_limits<float>::quiet_NaN(), 0}));
	},
	    "finite");
}

int main() {
	try {
		TestRealSelectedShapeOwner();
		TestNativeContainsBoundaries();
		TestActualExclusiveOwner();
		std::cout << "motion type1 actual shape owner fixtures passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
