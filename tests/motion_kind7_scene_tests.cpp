#include "sgre/scene/motion_assets.hpp"
#include "sgre/scene/motion_player_task.hpp"
#include "sgre/scene/motion_scene_draw_context.hpp"
#include "sgre/scene/motion_scene_evaluation.hpp"
#include "sgre/scene/motion_scene_player.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
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

template <class Operation>
static void RequireFailure(Operation operation, std::string_view message) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(message) != std::string_view::npos, error.what());
		return;
	}
	throw std::runtime_error("kind7 expected rejection containing: " + std::string(message));
}

template <class Value>
static sgre::resource::PsbValue Atom(Value value) {
	sgre::resource::PsbValue result;
	result.Data = std::move(value);
	return result;
}

static sgre::resource::PsbValue Coordinate(double x, double y, double z = 0) {
	return Atom(sgre::resource::PsbArray{Atom(x), Atom(y), Atom(z)});
}

static sgre::resource::PsbValue Layer(std::string label, std::uint32_t kind, sgre::resource::PsbArray children, double x, double y, double zoom, double origin_x = 0, double origin_y = 0, bool hide_at_four = false, bool join_target = false, std::uint32_t mesh = 0, std::uint32_t stencil = 0) {
	sgre::resource::PsbArray frames;
	for (std::size_t index = 0; index != 3; ++index) {
		sgre::resource::PsbObject fields{{"mask", Atom(std::int64_t{0x20663})}, {"coord", Coordinate(x, y)}, {"ox", Atom(origin_x)}, {"oy", Atom(origin_y)}, {"zx", Atom(zoom)}, {"zy", Atom(zoom)}, {"opa", Atom(std::int64_t{255})}, {"color", Atom(std::int64_t{0x808080ff})}, {"bm", Atom(std::int64_t{0x10})}};
		if (kind == 0 || kind == 3) {
			fields.emplace_back("src", Atom(std::string(kind == 3 ? "child" : "atlas")));
			fields.emplace_back("icon", Atom(std::string(kind == 3 ? "main" : "tile")));
		} else {
			// Native512e40's0x1849 mask does not inspect source/icon on kind7, even if unrelated atoms are present.
			fields.emplace_back("src", Atom(std::int64_t{17}));
			fields.emplace_back("icon", Atom(false));
		}
		const auto type = index == 2 || (hide_at_four && index == 1) ? std::int64_t{0} : std::int64_t{2};
		frames.push_back(Atom(sgre::resource::PsbObject{{"time", Atom(static_cast<double>(index * 4))}, {"type", Atom(type)}, {"content", Atom(std::move(fields))}}));
	}
	return Atom(sgre::resource::PsbObject{{"label", Atom(std::move(label))}, {"type", Atom(static_cast<std::int64_t>(kind))}, {"inheritMask", Atom(std::int64_t{4})}, {"coordinate", Atom(std::int64_t{0})}, {"frameList", Atom(std::move(frames))}, {"transformOrder", Atom(sgre::resource::PsbArray{Atom(std::int64_t{0}), Atom(std::int64_t{3}), Atom(std::int64_t{2}), Atom(std::int64_t{1})})}, {"children", Atom(std::move(children))}, {"stencilType", Atom(static_cast<std::int64_t>(stencil))}, {"meshTransform", Atom(static_cast<std::int64_t>(mesh))}, {"objTriPriority", Atom(std::int64_t{0})}, {"parameterize", Atom(std::monostate{})}, {"joinTarget", Atom(join_target)}, {"groundCorrection", Atom(false)}, {"motionIndependentLayerInherit", Atom(false)}});
}

static sgre::resource::PsbValue Timeline(bool child, bool join_target, std::uint32_t mesh, std::uint32_t stencil) {
	auto leaf = Layer(child ? "child-leaf" : "leaf", 0, {}, 0, 0, 1);
	auto inner = Layer(child ? "child-clip" : "inner-clip", 7, {leaf}, child ? 1 : 4, 0, child ? 0.5 : 0.25, child ? 0 : 1, child ? 0 : 2, !child);
	sgre::resource::PsbArray layers;
	sgre::resource::PsbArray order;
	if (child) {
		layers.push_back(std::move(inner));
		order = {Atom(std::int64_t{0}), Atom(std::int64_t{1})};
	} else {
		auto nested = Layer("selected-child", 3, {}, 2, 0, 1);
		layers.push_back(Layer("outer-clip", 7, {inner, nested}, 8, 8, 1, 0, 0, false, join_target, mesh, stencil));
		order = {Atom(std::int64_t{0}), Atom(std::int64_t{1}), Atom(std::int64_t{2}), Atom(std::int64_t{3})};
	}
	const auto priority = Atom(sgre::resource::PsbObject{{"time", Atom(0.0)}, {"type", Atom(std::int64_t{1})}, {"content", Atom(std::move(order))}});
	const auto terminal = Atom(sgre::resource::PsbObject{{"time", Atom(8.0)}, {"type", Atom(std::int64_t{1})}, {"content", Atom(std::monostate{})}});
	const auto layer_map = child ? sgre::resource::PsbObject{{"nested-clip", Atom(std::int64_t{0})}} : sgre::resource::PsbObject{{"root-clip", Atom(std::int64_t{0})}, {"child-layer", Atom(std::int64_t{3})}};
	return Atom(sgre::resource::PsbObject{{"type", Atom(std::int64_t{0})}, {"bounds", Atom(sgre::resource::PsbObject{{"left", Atom(0.0)}, {"top", Atom(0.0)}, {"right", Atom(64.0)}, {"bottom", Atom(64.0)}})}, {"lastTime", Atom(8.0)}, {"loopTime", Atom(-1.0)}, {"layer", Atom(std::move(layers))}, {"layerIndexMap", Atom(layer_map)}, {"priority", Atom(sgre::resource::PsbArray{priority, terminal})}, {"tag", Atom(sgre::resource::PsbArray{})}, {"parameter", Atom(std::monostate{})}, {"variable", Atom(std::monostate{})}, {"parameterize", Atom(std::monostate{})}});
}

static std::shared_ptr<sgre::resource::PsbDocument> Document(bool join_target = false, std::uint32_t mesh = 0, std::uint32_t stencil = 0) {
	sgre::resource::PsbChunkReference chunk;
	auto texture = Atom(sgre::resource::PsbObject{{"type", Atom(std::string("A8"))}, {"width", Atom(std::int64_t{4})}, {"height", Atom(std::int64_t{4})}, {"truncated_width", Atom(std::int64_t{4})}, {"truncated_height", Atom(std::int64_t{4})}, {"pixel", Atom(chunk)}});
	texture.SerializedOffset = 64;
	const auto icon = Atom(sgre::resource::PsbObject{{"left", Atom(0.0)}, {"top", Atom(0.0)}, {"width", Atom(2.0)}, {"height", Atom(2.0)}, {"originX", Atom(0.0)}, {"originY", Atom(0.0)}, {"attr", Atom(std::int64_t{2})}});
	const auto source = Atom(sgre::resource::PsbObject{{"texture", texture}, {"icon", Atom(sgre::resource::PsbObject{{"tile", icon}})}});
	const auto object = [&](bool child) {
		return Atom(sgre::resource::PsbObject{{"type", Atom(std::int64_t{0})}, {"motion", Atom(sgre::resource::PsbObject{{"main", Timeline(child, join_target, mesh, stencil)}})}});
	};
	auto document = std::make_shared<sgre::resource::PsbDocument>();
	document->Header.Version = 4;
	document->Header.RootOffset = 44;
	document->Header.DataOffset = 128;
	document->SerializedSize = 144;
	document->Chunks = {{128, 16}};
	document->Root = Atom(sgre::resource::PsbObject{{"id", Atom(std::string("motion"))}, {"spec", Atom(std::string("win"))}, {"version", Atom(3.03)}, {"label", Atom(std::string("Kind7 actual scene fixture"))}, {"easing", Atom(sgre::resource::PsbArray{})}, {"source", Atom(sgre::resource::PsbObject{{"atlas", source}})}, {"object", Atom(sgre::resource::PsbObject{{"character", object(false)}, {"child", object(true)}})}, {"sourceIconRenameMap", Atom(sgre::resource::PsbObject{})}});
	document->Root.SerializedOffset = 44;
	return document;
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

	explicit Fixture(bool join_target = false, std::uint32_t mesh = 0, std::uint32_t stencil = 0)
	    : Cache([this](auto, const auto&) {
		      return sgre::render::SpriteTextureHandle{static_cast<std::uint32_t>(++Uploads), 1, &Uploads};
	      },
	          [](auto) {
	          }),
	      Catalog(Cache), Manager(Cache), Clock(0), Tasks(8) {
		Asset = Catalog.Prepare(Document(join_target, mesh, stencil), std::make_shared<const std::vector<std::uint8_t>>(144, 255));
		Project = Manager.Register(Asset->Identity, Asset->Model, *Asset->Backing);
		sgre::scene::MotionManagerRuntimeServices services;
		services.SceneAssets = &Catalog;
		services.ReadSourceAliases = [this](auto project) {
			Require(project == Project, "clip source aliases require their real registration");
			return std::span<const sgre::scene::MotionSourceAlias>(Asset->Aliases);
		};
		services.ReadRegisteredAsset = [this](auto project) -> const sgre::scene::LoadedMotionAsset& {
			Require(project == Project, "clip asset requires its real registration");
			return *Asset;
		};
		Runtime = std::make_unique<sgre::scene::MotionManagerRuntime>(Manager, Clock, Seed, std::move(services), 8);
	}

	~Fixture() {
		Runtime->SceneContext().Clear();
		Tasks.Clear();
	}

	sgre::scene::MotionPlayerTask Attach() {
		return sgre::scene::MotionPlayerTask::Attach(*Runtime, Tasks, false, "character", [](const auto&) {
		});
	}
};

static sgre::scene::MotionSceneDrawState State(const sgre::scene::MotionSceneEvaluation& scene, std::string_view label) {
	for (const auto* row : scene.PacketRows()) {
		if (row->Node().Raw->Label == label) {
			return scene.DrawState(*row);
		}
	}
	throw std::runtime_error("actual kind7 fixture drawing row is absent: " + std::string(label));
}

static void RequireRectangle(const sgre::scene::MotionClipRectangle* actual, float left, float top, float right, float bottom) {
	Require(actual != nullptr, "actual composed kind7 rectangle is missing");
	if (!(std::abs(actual->Left - left) < 0.00001f && std::abs(actual->Top - top) < 0.00001f && std::abs(actual->Right - right) < 0.00001f && std::abs(actual->Bottom - bottom) < 0.00001f)) {
		throw std::runtime_error("kind7 rectangle expected=" + std::to_string(left) + "," + std::to_string(top) + "," + std::to_string(right) + "," + std::to_string(bottom) + " actual=" + std::to_string(actual->Left) + "," + std::to_string(actual->Top) + "," + std::to_string(actual->Right) + "," + std::to_string(actual->Bottom));
	}
}

static void TestActualSceneAndChildClipBorrow() {
	Fixture fixture;
	auto task = fixture.Attach();
	task.SetVisible(true);
	task.Play("main");
	task.Progress(0);
	const auto* scene = task.GenericPlayer()->Scene();
	Require(scene && fixture.Runtime->Identifiers().LiveCount() == 6 && scene->PacketRows().size() == 3 && scene->GlobalRows().size() == 2 && fixture.Uploads == 1, "three real clip nodes must own no source texture, geometry or drawing row while the genuine child and two sprite rows remain");
	for (const auto* row : scene->PacketRows()) {
		Require(row->Node().Prepared->Type != 7, "kind7 must never alias a color quad or reserved draw row");
	}
	const auto child_token = scene->FindLayer("child-layer");
	Require(child_token.has_value(), "actual selected child getter map must resolve the containing kind3 owner");
	const auto& child = scene->LayerChild(*child_token);
	const auto* outer = child.InheritedClip();
	RequireRectangle(outer, -8, -8, 24, 24);
	const auto direct = State(*scene, "leaf");
	const auto nested = State(*scene, "child-leaf");
	RequireRectangle(direct.IndividualClip, 7.75f, 3.5f, 15.75f, 11.5f);
	RequireRectangle(nested.IndividualClip, 3, 0, 19, 16);
	Require(direct.IndividualClip != outer && nested.IndividualClip != outer && direct.IndividualClip != nested.IndividualClip, "nested selected clips own distinct retained rectangles while kind3 borrows the exact containing one");
	const auto* direct_identity = direct.IndividualClip;
	const auto* nested_identity = nested.IndividualClip;
	task.SetCoordinate3({3, 0, 2});
	task.Progress(0);
	Require(child.InheritedClip() == outer && State(*scene, "leaf").IndividualClip == direct_identity && State(*scene, "child-leaf").IndividualClip == nested_identity, "real root mutation changes owned values without replacing rectangle identities or child borrows");
	RequireRectangle(outer, -5, -6, 27, 26);
	RequireRectangle(direct_identity, 10.75f, 5.5f, 18.75f, 13.5f);
	RequireRectangle(nested_identity, 6, 2, 22, 18);
	fixture.Tasks.Group(sgre::scene::MotionTaskGroup::Second).Draw(0);
	const std::array<sgre::resource::MotionPoint, 1> sizes{{{4, 4}}};
	sgre::scene::MotionPreparedFrame globals;
	globals.TextureSizes = sizes;
	globals.InitialScissor = {0, 0, 64, 64};
	globals.AppliedRoundedScissor = globals.InitialScissor;
	globals.TargetBounds = globals.InitialScissor;
	globals.Viewport = {64, 64};
	globals.AxisScale = {1, 1};
	const auto frame = fixture.Runtime->SceneContext().Prepare(globals);
	Require(frame.Queue.size() == 2 && frame.Nodes.size() == 3 && frame.Queue[0].IndividualClip && frame.Queue[1].IndividualClip && frame.Queue[0].IndividualClip != frame.Queue[1].IndividualClip, "actual shared frame preparation retains both real packet clip identities");
	RequireFailure([&] {
		task.Progress(0);
	},
	    "queued");
	RequireFailure([&] {
		task.Play("main", sgre::scene::MotionPlayFlags::Force);
	},
	    "queued");
	fixture.Runtime->SceneContext().Clear();
	task.SetTickCount(4);
	task.Progress(0);
	Require(State(*scene, "leaf").IndividualClip == outer, "inactive kind7 inherits its immediate parent's clip rather than updating or removing that parent's rectangle");
	task.SetTickCount(0);
	task.Progress(0);
	Require(State(*scene, "leaf").IndividualClip == direct_identity, "reactivating a real clip reuses its retained owned rectangle");
	task.Play("main", sgre::scene::MotionPlayFlags::Force);
	task.Progress(0);
	Require(fixture.Runtime->Identifiers().LiveCount() == 6 && fixture.Uploads == 1, "Force selection retires old selected clip owners before installing new true owners without duplicate atlas uploads");
	task.Clear();
	Require(fixture.Runtime->Identifiers().LiveCount() == 0, "actual Clear retires parent/child clip storage and selected IDs before task teardown");
}

static void TestUnsupportedClipOwnersAndJoin() {
	for (const auto modifiers : std::array<std::array<std::uint32_t, 2>, 2>{{{1, 0}, {0, 1}}}) {
		Fixture fixture(false, modifiers[0], modifiers[1]);
		auto task = fixture.Attach();
		RequireFailure([&] {
			task.Play("main");
		},
		    "mesh/stencil");
		Require(fixture.Runtime->Identifiers().LiveCount() == 0, "unsupported clip mesh/stencil owners reject before selected allocation/publication");
	}
	Fixture fixture(true);
	auto task = fixture.Attach();
	task.SetVisible(true);
	task.Play("main");
	task.Progress(0);
	const auto serial = task.Commands().SelectionSerial();
	task.Play("main", sgre::scene::MotionPlayFlags::Join);
	Require(task.Commands().SelectionSerial() == serial, "native same-requested-name JOIN withoutForce is an early no-op before snapshot capture");
	RequireFailure([&] {
		task.Play("main", sgre::scene::MotionPlayFlags::Force | sgre::scene::MotionPlayFlags::Join);
	},
	    "snapshot transfer owner");
}

int main() {
	try {
		TestActualSceneAndChildClipBorrow();
		TestUnsupportedClipOwnersAndJoin();
		std::cout << "Actual kind7 scene: exclusive clips, numeric composition, child borrows, finite frame clips and retirement passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
