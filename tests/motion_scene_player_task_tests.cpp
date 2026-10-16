#include "sgre/scene/motion_player_task.hpp"

#include "motion_default_normalization_vectors.hpp"
#include "motion_timeline_variable_native_fixture.hpp"
#include "sgre/scene/motion_particle_player_state.hpp"
#include "sgre/scene/motion_timeline_variables.hpp"

#include "sgre/scene/motion_assets.hpp"
#include "sgre/scene/motion_render_queue.hpp"
#include "sgre/scene/motion_scene_draw_context.hpp"
#include "sgre/scene/motion_scene_evaluation.hpp"
#include "sgre/scene/motion_scene_player.hpp"

#include <algorithm>
#include <array>
#include <bit>
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

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view context) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, error.what());
		return;
	}
	throw std::runtime_error("actual scene owner accepted an unsupported lifetime or service bundle");
}

template <typename Value>
static sgre::resource::PsbValue Atom(Value value) {
	sgre::resource::PsbValue result;
	result.Data = std::move(value);
	return result;
}

static sgre::resource::PsbValue Layer(std::string label, bool child, bool missing_child = false) {
	sgre::resource::PsbArray frames;
	for (std::size_t index = 0; index != 2; ++index) {
		const auto content = Atom(sgre::resource::PsbObject{{"mask", Atom(std::int64_t{0x60603})}, {"src", Atom(std::string(child ? "child" : "atlas"))}, {"icon", Atom(std::string(child ? missing_child ? "absent" : "main" : "tile"))}, {"coord", Atom(sgre::resource::PsbArray{Atom(child ? 2.0 : 0.0), Atom(0.0), Atom(0.0)})}, {"ox", Atom(0.0)}, {"oy", Atom(0.0)}, {"opa", Atom(std::int64_t{255})}, {"color", Atom(std::int64_t{0x808080ff})}, {"bm", Atom(std::int64_t{0x10})}, {"act", Atom(std::string("first"))}});
		frames.push_back(Atom(sgre::resource::PsbObject{{"time", Atom(index == 0 ? 0.0 : 8.0)}, {"type", Atom(index == 0 ? std::int64_t{2} : std::int64_t{0})}, {"content", content}}));
	}
	return Atom(sgre::resource::PsbObject{{"label", Atom(std::move(label))}, {"type", Atom(child ? std::int64_t{3} : std::int64_t{0})}, {"inheritMask", Atom(std::int64_t{4})}, {"coordinate", Atom(std::int64_t{0})}, {"frameList", Atom(std::move(frames))}, {"transformOrder", Atom(sgre::resource::PsbArray{Atom(std::int64_t{0}), Atom(std::int64_t{3}), Atom(std::int64_t{2}), Atom(std::int64_t{1})})}, {"children", Atom(sgre::resource::PsbArray{})}, {"stencilType", Atom(std::int64_t{0})}, {"meshTransform", Atom(std::int64_t{0})}, {"objTriPriority", Atom(std::int64_t{0})}, {"parameterize", Atom(std::monostate{})}, {"joinTarget", Atom(false)}, {"groundCorrection", Atom(false)}, {"motionIndependentLayerInherit", Atom(false)}});
}

static sgre::resource::PsbValue Timeline(bool child, bool retained_child = false, bool sync_child = false, bool timeline_scalar = false, bool missing_child = false) {
	const auto order = child ? sgre::resource::PsbArray{Atom(std::int64_t{0})} : sgre::resource::PsbArray{Atom(std::int64_t{0}), Atom(std::int64_t{1})};
	const auto priority = Atom(sgre::resource::PsbObject{{"time", Atom(0.0)}, {"type", Atom(std::int64_t{1})}, {"content", Atom(order)}});
	const auto terminal = Atom(sgre::resource::PsbObject{{"time", Atom(8.0)}, {"type", Atom(std::int64_t{1})}, {"content", Atom(std::monostate{})}});
	auto layers = sgre::resource::PsbArray{Layer(child ? "nested" : "sprite", false)};
	if (!child) {
		layers.push_back(Layer("selected-child", true, missing_child));
	}
	auto result = Atom(sgre::resource::PsbObject{{"type", Atom(std::int64_t{0})}, {"bounds", Atom(sgre::resource::PsbObject{{"left", Atom(0.0)}, {"top", Atom(0.0)}, {"right", Atom(16.0)}, {"bottom", Atom(8.0)}})}, {"lastTime", Atom(8.0)}, {"loopTime", Atom(-1.0)}, {"layer", Atom(std::move(layers))}, {"priority", Atom(sgre::resource::PsbArray{priority, terminal})}, {"tag", Atom(sgre::resource::PsbArray{})}, {"parameter", Atom(std::monostate{})}, {"variable", Atom(std::monostate{})}, {"parameterize", Atom(std::monostate{})}});
	if (missing_child && !child) {
		// Native FindLayer uses the serialized zero-based layerIndexMap, independently of labels and UIDs.
		std::get<sgre::resource::PsbObject>(result.Data).emplace_back("layerIndexMap", Atom(sgre::resource::PsbObject{{"selected-child", Atom(std::int64_t{1})}}));
	}

	if (retained_child && !child) {
		for (auto& [name, value] : std::get<sgre::resource::PsbObject>(result.Data)) {
			if (name == "parameter") {
				value = Atom(sgre::resource::PsbArray{Atom(sgre::resource::PsbObject{{"id", Atom(std::string("param"))}, {"discretization", Atom(false)}, {"rangeBegin", Atom(0.0)}, {"rangeEnd", Atom(8.0)}, {"division", Atom(8.0)}, {"enabled", Atom(std::int64_t{1})}})});
			}
		}
	}
	if (timeline_scalar && !child) {
		for (auto& [name, value] : std::get<sgre::resource::PsbObject>(result.Data)) {
			if (name == "parameterize") {
				value = Atom(std::int64_t{0});
			}
		}
	}
	if (sync_child && child) {
		for (auto& [name, value] : std::get<sgre::resource::PsbObject>(result.Data)) {
			if (name == "tag") {
				const auto sync = Atom(sgre::resource::PsbObject{{"time", Atom(0.0)}, {"type", Atom(std::int64_t{1})}, {"content", Atom(sgre::resource::PsbObject{{"align", Atom(false)}, {"sync", Atom(true)}, {"action", Atom(std::string{})}})}});
				const auto end = Atom(sgre::resource::PsbObject{{"time", Atom(8.0)}, {"type", Atom(std::int64_t{0})}, {"content", Atom(std::monostate{})}});
				value = Atom(sgre::resource::PsbArray{sync, end});
			}
		}
	}
	return result;
}

static std::shared_ptr<sgre::resource::PsbDocument> Document(bool retained_child = false, bool sync_child = false, bool retained_mesh = false, bool timeline_scalar = false, bool missing_child = false, std::size_t extra_timelines = 0) {
	sgre::resource::PsbChunkReference chunk;
	auto texture = Atom(sgre::resource::PsbObject{{"type", Atom(std::string("A8"))}, {"width", Atom(std::int64_t{4})}, {"height", Atom(std::int64_t{4})}, {"truncated_width", Atom(std::int64_t{4})}, {"truncated_height", Atom(std::int64_t{4})}, {"pixel", Atom(chunk)}});
	texture.SerializedOffset = 64;
	auto icon = Atom(sgre::resource::PsbObject{{"left", Atom(0.0)}, {"top", Atom(0.0)}, {"width", Atom(2.0)}, {"height", Atom(2.0)}, {"originX", Atom(0.0)}, {"originY", Atom(0.0)}, {"attr", Atom(std::int64_t{2})}});
	if (retained_mesh) {
		// The native rectangle branch retains this atom without entering its lazy polygon decoder.
		std::get<sgre::resource::PsbObject>(icon.Data).emplace_back("mesh", Atom(sgre::resource::PsbObject{{"vertices", Atom(chunk)}}));
	}
	const auto source = Atom(sgre::resource::PsbObject{{"texture", texture}, {"icon", Atom(sgre::resource::PsbObject{{"tile", icon}})}});
	const auto object = [retained_child, sync_child, timeline_scalar, missing_child, extra_timelines](bool child) {
		auto motions = sgre::resource::PsbObject{{"main", Timeline(child, retained_child, sync_child, timeline_scalar, missing_child)}};
		if (!child) {
			for (std::size_t index = 0; index != extra_timelines; ++index) {
				motions.emplace_back("variant" + std::to_string(index), Timeline(false, retained_child, sync_child, timeline_scalar, missing_child));
			}
		}
		return Atom(sgre::resource::PsbObject{{"type", Atom(std::int64_t{0})}, {"motion", Atom(std::move(motions))}});
	};
	auto document = std::make_shared<sgre::resource::PsbDocument>();
	document->Header.Version = 4;
	document->Header.RootOffset = 44;
	document->Header.DataOffset = 128;
	document->SerializedSize = 144;
	document->Chunks = {{128, 16}};
	document->Root = Atom(sgre::resource::PsbObject{{"id", Atom(std::string("motion"))}, {"spec", Atom(std::string("win"))}, {"version", Atom(3.03)}, {"label", Atom(std::string("Actual task scene fixture"))}, {"easing", Atom(sgre::resource::PsbArray{})}, {"source", Atom(sgre::resource::PsbObject{{"atlas", source}})}, {"object", Atom(sgre::resource::PsbObject{{"character", object(false)}, {"child", object(true)}})}, {"sourceIconRenameMap", Atom(sgre::resource::PsbObject{})}});
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
	std::vector<std::pair<std::string, std::string>> Events;

	explicit Fixture(bool retained_child = false, bool sync_child = false, bool retained_mesh = false, bool timeline_scalar = false, std::size_t draw_capacity = 32, bool missing_child = false, std::size_t extra_timelines = 0, std::shared_ptr<const sgre::resource::PsbDocument> document = {})
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
		Asset = Catalog.Prepare(document ? std::move(document) : Document(retained_child, sync_child, retained_mesh, timeline_scalar, missing_child, extra_timelines), std::make_shared<const std::vector<std::uint8_t>>(144, 255));
		Project = Manager.Register(Asset->Identity, Asset->Model, *Asset->Backing);
		Runtime = std::make_unique<sgre::scene::MotionManagerRuntime>(Manager, Clock, Seed, Services(), draw_capacity);
	}

	~Fixture() {
		if (Runtime) {
			Runtime->SceneContext().Clear();
		}
		Tasks.Clear();
	}

	sgre::scene::MotionManagerRuntimeServices Services() {
		sgre::scene::MotionManagerRuntimeServices services;
		services.ReadSourceAliases = [this](auto project) {
			Require(project == Project, "scene aliases borrowed a different actual registration");
			return std::span<const sgre::scene::MotionSourceAlias>(Asset->Aliases);
		};
		services.SceneAssets = &Catalog;
		services.ReadRegisteredAsset = [this](auto project) -> const sgre::scene::LoadedMotionAsset& {
			Require(project == Project, "scene asset borrowed a different actual registration");
			return *Asset;
		};
		return services;
	}

	sgre::scene::MotionPlayerTask Attach() {
		return sgre::scene::MotionPlayerTask::Attach(*Runtime, Tasks, false, "character", [this](const auto& event) {
			Require(event.Kind == sgre::scene::MotionPlayerEventKind::Action, "fixture has only authored real layer actions");
			Events.emplace_back(event.Layer, event.Action);
		});
	}
};

static sgre::scene::MotionPreparedFrame Globals(std::span<const sgre::resource::MotionPoint> sizes) {
	sgre::scene::MotionPreparedFrame globals;
	globals.TextureSizes = sizes;
	globals.InitialScissor = {0, 0, 16, 8};
	globals.AppliedRoundedScissor = globals.InitialScissor;
	globals.TargetBounds = globals.InitialScissor;
	globals.Viewport = {16, 8};
	globals.AxisScale = {1, 1};
	return globals;
}

static bool HasGeometry(const sgre::scene::MotionSceneEvaluation& scene, const sgre::scene::MotionClipGeometry* geometry) {
	for (const auto& packet : scene.Packets().Nodes()) {
		if (packet.Command.Geometry == geometry) {
			return true;
		}
	}
	return false;
}

static void TestActualTasksAndSharedOrdering() {
	Fixture fixture;
	const auto asset_references = fixture.Asset.use_count();
	sgre::scene::MotionSharedRandom expected(fixture.Seed);
	const auto first_sample = expected.NextUnitFloat();
	auto first = fixture.Attach();
	const auto second_sample = expected.NextUnitFloat();
	auto second = fixture.Attach();
	Require(first.GenericPlayer() && second.GenericPlayer() && first.Random().State() == sgre::scene::MotionPlayerRandom(first_sample).State() && second.Random().State() == sgre::scene::MotionPlayerRandom(second_sample).State(), "actual task construction must share the real consecutive seed samples and generic service pair");
	RequireFailure([&] {
		static_cast<void>(first.Player());
	},
	    "legacy");
	first.SetVisible(true);
	second.SetVisible(true);
	first.SetPriority(7);
	second.SetPriority(-3);
	first.Play("main");
	second.Play("main");
	const auto first_serial = first.Commands().SelectionSerial();
	auto& second_group = fixture.Tasks.Group(sgre::scene::MotionTaskGroup::Second);
	second_group.Update();
	const auto* first_scene = first.GenericPlayer()->Scene();
	const auto* second_scene = second.GenericPlayer()->Scene();
	Require(first_scene && second_scene && first_scene->PacketRows().size() == 3 && second_scene->PacketRows().size() == 3 && fixture.Runtime->Identifiers().LiveCount() == 6, "real root Update must create both actual nonempty kind3 children and their shared UID owners");
	Require(fixture.Clock.VariableStep() == 1 && fixture.Runtime->ReadFrameDelta() == 1 && fixture.Events.size() == 6 && std::count(fixture.Events.begin(), fixture.Events.end(), std::pair<std::string, std::string>{"nested", "first"}) == 2, "task update must consume the cached clock and deliver real nested actions once");
	Require(fixture.Asset.use_count() == asset_references && fixture.Manager.ReferenceCount(fixture.Project) == 1, "scene evaluation must borrow actual catalog/assets without another registration or retained loaded owner");
	second_group.Draw(0x12345678);
	Require(fixture.Runtime->Context().Size() == 0 && fixture.Runtime->SceneContext().Size() == 2, "actual task Draw must publish nested scenes through its source-manager queue");
	RequireFailure([&] {
		first.Stop();
	},
	    "queued");
	RequireFailure([&] {
		fixture.Runtime->RequireCanRetirePlayers();
	},
	    "consumed");
	RequireFailure([&] {
		second_group.Draw(0);
	},
	    "queued");
	const std::array<sgre::resource::MotionPoint, 1> sizes{{{4, 4}}};
	const auto frame = fixture.Runtime->SceneContext().Prepare(Globals(sizes));
	Require(frame.Queue.size() == 4 && frame.Nodes.size() == 6 && frame.ClipQueue.size() == frame.Queue.size(), "manager frame must retain every real quad and concatenate exact packet-indexed graphs");
	Require(HasGeometry(*second_scene, frame.Queue.front().Clip->Geometry) && HasGeometry(*first_scene, frame.Queue.back().Clip->Geometry), "one actual manager sort must place the second task's lower-priority rows before the first despite append order");
	for (const auto& command : frame.Queue) {
		Require(command.Clip && command.Clip->Geometry && command.Clip->Geometry->Positions.size() == 4 && command.Texture == 0 && command.BlendMode == 0x10 && command.Key == 2, "prepared queue must retain real source slots, material flags and generated geometry");
	}
	RequireFailure([&] {
		static_cast<void>(fixture.Runtime->SceneContext().Prepare(Globals(sizes)));
	},
	    "unconsumed");
	sgre::scene::MotionManagerRuntime other(fixture.Manager, fixture.Clock, fixture.Seed, fixture.Services(), 32);
	RequireFailure([&] {
		other.SceneContext().Append(*first_scene);
	},
	    "distinct actual");
	other.Close();
	fixture.Runtime->SceneContext().Clear();
	first.Play("main", sgre::scene::MotionPlayFlags::Force);
	Require(first.Commands().SelectionSerial() != first_serial, "real Force must retire and replace the selected scene after its queue is consumed");
	second_group.Update();
	second_group.Draw(0);
	Require(fixture.Runtime->SceneContext().Size() == 2 && fixture.Runtime->Identifiers().LiveCount() == 6, "stable immutable child preparations must survive replacement without lost or duplicate actual UID owners");
	fixture.Runtime->SceneContext().Clear();
	fixture.Tasks.Clear();
	Require(!first.IsLive() && !second.IsLive() && fixture.Runtime->Identifiers().LiveCount() == 0 && fixture.Runtime->PlayerCount() == 0, "actual task retirement must destroy evaluators before selected IDs and resource borrows");
	fixture.Runtime->Close();
	fixture.Runtime.reset();
	Require(fixture.Manager.Unregister(fixture.Project) && fixture.Cache.Count() == 0 && fixture.Uploads == 1 && fixture.Releases == 1, "actual registration/texture retirement must complete after every task and publication");
}

static void TestSceneOccurrenceCapacityIsIndependentOfPacketRows() {
	Fixture fixture(false, false, false, false, 1);
	auto first = fixture.Attach();
	first.SetVisible(true);
	first.Play("main");
	first.Progress(0);
	const auto* scene = first.GenericPlayer()->Scene();
	Require(scene && scene->PacketRows().size() == 3 && scene->GlobalRows().size() == 2, "one genuine task includes its containing sprite, kind3 row and selected child sprite");
	auto& group = fixture.Tasks.Group(sgre::scene::MotionTaskGroup::Second);
	group.Draw(0);
	Require(fixture.Runtime->SceneContext().Size() == 1, "one actual task Draw consumes one scene occurrence regardless of its selected rows");
	const std::array<sgre::resource::MotionPoint, 1> sizes{{{4, 4}}};
	const auto frame = fixture.Runtime->SceneContext().Prepare(Globals(sizes));
	Require(frame.Nodes.size() == 3 && frame.Queue.size() == 2 && frame.MaskSettings.size() == 3, "scene occurrence capacity1 must retain all three genuine packet owners and both drawable rows");
	for (const auto& command : frame.Queue) {
		Require(command.Clip && HasGeometry(*scene, command.Clip->Geometry), "separate row capacity must preserve the exact selected geometry owners");
	}
	RequireFailure([&] {
		first.Progress(1);
	},
	    "queued");
	fixture.Runtime->SceneContext().Clear();

	auto second = fixture.Attach();
	second.SetVisible(true);
	second.Play("main");
	group.Update();
	RequireFailure([&] {
		group.Draw(0);
	},
	    "distinct actual task Draw");
	Require(fixture.Runtime->SceneContext().Size() == 1, "separate packet capacity must not silently expand the actual task occurrence limit");
	fixture.Runtime->SceneContext().Clear();
	fixture.Tasks.Clear();
}

static void TestClearRetiresSelectedTreeWithoutResettingPlayer() {
	Fixture fixture(true);
	auto task = fixture.Attach();
	task.SetVisible(true);
	task.SetCoordinate3({7, 11, 13});
	task.Play("main");
	task.Progress(2);
	task.SetVariable("retained", 3);
	task.SetVariable("selected-child::retained", 5);
	const auto timeline = task.Commands().CachedTimeline();
	const auto names = task.Commands().Names();
	const auto clock = task.Commands().ClockState();
	const auto serial = task.Commands().SelectionSerial();
	const auto random = task.Random().State();
	const auto* parameter_owner = task.Commands().ParameterValues().CurrentTimelineParameters(*timeline);
	const auto* parameter_cell = parameter_owner->Entries().data();
	const auto event_count = fixture.Events.size();
	Require(fixture.Runtime->Identifiers().LiveCount() == 3 && task.Commands().ParameterValues().RegisteredParameterCount() == 1, "clear fixture requires actual root/child IDs and a live selected parameter owner");
	auto& second = fixture.Tasks.Group(sgre::scene::MotionTaskGroup::Second);
	second.Draw(0);
	RequireFailure([&] {
		task.Clear();
	},
	    "queued");
	Require(task.GenericPlayer()->Scene() && fixture.Runtime->Identifiers().LiveCount() == 3 && task.Commands().SelectionSerial() == serial, "queued clear must reject before selected-tree mutation");
	fixture.Runtime->SceneContext().Clear();
	task.Clear();
	task.Clear();
	Require(task.IsLive() && fixture.Runtime->PlayerCount() == 1 && !task.GenericPlayer()->Scene() && fixture.Runtime->Identifiers().LiveCount() == 0 && task.Commands().ParameterValues().RegisteredParameterCount() == 1 && task.Commands().ParameterValues().CurrentTimelineParameters(*timeline) == parameter_owner && parameter_owner->Entries().data() == parameter_cell, "clear must retire every actual selected root/child node while retaining its concrete task/runtime and actual containing parameter cells");
	Require(task.Commands().CachedTimeline() == timeline && task.Commands().SelectionSerial() == serial && task.Commands().Names().CurrentMotion == names.CurrentMotion && task.Commands().Names().RequestedMotion == names.RequestedMotion && task.Commands().Names().CurrentCharacter == names.CurrentCharacter, "clear must retain actual selected timeline, command names and selection serial");
	const auto& cleared = task.Commands().ClockState();
	Require(cleared.TickCount == clock.TickCount && cleared.EvaluationTime == clock.EvaluationTime && cleared.TickAssigned == clock.TickAssigned && cleared.Playing == clock.Playing && cleared.LastTime == clock.LastTime && cleared.SyncWaiting == clock.SyncWaiting && task.Random().State() == random && fixture.Events.size() == event_count, "clear must neither stop/seek the real player nor consume RNG or dispatch new events");
	Require(task.GetVariable("retained") == 3 && task.GetVariable("selected-child/retained") == 5 && task.GetVariableKeys() == std::vector<std::string>{"param"} && task.Root().Overrides().Coordinate == std::array<float, 3>{7, 11, 13}, "clear empties descriptor targets, preserving containing registered keys, both retained maps and root overrides");
	sgre::scene::MotionSharedRandom expected(fixture.Seed);
	static_cast<void>(expected.NextUnitFloat());
	static_cast<void>(expected.NextUnitFloat());
	Require(fixture.Runtime->NextUnitFloat() == expected.NextUnitFloat(), "idempotent clear must not sample the actual shared manager random owner");
	task.SetVariable("selected-child/retained", 6);
	Require(task.GetVariable("selected-child::retained") == 6, "a cleared controller stores its value with no dangling selected targets");
	second.Draw(0);
	Require(fixture.Runtime->SceneContext().Size() == 0, "a cleared native player draws an empty selected tree immediately");
	task.Progress(1);
	Require(task.Commands().ClockState().TickCount == clock.TickCount + 1 && task.Root().Frame().Composed && fixture.Events.size() == event_count, "root-only progression must use retained clock with its actual retained parameter records and no fabricated layer actions");
	task.Play("main");
	Require(!task.GenericPlayer()->Scene() && fixture.Runtime->Identifiers().LiveCount() == 0 && task.Commands().SelectionSerial() == serial, "unchanged-name Play after clear remains the native no-op");
	task.Play("main", sgre::scene::MotionPlayFlags::Force);
	task.Progress(0);
	Require(task.GenericPlayer()->Scene() && fixture.Runtime->Identifiers().LiveCount() == 3 && task.Commands().SelectionSerial() != serial && task.Commands().ParameterValues().RegisteredParameterCount() == 1 && task.GetVariable("retained") == 3 && task.GetVariable("selected-child::retained") == 6, "genuine Force must rebuild a fresh selected tree and rebind surviving descriptor values");
	second.Draw(0);
	Require(fixture.Runtime->SceneContext().Size() == 1, "actual reinstallation must publish the new live scene");
	fixture.Runtime->SceneContext().Clear();
	task.Clear();
	fixture.Tasks.Clear();
	Require(!task.IsLive() && fixture.Runtime->Identifiers().LiveCount() == 0, "cleared player retirement must release its containing owner exactly once");
}

static void TestClearRetainsWholeTimelineScalar() {
	Fixture fixture(true, false, false, true);
	auto task = fixture.Attach();
	task.Play("main");
	task.Progress(0);
	const auto* timeline = task.Commands().CachedTimeline();
	const auto* parameter = task.Commands().ParameterValues().TimelineParameter(*timeline);
	Require(parameter != nullptr && fixture.Runtime->Identifiers().LiveCount() == 3, "clear scalar fixture requires a real bound generic timeline record and selected child");
	task.Clear();
	task.SetVariable("param", 8, 7);
	task.Progress(100);
	Require(task.Commands().ClockState().TickCount == 8 && task.Commands().ClockState().EvaluationTime == 8 && task.Commands().ParameterValues().TimelineParameter(*timeline) == parameter && parameter->ControlFlags == 0 && parameter->Position == 8 && fixture.Runtime->Identifiers().LiveCount() == 0, "cleared player must retain its exact whole-timeline scalar, follow that scalar instead of supplied delta, and acknowledge real flags after root composition");
	task.SetVariable("param", 0);
	task.Progress(100);
	Require(task.Commands().ClockState().TickCount == 0 && task.Commands().ClockState().EvaluationTime == 0 && !task.GenericPlayer()->Scene(), "cleared scalar must retain native reverse parameter traversal without resurrecting selected layers");
}

static void TestRetainedIconMeshUsesNativeRectangle() {
	for (const bool retained_mesh : {false, true}) {
		Fixture fixture(false, false, retained_mesh);
		auto task = fixture.Attach();
		task.SetVisible(true);
		task.Play("main");
		auto& group = fixture.Tasks.Group(sgre::scene::MotionTaskGroup::Second);
		group.Update();
		group.Draw(0);
		const std::array<sgre::resource::MotionPoint, 1> sizes{{{4, 4}}};
		const auto frame = fixture.Runtime->SceneContext().Prepare(Globals(sizes));
		Require(frame.Queue.size() == 2, "retained icon mesh must keep both real ordinary root/child quads");
		for (std::size_t index = 0; index != frame.Queue.size(); ++index) {
			const auto* geometry = frame.Queue[index].Clip->Geometry;
			Require(geometry != nullptr && geometry->Positions.size() == 4, "retained mesh atom must not substitute a polygon for the native rectangle branch");
			const float left = index == 0 ? 2.0f : 0.0f;
			const std::array<sgre::resource::MotionPoint, 4> expected{{{left, 0}, {left + 2, 0}, {left, 2}, {left + 2, 2}}};
			for (std::size_t point = 0; point != expected.size(); ++point) {
				Require(geometry->Positions[point].X == expected[point].X && geometry->Positions[point].Y == expected[point].Y, "ordinary retained-mesh rectangle differs from native source dimensions and child translation");
			}
		}
		fixture.Runtime->SceneContext().Clear();
	}
}

static void TestActualMissingChildTimelineKeepsItsRealRoot() {
	for (const bool parameter_record : {false, true}) {
		// Actual corpus references present child objects with absent motions; this is a genuine failed registered lookup.
		Fixture fixture(parameter_record, false, false, false, 32, true);
		Require(fixture.Manager.ResolveTimeline("child", "main").has_value() && !fixture.Manager.ResolveTimeline("child", "absent"), "fixture must distinguish an actual registered child object from its absent requested timeline");
		auto task = fixture.Attach();
		task.SetVisible(true);
		task.Play("main", sgre::scene::MotionPlayFlags::Force);
		task.SetCoordinate3({5, 7, 11});
		task.Progress(0);
		const auto* scene = task.GenericPlayer()->Scene();
		Require(scene != nullptr, "actual parent selection is required before the missing child target");
		const auto layer = scene->FindLayer("selected-child");
		Require(layer.has_value(), "actual kind3 layer must remain selected despite its missing child motion");
		const auto child = scene->LayerChild(*layer).Borrow();
		const std::array<float, 3> deltas{0, 1, 7};
		std::size_t action_count = 0;
		for (std::size_t phase = 0; phase < deltas.size(); ++phase) {
			task.SetLeft(5.0f + static_cast<float>(phase));
			if (phase != 0) {
				task.Progress(deltas[phase]);
			}
			const auto& commands = task.ChildCommands(child);
			const auto& names = commands.Names();
			const auto& root = task.ChildRoot(child).Frame();
			Require(!commands.SelectedProject() && commands.CachedTimeline() == nullptr && commands.SelectionSerial() == 0 && names.CurrentCharacter == "child" && names.RequestedCharacter == "child" && names.CurrentMotion.empty() && names.RequestedMotion.empty() && names.DelayedMotion.empty(), "missing child must preserve the actual command failure rather than install an empty substitute motion");
			Require(root.Composed && root.Hierarchy.Attributes.Coordinate == std::array<float, 3>{7.0f + static_cast<float>(phase), 7, 11} && commands.ClockState().DeltaTicks == deltas[phase] && !commands.ClockState().InitialCompositionPending && !commands.ClockState().TickAssigned && !commands.ClockState().Playing, "native524910 must compose the actual transferred root and acknowledge its clock even without a first selected timeline");
			Require(scene->PacketRows().size() == 2 && scene->GlobalRows().size() == 1 && fixture.Runtime->Identifiers().LiveCount() == 2, "missing child may not manufacture descendant IDs, geometry or prepared rows");
			if (phase == 0) {
				action_count = fixture.Events.size();
				Require(action_count == 2 && std::none_of(fixture.Events.begin(), fixture.Events.end(), [](const auto& event) {
					return event.first == "nested";
				}),
				    "only real containing-layer actions may survive the empty child event prepend");
			} else {
				Require(fixture.Events.size() == action_count, "retained failed child command may not repeat first-frame actions");
			}
			fixture.Tasks.Group(sgre::scene::MotionTaskGroup::Second).Draw(0);
			const std::array<sgre::resource::MotionPoint, 1> sizes{{{4, 4}}};
			const auto frame = fixture.Runtime->SceneContext().Prepare(Globals(sizes));
			Require(frame.Queue.size() == 1 && frame.Nodes.size() == 2, "actual parent sprite must publish normally while the unresolved child contributes no rows");
			fixture.Runtime->SceneContext().Clear();
		}
		task.Play("main", sgre::scene::MotionPlayFlags::Force);
		Require(!child.IsLive(), "real Force retirement must expire the exact failed child owner before replacement");
		task.Progress(0);
		Require(fixture.Runtime->Identifiers().LiveCount() == 2, "replacement must retry the genuine missing target without preserving the previous child receipt");
		fixture.Tasks.Clear();
		Require(!task.IsLive() && fixture.Runtime->Identifiers().LiveCount() == 0, "unselected child update callbacks must retire before their actual selected/command/root owners");
	}
}

static void TestPreparationLeasesBoundLiveOwners() {
	Fixture fixture(false, false, false, false, 32, false, 300);
	const auto main = fixture.Manager.ResolveTimeline("character", "main");
	Require(main.has_value(), "lease fixture requires its actual registered timeline");
	auto first = fixture.Runtime->PrepareScene(*main);
	auto same = fixture.Runtime->PrepareScene(*main);
	Require(first.Owner.Owns(fixture.Manager, *main->Timeline, &first.Timeline) && first.Owner.SameOwner(same.Owner) && &first.Timeline == &same.Timeline && fixture.Runtime->RegistrationOrderMatches(first.Sources), "repeated live preparation must share its exact immutable owner and source order");
	Require(!fixture.Runtime->CanClose(), "an externally held preparation lease must prevent its actual runtime retirement");
	RequireFailure([&] {
		fixture.Runtime->Close();
	},
	    "retire first");
	std::vector<sgre::scene::MotionPreparationLease> held;
	held.reserve(255);
	for (std::size_t index = 0; index != 255; ++index) {
		const auto selected = fixture.Manager.ResolveTimeline("character", "variant" + std::to_string(index));
		Require(selected.has_value(), "every bounded live-owner input must be genuinely registered");
		held.push_back(fixture.Runtime->PrepareScene(*selected).Owner);
	}
	const auto next = fixture.Manager.ResolveTimeline("character", "variant255");
	Require(next.has_value(), "next bounded preparation must be genuinely registered");
	RequireFailure([&] {
		static_cast<void>(fixture.Runtime->PrepareScene(*next));
	},
	    "simultaneously live");
	held.pop_back();
	auto replacement = fixture.Runtime->PrepareScene(*next);
	Require(first.Owner.Owns(fixture.Manager, *main->Timeline, &first.Timeline) && first.Timeline.Layers.size() == 2, "reclaiming an expired entry must preserve every other actual live preparation");
	held.clear();
	first.Owner = {};
	same.Owner = {};
	replacement.Owner = {};
	Require(fixture.Runtime->CanClose(), "releasing the actual leases must remove their registration borrows without runtime history");
}

static void TestCurrentPreparationSurvivesSelectionHistory() {
	Fixture fixture(false, false, false, false, 32, false, 300);
	auto task = fixture.Attach();
	task.SetVisible(true);
	for (std::size_t index = 0; index != 300; ++index) {
		task.Play("variant" + std::to_string(index), sgre::scene::MotionPlayFlags::Force);
		task.Progress(0);
		const auto* scene = task.GenericPlayer()->Scene();
		Require(scene && scene->PacketRows().size() == 3 && fixture.Runtime->Identifiers().LiveCount() == 3, "real same-task replacement must retain only its genuine current parent and selected child owners");
		fixture.Tasks.Group(sgre::scene::MotionTaskGroup::Second).Draw(0);
		const std::array<sgre::resource::MotionPoint, 1> sizes{{{4, 4}}};
		const auto frame = fixture.Runtime->SceneContext().Prepare(Globals(sizes));
		Require(frame.Nodes.size() == 3 && frame.Queue.size() == 2, "every real replacement must publish the actual child and containing geometry");
		fixture.Runtime->SceneContext().Clear();
	}
	Require(fixture.Events.size() == 900, "current-only preparation must preserve all actual layer actions beyond the old historical cache limit");
	fixture.Tasks.Clear();
	Require(fixture.Runtime->CanClose() && fixture.Runtime->Identifiers().LiveCount() == 0, "real task retirement must release every selected and current-tag preparation lease");
}

static sgre::scene::MotionPlayerUpdateServices EventOwnerServices(std::size_t& delivered) {
	sgre::scene::MotionPlayerUpdateServices services;
	services.ReadFrameDelta = [] {
		return 1.0f;
	};
	services.PrepareFrames = [](auto, const auto&) {
		throw std::runtime_error("event-lease fixture does not replace actual numeric traversal");
	};
	services.ComposeLayers = [](const auto&) {
		throw std::runtime_error("event-lease fixture does not replace actual composition");
	};
	services.PublishDrawPackets = [](const auto&) {
		throw std::runtime_error("event-lease fixture does not manufacture frame publication");
	};
	services.Draw = [] {
		throw std::runtime_error("event-lease fixture does not manufacture drawing");
	};
	services.DeliverEvent = [&](const auto& event) {
		Require(event.Layer == "sprite" && event.Action == "first", "detached child queue must retain exact original raw layer/action strings");
		++delivered;
	};
	return services;
}

static void TestDetachedEventPreparationLease() {
	Fixture fixture(false, false, false, false, 32, false, 300);
	std::size_t delivered = 0;
	{
		sgre::scene::MotionPlayerCommands parent(fixture.Manager);
		parent.SetCharacter("character");
		parent.Play("variant0");
		sgre::scene::MotionPlayerUpdate parent_update(parent, EventOwnerServices(delivered));
		{
			auto current = fixture.Runtime->PrepareScene(*parent.SelectedProject());
			const auto foreign = fixture.Manager.ResolveTimeline("character", "main");
			RequireFailure([&] {
				parent_update.PrepareCurrentTimeline(*foreign->Timeline, current.Owner);
			},
			    "exact manager/timeline lease");
			parent_update.PrepareCurrentTimeline(*parent.CachedTimeline(), current.Owner);
		}
		{
			sgre::scene::MotionPlayerCommands child(fixture.Manager, parent);
			child.SetCharacter("character");
			child.Play("main");
			sgre::scene::MotionPlayerUpdate child_update(child, EventOwnerServices(delivered));
			auto current = fixture.Runtime->PrepareScene(*child.SelectedProject());
			child_update.PrepareCurrentTimeline(*child.CachedTimeline(), current.Owner);
			const auto& raw = child.CachedTimeline()->Layers.front();
			const auto* action = std::get_if<std::string>(&raw.Frames.front().Content.Find("act")->Data);
			Require(action != nullptr, "event lease fixture requires its actual raw PSB action string");
			child_update.EnqueueAction(raw.Label, *action);
			parent_update.PrependChildEvents(child_update);
			Require(child_update.PendingEventCount() == 0 && parent_update.PendingEventCount() == 1, "actual prepend must transfer the event and its preparation borrow together");
		}
		// Only the parent's pending raw-string queue now keeps the child's old preparation alive.
		std::vector<sgre::scene::MotionPreparationLease> held;
		held.reserve(254);
		for (std::size_t index = 1; index != 255; ++index) {
			const auto selected = fixture.Manager.ResolveTimeline("character", "variant" + std::to_string(index));
			held.push_back(fixture.Runtime->PrepareScene(*selected).Owner);
		}
		const auto next = fixture.Manager.ResolveTimeline("character", "variant255");
		RequireFailure([&] {
			static_cast<void>(fixture.Runtime->PrepareScene(*next));
		},
		    "simultaneously live");
		parent_update.PostUpdate();
		Require(delivered == 1 && parent_update.PendingEventCount() == 0, "real detached delivery must drain its original event before releasing that raw-string owner");
		auto released_slot = fixture.Runtime->PrepareScene(*next);
		Require(released_slot.Owner.Owns(fixture.Manager, *next->Timeline, &released_slot.Timeline), "successful delivery must release exactly the old queued preparation and permit its bounded slot reuse");
	}
	Require(fixture.Runtime->CanClose(), "current-tag and detached-event leases must all retire before the enclosing runtime");
}

static void TestServicePairAndExactCatalog() {
	Fixture fixture;
	auto incomplete = fixture.Services();
	incomplete.ReadRegisteredAsset = {};
	RequireFailure([&] {
		sgre::scene::MotionManagerRuntime invalid(fixture.Manager, fixture.Clock, fixture.Seed, incomplete);
	},
	    "registered source-alias");
	auto copied_asset = *fixture.Asset;
	auto foreign = fixture.Services();
	foreign.ReadRegisteredAsset = [&](auto) -> const sgre::scene::LoadedMotionAsset& {
		return copied_asset;
	};
	sgre::scene::MotionManagerRuntime runtime(fixture.Manager, fixture.Clock, fixture.Seed, foreign, 32);
	RequireFailure([&] {
		static_cast<void>(runtime.ReadRegisteredAsset(fixture.Project));
	},
	    "actual manager/catalog registration");
	runtime.Close();
}

static void TestRetainedChildRecomposesCurrentParent(bool sync_waiting) {
	Fixture fixture(true, sync_waiting);
	sgre::scene::MotionPlayerCommands commands(fixture.Manager);
	commands.SetCharacter("character");
	commands.Play("main");
	const auto& timeline = *commands.CachedTimeline();
	const auto prepared = sgre::scene::PrepareMotionTimeline(timeline, *fixture.Asset->Model, sgre::scene::MotionAttributeBlendPolicy::HalveRgbForClearBlendNibble);
	sgre::scene::MotionRootScene root;
	root.SetVisible(true);
	const sgre::scene::MotionPlayerRenderControls render;
	sgre::scene::MotionSceneEvaluation* evaluator = nullptr;
	float elapsed = 0;
	std::size_t events = 0;
	sgre::scene::MotionPlayerUpdateServices update_services;
	update_services.ReadFrameDelta = [&] {
		return elapsed;
	};
	update_services.PrepareFrames = [&](auto traversal, const auto& frame) {
		evaluator->PrepareFrames(traversal, frame);
	};
	update_services.ComposeLayers = [&](const auto& frame) {
		evaluator->Compose(frame);
	};
	update_services.PublishDrawPackets = [&](const auto& frame) {
		evaluator->Publish(frame);
	};
	update_services.Draw = [&] {
		evaluator->Draw();
	};
	update_services.DeliverEvent = [&](const auto&) {
		++events;
	};
	sgre::scene::MotionPlayerUpdate update(commands, std::move(update_services));
	sgre::scene::MotionChildConstructionServices construction;
	construction.ReadManagerRandom = [&](const auto&) {
		return fixture.Runtime->NextUnitFloat();
	};
	sgre::scene::MotionChildFrameSlotServices slots;
	slots.EnqueueAction = [&](auto layer, auto action) {
		update.EnqueueAction(layer, action);
	};
	std::vector<std::unique_ptr<sgre::scene::PreparedMotionTimeline>> child_preparations;
	sgre::scene::MotionSelectedLayerNodes selected(commands, root, render, timeline, prepared, fixture.Runtime->Identifiers(), construction, slots);
	{
		sgre::scene::MotionSceneEvaluationServices services;
		services.ReadRegisteredAsset = [&](auto project) -> const sgre::scene::LoadedMotionAsset& {
			return fixture.Runtime->ReadRegisteredAsset(project);
		};
		services.PrepareChildTimeline = [&](const auto& selection) -> const sgre::scene::PreparedMotionTimeline& {
			child_preparations.push_back(std::make_unique<sgre::scene::PreparedMotionTimeline>(sgre::scene::PrepareMotionTimeline(*selection.Timeline, *fixture.Asset->Model, sgre::scene::MotionAttributeBlendPolicy::HalveRgbForClearBlendNibble)));
			return *child_preparations.back();
		};
		services.RequireUnqueued = [&] {
			Require(fixture.Runtime->SceneContext().CanClear() && fixture.Runtime->SceneContext().Size() == 0, "retained child fixture has an outstanding actual scene publication");
		};
		services.AppendDraw = [](const auto&, auto) {
			throw std::runtime_error("retained child numeric fixture does not submit a replacement draw");
		};
		services.DeliverEvent = [&](const auto&) {
			++events;
		};
		sgre::scene::MotionSceneEvaluation scene(selected, commands, root, render, fixture.Catalog, *fixture.Runtime, update, std::move(services));
		evaluator = &scene;
		scene.Update();
		auto& child = selected.Child(1).Commands();
		const auto* child_timeline = child.CachedTimeline();
		Require(child_timeline != nullptr, "retained fixture did not select its actual child timeline");
		const auto* child_parameters = child.ParameterValues().CurrentTimelineParameters(*child_timeline);
		const std::size_t initial_events = sync_waiting ? 4 : 3;
		Require(child_parameters && child_parameters->Entries().empty() && commands.ParameterValues().RegisteredParameterCount() == 1 && events == initial_events, "retained fixture must create a real zero-own-record child under the parameterized parent");
		if (sync_waiting) {
			Require(child.ClockState().SyncWaiting && child.ClockState().Playing, "actual child Sync tag did not establish its native clock gate");
		} else {
			child.Stop();
		}
		const auto child_clock = child.ClockState();
		const auto child_generation = child.SelectionSerial();
		const auto graph_generation = scene.GraphGeneration();
		const auto* actual_child = selected.ChildNodes(1);
		const auto child_geometry = [&]() -> const sgre::scene::MotionClipGeometry& {
			for (const auto* row : scene.PacketRows()) {
				if (row->Node().Raw->Label == "nested") {
					const auto* geometry = scene.Packets().Nodes()[scene.Packets().Index(*row)].Command.Geometry;
					Require(geometry && geometry->Positions.size() == 4, "actual stopped child geometry is absent");
					return *geometry;
				}
			}
			throw std::runtime_error("actual stopped child row is absent");
		};
		const auto initial_position = child_geometry().Positions.front();
		elapsed = 1;
		for (const auto x : std::array<float, 2>{7, 9}) {
			commands.ParameterValues().WriteLocal("param", x - 1, 0);
			root.SetCoordinate3({x, 0, 0});
			scene.Update();
			Require(child.ClockState().Playing == child_clock.Playing && child.ClockState().SyncWaiting == child_clock.SyncWaiting && child.ClockState().TickCount == child_clock.TickCount && child.ClockState().EvaluationTime == child_clock.EvaluationTime && child.SelectionSerial() == child_generation && selected.ChildNodes(1) == actual_child && scene.GraphGeneration() == graph_generation && events == initial_events, "retained child composition must not fabricate cursor traversal, reselect its tree or repeat frame/Sync events");
			const auto position = child_geometry().Positions.front();
			Require(position.X == initial_position.X + x && position.Y == initial_position.Y && scene.GlobalRows().size() == 2 && child_preparations.size() == 1, "stopped child must recompose and publish real geometry from the current containing pose");
		}
	}
	selected.Retire();
	Require(fixture.Runtime->Identifiers().LiveCount() == 0 && commands.ParameterValues().RegisteredParameterCount() == 0, "retained child composition must retire all actual IDs and parameter registrations");
}

static sgre::resource::PsbValue& MutableProperty(sgre::resource::PsbValue& value, std::string_view key) {
	for (auto& [name, property] : std::get<sgre::resource::PsbObject>(value.Data)) {
		if (name == key) {
			return property;
		}
	}
	throw std::runtime_error("variable fixture's actual serialized property is absent");
}

static std::shared_ptr<sgre::resource::PsbDocument> VariableDocument(std::uint32_t interval, bool bound_layer, bool hidden_middle = false, std::string scope = {}) {
	auto document = Document();
	auto& character = MutableProperty(MutableProperty(document->Root, "object"), "character");
	auto& timeline = MutableProperty(MutableProperty(character, "motion"), "main");
	MutableProperty(timeline, "lastTime") = Atom(20.0);
	sgre::resource::PsbArray priority;
	priority.push_back(Atom(sgre::resource::PsbObject{{"time", Atom(0.0)}, {"type", Atom(std::int64_t{1})}, {"content", Atom(bound_layer ? sgre::resource::PsbArray{Atom(std::int64_t{0})} : sgre::resource::PsbArray{})}}));
	priority.push_back(Atom(sgre::resource::PsbObject{{"time", Atom(20.0)}, {"type", Atom(std::int64_t{1})}, {"content", Atom(std::monostate{})}}));
	MutableProperty(timeline, "priority") = Atom(std::move(priority));
	MutableProperty(timeline, "parameter") = Atom(sgre::resource::PsbArray{Atom(sgre::resource::PsbObject{{"id", Atom(std::string("drive"))}, {"discretization", Atom(false)}, {"rangeBegin", Atom(0.0)}, {"rangeEnd", Atom(8.0)}, {"division", Atom(8.0)}, {"enabled", Atom(std::int64_t{1})}})});
	sgre::resource::PsbArray frames;
	const std::array<double, 4> times{0, 5, 10, 11};
	const std::array<double, 3> values{-0.5, 1.5, 0.25};
	for (std::size_t index = 0; index != times.size(); ++index) {
		const std::int64_t type = index == 3 || (index == 1 && hidden_middle) ? 0 : index == 2 ? 2
		                                                                                       : 3;
		auto frame = Atom(sgre::resource::PsbObject{{"time", Atom(times[index])}, {"type", Atom(type)}});
		if (type != 0) {
			std::get<sgre::resource::PsbObject>(frame.Data).emplace_back("content", Atom(sgre::resource::PsbObject{{"value", Atom(values[index])}, {"interval", Atom(static_cast<std::int64_t>(interval))}, {"easing", Atom(std::monostate{})}}));
		}
		frames.push_back(std::move(frame));
	}
	MutableProperty(timeline, "variable") = Atom(sgre::resource::PsbArray{Atom(sgre::resource::PsbObject{{"label", Atom(std::string("drive"))}, {"scope", Atom(std::move(scope))}, {"frameList", Atom(std::move(frames))}})});
	sgre::resource::PsbArray layers;
	if (bound_layer) {
		auto layer = Layer("driver", false);
		MutableProperty(layer, "type") = Atom(std::int64_t{2});
		MutableProperty(layer, "inheritMask") = Atom(std::int64_t{0});
		MutableProperty(layer, "parameterize") = Atom(std::int64_t{0});
		std::get<sgre::resource::PsbObject>(layer.Data).emplace_back("meshCombine", Atom(false));
		sgre::resource::PsbArray local_frames;
		for (const auto time : std::array<double, 3>{0, 4, 8}) {
			local_frames.push_back(Atom(sgre::resource::PsbObject{{"time", Atom(time)}, {"type", Atom(std::int64_t{3})}, {"content", Atom(sgre::resource::PsbObject{{"mask", Atom(std::int64_t{2})}, {"coord", Atom(sgre::resource::PsbArray{Atom(time * 10), Atom(0.0), Atom(0.0)})}})}}));
		}
		MutableProperty(layer, "frameList") = Atom(std::move(local_frames));
		layers.push_back(std::move(layer));
		std::get<sgre::resource::PsbObject>(timeline.Data).emplace_back("layerIndexMap", Atom(sgre::resource::PsbObject{{"driver", Atom(std::int64_t{0})}}));
	}
	MutableProperty(timeline, "layer") = Atom(std::move(layers));
	return document;
}

static void TestActualTimelineVariableRecords() {
	for (std::size_t test = 0; test != kNativeTimelineVariableStates.size(); ++test) {
		Fixture fixture(false, false, false, false, 32, false, 0, VariableDocument(static_cast<std::uint32_t>(test), test == 4));
		auto task = fixture.Attach();
		task.SetVisible(true);
		task.Play("main");
		const auto* owner = task.Commands().TimelineVariables();
		Require(owner != nullptr && owner->Count() == 1, "actual Play must own its serialized variable record before selected-node construction");
		for (std::size_t phase = 0; phase != kNativeTimelineVariableStates[test].size(); ++phase) {
			const auto& expected = kNativeTimelineVariableStates[test][phase];
			const auto position = std::bit_cast<float>(expected.EvaluationBits);
			if (phase == 7) {
				task.SetTickCount(position);
			}
			if (phase == 9) {
				task.Stop();
			}
			if (phase == 10) {
				task.Clear();
			}
			task.Progress(phase == 7 ? 0 : position - task.Commands().ClockState().TickCount);
			Require(owner == task.Commands().TimelineVariables(), "Clear/ordinary updates must retain the genuine current variable owner");
			const auto& state = owner->State(0);
			Require(state.CurrentSlot == expected.CurrentSlot && std::bit_cast<std::uint32_t>(state.Value) == expected.ValueBits, "variable native current slot/value differs");
			for (std::size_t slot = 0; slot != 2; ++slot) {
				const auto& cell = state.Cells[slot];
				Require(cell.FrameIndex == expected.FrameIndices[slot] && cell.Hidden == expected.Hidden[slot] && cell.Tween == expected.Tween[slot] && std::bit_cast<std::uint32_t>(cell.Value) == expected.CellValueBits[slot], "variable native physical-slot retention differs");
			}
			const auto& timeline = *task.Commands().CachedTimeline();
			const auto* parameters = task.Commands().ParameterValues().CurrentTimelineParameters(timeline);
			Require(parameters != nullptr && parameters->Entries().size() == 1 && std::bit_cast<std::uint32_t>(parameters->Entries().front().Position) == expected.ParameterBits && parameters->Entries().front().ControlFlags == 0, "variable phase must update the actual registered parameter with native flags0");
			if (test == 4 && phase != 10) {
				const auto* scene = task.GenericPlayer()->Scene();
				const auto layer = scene->FindLayer("driver");
				Require(layer.has_value(), "numeric variable fixture requires its actual serialized layer-index map");
				const auto x = scene->LayerCoordinate(*layer)[0];
				Require(std::abs(x - std::bit_cast<float>(expected.LocalXBits)) <= 0.000001f, "native old-cursor/new-parameter numeric coordinate differs");
			}
		}
		task.Play("main", sgre::scene::MotionPlayFlags::Force);
		Require(task.Commands().TimelineVariables()->State(0).Initialized == false, "Force must recreate the real variable slots rather than retain a prior decoded record");
		task.Progress(0);
	}
}

static void TestTimelineVariableHiddenAndDescriptorOwners() {
	Fixture hidden(false, false, false, false, 32, false, 0, VariableDocument(1, false, true));
	auto task = hidden.Attach();
	task.Play("main");
	task.Progress(0);
	const auto before = task.GetVariable("drive");
	task.Progress(5);
	Require(task.GetVariable("drive") == before && task.Commands().TimelineVariables()->State(0).Cells[1].Hidden, "a hidden current variable control must retain the actual value map");
	task.SetVariable("drive", 0.75f);
	task.Progress(1);
	Require(task.GetVariable("drive") == 0.75f, "hidden variable control must not manufacture a new setter write");
	Fixture scoped(false, false, false, false, 32, false, 0, VariableDocument(0, false, false, "path"));
	auto descriptor = scoped.Attach();
	descriptor.Play("main");
	descriptor.Progress(0);
	Require(descriptor.GetVariable("path::drive") == -0.5f && descriptor.GetVariable("drive") == 0, "scope must construct the native descriptor key and use its actual controller map");
}

static void TestTimelineVariablesUseActualSelectedChild() {
	auto document = Document();
	auto& objects = MutableProperty(document->Root, "object");
	auto& parent = MutableProperty(MutableProperty(MutableProperty(objects, "character"), "motion"), "main");
	std::get<sgre::resource::PsbObject>(parent.Data).emplace_back("layerIndexMap", Atom(sgre::resource::PsbObject{{"selected-child", Atom(std::int64_t{1})}}));
	auto& child = MutableProperty(MutableProperty(MutableProperty(objects, "child"), "motion"), "main");
	auto variables = VariableDocument(1, false);
	auto& source = MutableProperty(MutableProperty(MutableProperty(MutableProperty(variables->Root, "object"), "character"), "motion"), "main");
	MutableProperty(child, "variable") = MutableProperty(source, "variable");
	MutableProperty(child, "parameter") = MutableProperty(source, "parameter");
	Fixture fixture(false, false, false, false, 32, false, 0, document);
	auto task = fixture.Attach();
	task.SetVisible(true);
	task.Play("main");
	for (const auto delta : std::array<float, 2>{0, 5}) {
		task.Progress(delta);
		const auto* scene = task.GenericPlayer()->Scene();
		const auto reference = scene->FindLayer("selected-child");
		Require(reference.has_value(), "child variable fixture requires its genuine target map");
		const auto& commands = scene->LayerChild(*reference).Commands();
		Require(commands.TimelineVariables() != nullptr && commands.TimelineVariables()->Uses(commands, *commands.CachedTimeline()) && commands.ParameterValues().ReadLocal("drive") == (delta == 0 ? -0.5f : 1.5f) && task.GetVariable("drive") == 0, "selected-child variables must update the actual child map without redirecting to the parent");
	}
	task.Clear();
	Require(task.Commands().ParameterValues().RegisteredParameterCount() == 0 && fixture.Runtime->Identifiers().LiveCount() == 0, "actual child variable retirement must release all descendant parameter registrations and IDs");
}

static void TestMalformedTimelineVariablesRemainRejected() {
	for (std::size_t malformed = 0; malformed != 3; ++malformed) {
		auto document = VariableDocument(1, false);
		auto& timeline = MutableProperty(MutableProperty(MutableProperty(MutableProperty(document->Root, "object"), "character"), "motion"), "main");
		auto& variable = std::get<sgre::resource::PsbArray>(MutableProperty(timeline, "variable").Data).front();
		auto& frames = std::get<sgre::resource::PsbArray>(MutableProperty(variable, "frameList").Data);
		if (malformed == 0) {
			MutableProperty(frames.front(), "type") = Atom(std::int64_t{1});
		} else if (malformed == 1) {
			frames.resize(1);
		} else {
			MutableProperty(frames[1], "time") = Atom(0.0);
		}
		Fixture fixture(false, false, false, false, 32, false, 0, document);
		auto task = fixture.Attach();
		RequireFailure([&] {
			task.Play("main");
		},
		    "motion timeline variable");
		Require(task.Commands().SelectionSerial() == 0 && task.Commands().CachedTimeline() == nullptr && fixture.Runtime->Identifiers().LiveCount() == 0, "malformed variable metadata must not create a fake selected owner");
	}
}

static void TestActualPlainVelocityPhase() {
	Fixture fixture;
	sgre::scene::MotionPlayerCommands commands(fixture.Manager);
	sgre::scene::MotionRootScene root;
	sgre::scene::MotionPlayerRenderControls render;
	sgre::scene::MotionScenePlayer player(*fixture.Runtime, commands, root, render, [](const auto&) {
	});
	commands.SetCharacter("character");
	commands.Play("main");
	player.Rebuild(*commands.SelectedProject());
	root.SetVisible(true);
	root.SetVelocity({0.5f, -1.25f, 0.125f});
	player.Progress(2);
	Require(root.Overrides().Coordinate == std::array<float, 3>{1, -2.5f, 0.25f}, "actual selected update must advance velocity once using its real scaled delta before composition");
	Require(root.Frame().Hierarchy.Attributes.Coordinate == root.Overrides().Coordinate, "published selected geometry must use the already advanced root");
	player.Draw();
	Require(fixture.Runtime->SceneContext().Size() == 1, "the actual moved scene must remain draw-publishable");
	fixture.Runtime->SceneContext().Clear();
	player.Clear();
	player.Progress(1);
	Require(root.Overrides().Coordinate == std::array<float, 3>{1.5f, -3.75f, 0.375f}, "the genuine cleared root-only player retains velocity and its ordinary composition phase");
}

static std::shared_ptr<sgre::resource::PsbDocument> NormalizationDocument(std::uint32_t inherit, bool nested, bool independent) {
	auto document = Document();
	auto& objects = MutableProperty(document->Root, "object");
	auto& main = MutableProperty(MutableProperty(MutableProperty(objects, "character"), "motion"), "main");
	auto* local = &main;
	if (nested) {
		auto& parent_layer = std::get<sgre::resource::PsbArray>(MutableProperty(main, "layer").Data)[1];
		MutableProperty(parent_layer, "inheritMask") = Atom(std::int64_t{0x7fc});
		MutableProperty(parent_layer, "motionIndependentLayerInherit") = Atom(independent);
		auto& parent_frame = std::get<sgre::resource::PsbArray>(MutableProperty(parent_layer, "frameList").Data).front();
		MutableProperty(MutableProperty(parent_frame, "content"), "coord") = Atom(sgre::resource::PsbArray{Atom(0.0), Atom(0.0), Atom(0.0)});
		local = &MutableProperty(MutableProperty(MutableProperty(objects, "child"), "motion"), "main");
	} else {
		std::get<sgre::resource::PsbArray>(MutableProperty(main, "layer").Data).resize(1);
		auto& priority = std::get<sgre::resource::PsbArray>(MutableProperty(main, "priority").Data).front();
		MutableProperty(priority, "content") = Atom(sgre::resource::PsbArray{Atom(std::int64_t{0})});
	}
	auto& layer = std::get<sgre::resource::PsbArray>(MutableProperty(*local, "layer").Data).front();
	MutableProperty(layer, "inheritMask") = Atom(static_cast<std::int64_t>(inherit));
	auto& frame = std::get<sgre::resource::PsbArray>(MutableProperty(layer, "frameList").Data).front();
	MutableProperty(frame, "content") = Atom(sgre::resource::PsbObject{{"mask", Atom(std::int64_t{0x5fe})}, {"src", Atom(std::string("atlas"))}, {"icon", Atom(std::string("tile"))}, {"coord", Atom(sgre::resource::PsbArray{Atom(2.0), Atom(3.0), Atom(0.5)})}, {"angle", Atom(15.0)}, {"zx", Atom(0.75)}, {"zy", Atom(1.25)}, {"sx", Atom(0.125)}, {"sy", Atom(-0.25)}, {"fx", Atom(false)}, {"fy", Atom(true)}, {"opa", Atom(std::int64_t{200})}});

	return document;
}

static void RequireNativeNormalization(const sgre::scene::MotionSceneEvaluation& scene, std::string_view label, const NativeMotionNormalization& expected) {
	const sgre::scene::MotionComposedLayer* found = nullptr;
	for (const auto* row : scene.PacketRows()) {
		if (row->Node().Prepared->Label == label) {
			Require(found == nullptr, "native normalization fixture must resolve one exact selected drawing row");
			found = scene.DrawState(*row).Numeric;
		}
	}
	Require(found != nullptr && found->Active, "native normalization requires the actual completed visible source row");
	const auto& pose = found->Hierarchy.Attributes;
	const auto& matrix = found->Hierarchy.Transform;
	const std::array<float, 4> actual_matrix{matrix.XX, matrix.XY, matrix.YX, matrix.YY};
	const std::array<float, 8> actual_attributes{pose.Coordinate[0], pose.Coordinate[1], pose.Coordinate[2], pose.Angle, pose.ZoomX, pose.ZoomY, pose.ShearX, pose.ShearY};
	for (std::size_t index = 0; index < actual_matrix.size(); ++index) {
		Require(std::bit_cast<std::uint32_t>(actual_matrix[index]) == expected.Matrix[index], "actual containing normalization matrix differs from unchanged original50c550");
	}
	for (std::size_t index = 0; index < actual_attributes.size(); ++index) {
		Require(std::bit_cast<std::uint32_t>(actual_attributes[index]) == expected.Attributes[index], "actual containing normalization pose differs from unchanged original50c550");
	}
	Require(pose.Opacity == expected.Attributes[8] && pose.FlipX == (expected.Flips[0] != 0) && pose.FlipY == (expected.Flips[1] != 0), "actual root opacity/flip normalization differs from unchanged original50c550");
}

static void TestActualConstructorDefaultNormalization() {
	for (const auto& expected : kNativeMotionNormalization) {
		const bool nested = expected.OwnerKind != 0;
		Fixture fixture(false, false, false, false, 32, false, 0, NormalizationDocument(expected.Inherit, nested, expected.OwnerKind == 2));
		auto task = fixture.Attach();
		task.SetVisible(true);
		task.SetCoordinate3({3, -4, 1});
		task.SetZoom(2, 0.5f);
		task.SetOpacity(128);
		task.Play("main");
		task.Progress(0);
		RequireNativeNormalization(*task.GenericPlayer()->Scene(), nested ? "nested" : "sprite", expected);
		fixture.Tasks.Group(sgre::scene::MotionTaskGroup::Second).Draw(0);
		fixture.Runtime->SceneContext().Clear();
	}
	for (const auto& expected : kNativeMotionNormalization) {
		if (expected.OwnerKind != 0) {
			continue;
		}
		Fixture fixture(false, false, false, false, 32, false, 0, NormalizationDocument(expected.Inherit, false, false));
		sgre::scene::MotionPlayerCommands parent(fixture.Manager);
		parent.SetCharacter("character");
		parent.Play("main");
		sgre::scene::MotionRootScene parent_root;
		sgre::scene::MotionPlayerRenderControls render;
		sgre::scene::MotionChildConstructionServices services;
		services.ReadManagerRandom = [&fixture](const auto& manager) {
			Require(&manager == &fixture.Manager, "particle normalization must use its actual containing manager");
			return fixture.Runtime->NextUnitFloat();
		};
		sgre::scene::MotionParticlePlayerState particle(parent, parent_root, render, services);
		particle.SelectInitialTarget({"character", "main"});
		particle.Root().SetCoordinate3({3, -4, 1});
		particle.Root().SetZoom(2, 0.5f);
		particle.Root().SetOpacity(128);
		sgre::scene::MotionScenePlayer player(*fixture.Runtime, particle.Commands(), particle.Root(), particle.RenderControls(), [](const auto&) {
		});
		player.Rebuild(*particle.Commands().SelectedProject());
		player.Progress(0);
		RequireNativeNormalization(*player.Scene(), "sprite", expected);
	}
}

static void TestClearedTaskOutlivesFinalResourceUnregister() {
	for (const bool timeline_scalar : {false, true}) {
		Fixture fixture(true, false, false, timeline_scalar);
		auto task = fixture.Attach();
		task.SetVisible(true);
		task.SetCoordinate3({7, 11, 13});
		task.Play("main");
		task.Progress(0);
		const auto* timeline = task.Commands().CachedTimeline();
		const auto* parameter_owner = task.Commands().ParameterValues().CurrentTimelineParameters(*timeline);
		const auto* parameter_cells = parameter_owner->Entries().data();
		const auto* variables = task.Commands().TimelineVariables();
		const auto serial = task.Commands().SelectionSerial();
		const auto events = fixture.Events.size();
		const auto random = task.Random().State();
		std::weak_ptr<const sgre::scene::LoadedMotionAsset> loaded = fixture.Asset;
		std::weak_ptr<const sgre::resource::PsbDocument> document = fixture.Asset->Document;
		std::weak_ptr<const std::vector<std::uint8_t>> pixels = fixture.Asset->Backing;
		std::weak_ptr<const sgre::resource::MotionResource> metadata = fixture.Asset->Model;
		Require(task.GenericPlayer()->Scene() && fixture.Runtime->Identifiers().LiveCount() == 3 && parameter_owner->Entries().size() == 1 && variables != nullptr, "unregister regression requires a real visible selected tree and current scalar/variable record owners");

		task.Clear();
		const auto next_uid = fixture.Runtime->Identifiers().NextIdentifier();
		const auto retired_uids = fixture.Runtime->Identifiers().RetiredCount();
		Require(task.Commands().SelectedLayersCleared() && !task.GenericPlayer()->Scene() && fixture.Runtime->Identifiers().LiveCount() == 0 && fixture.Runtime->PlayerCount() == 1, "actual Clear must retire selected nodes while retaining its Active containing task");
		Require(fixture.Manager.Unregister(fixture.Project) && fixture.Manager.Count() == 0 && fixture.Cache.Count() == 0 && fixture.Releases == 1, "final unregister must release real registration textures before the cleared task retires");
		fixture.Asset.reset();
		fixture.Catalog.Prune();
		Require(loaded.expired() && document.expired() && pixels.expired() && !metadata.expired(), "cleared metadata lifetime must not pin loaded PSB document, pixel backing or GPU resource owners");
		Require(!fixture.Runtime->ResourceBorrowsLive() && task.GenericPlayer()->ResourcesLive(), "a cleared exact cached selection must remain phase-live while its expired source-registration lease remains observably stale");

		task.SetVariable("param", 6, 7);
		auto& second = fixture.Tasks.Group(sgre::scene::MotionTaskGroup::Second);
		second.Update();
		Require(task.Commands().ClockState().TickCount == (timeline_scalar ? 6 : 1) && parameter_owner->Entries().front().Position == 6 && parameter_owner->Entries().front().ControlFlags == 0, "actual cleared task Update must sample real frame delta or retained whole-timeline scalar and acknowledge native parameter flags");
		task.SetVariable("param", 2);
		task.Progress(2);
		Require(task.Commands().ClockState().TickCount == (timeline_scalar ? 2 : 3) && task.Root().Frame().Composed && task.Root().Frame().Hierarchy.Attributes.Coordinate == std::array<float, 3>{7, 11, 13}, "explicit cleared Progress must preserve parameter-driven reverse traversal and compose its real root after unregister");
		second.Draw(0);
		task.Clear();
		Require(fixture.Runtime->Context().Size() == 0 && fixture.Runtime->SceneContext().Size() == 0 && !task.GenericPlayer()->Scene() && fixture.Runtime->Identifiers().LiveCount() == 0 && fixture.Runtime->Identifiers().NextIdentifier() == next_uid && fixture.Runtime->Identifiers().RetiredCount() == retired_uids, "cleared Draw and repeated Clear must stay empty without allocating UIDs or republishing retired selected rows");
		Require(task.Commands().CachedTimeline() == timeline && task.Commands().SelectionSerial() == serial && task.Commands().TimelineVariables() == variables && task.Commands().ParameterValues().CurrentTimelineParameters(*timeline) == parameter_owner && parameter_owner->Entries().data() == parameter_cells && task.Random().State() == random && fixture.Events.size() == events && second.State(task.Handle()).Status == sgre::scene::TaskStatus::Active, "unregister must not replace exact cached owners, consume RNG, manufacture events or retire the surviving Active task");
		fixture.Tasks.Clear();
		Require(!task.IsLive() && metadata.expired() && fixture.Runtime->CanClose(), "true task retirement must release retained immutable metadata and every parameter/UID owner");
	}

	{
		Fixture fixture(false, false, false, false, 32, false, 0, VariableDocument(0, true));
		auto task = fixture.Attach();
		task.SetVisible(true);
		task.Play("main");
		task.Progress(0);
		const auto* variables = task.Commands().TimelineVariables();
		const auto* timeline = task.Commands().CachedTimeline();
		Require(variables && variables->Count() == 1 && variables->State(0).Value == -.5f && fixture.Runtime->Identifiers().LiveCount() == 1, "retained variable regression requires an actual initialized serialized tween and selected descriptor target");
		task.Clear();
		Require(fixture.Manager.Unregister(fixture.Project), "retained variable regression must perform the actual final unregister");
		fixture.Asset.reset();
		fixture.Tasks.Group(sgre::scene::MotionTaskGroup::Second).Update();
		task.Progress(4);
		const auto* parameter_owner = task.Commands().ParameterValues().CurrentTimelineParameters(*timeline);
		Require(task.Commands().ClockState().TickCount == 5 && variables == task.Commands().TimelineVariables() && variables->State(0).Value == 1.5f && task.GetVariable("drive") == 1.5f && parameter_owner->Entries().front().Position == 1.5f && parameter_owner->Entries().front().ControlFlags == 0, "retained current variable cells must continue genuine cursor/application work after Clear and unregister with no selected target");
		fixture.Tasks.Group(sgre::scene::MotionTaskGroup::Second).Draw(0);
		Require(fixture.Runtime->Identifiers().LiveCount() == 0 && fixture.Runtime->SceneContext().Size() == 0 && fixture.Runtime->Context().Size() == 0, "retained variable application must not resurrect selected geometry or enqueue retired draw owners");
		fixture.Tasks.Clear();
	}

	{
		Fixture fixture;
		auto task = fixture.Attach();
		task.SetVisible(true);
		task.Play("main");
		task.Progress(0);
		const auto* active_scene = task.GenericPlayer()->Scene();
		const auto ticks = task.Commands().ClockState().TickCount;
		Require(active_scene && fixture.Runtime->Identifiers().LiveCount() == 3 && !task.Commands().SelectedLayersCleared(), "negative unregister case requires a genuine active selected tree");
		Require(fixture.Manager.Unregister(fixture.Project), "negative active case must remove the actual resource registration");
		RequireFailure([&] {
			static_cast<void>(task.Commands().TimelineVariables()->State(0));
		},
		    "exact live current-player/resource owner");
		RequireFailure([&] {
			fixture.Tasks.Group(sgre::scene::MotionTaskGroup::Second).Update();
		},
		    "motion scene phase");
		RequireFailure([&] {
			task.Progress(1);
		},
		    "motion scene phase");
		RequireFailure([&] {
			fixture.Tasks.Group(sgre::scene::MotionTaskGroup::Second).Draw(0);
		},
		    "motion scene phase");
		Require(!task.GenericPlayer()->ResourcesLive() && task.GenericPlayer()->Scene() == active_scene && task.Commands().ClockState().TickCount == ticks && fixture.Runtime->Identifiers().LiveCount() == 3 && fixture.Runtime->SceneContext().Size() == 0, "active-tree unregister must still reject all phases before clock, selected-owner or queued-publication mutation");
		fixture.Tasks.Clear();
	}

	{
		Fixture fixture;
		auto runtime = std::move(fixture.Runtime);
		sgre::scene::MotionPlayerCommands commands(fixture.Manager);
		sgre::scene::MotionRootScene root;
		sgre::scene::MotionPlayerRenderControls render;
		sgre::scene::MotionScenePlayer player(*runtime, commands, root, render, [](const auto&) {
		});
		Require(runtime->CanClose() && player.ResourcesLive(), "closed-owner negative case must use an empty real runtime with no task or preparation lease");
		runtime->Close();
		RequireFailure([&] {
			player.Update();
		},
		    "motion scene phase");
		RequireFailure([&] {
			player.Progress(0);
		},
		    "motion scene phase");
		RequireFailure([&] {
			player.Draw();
		},
		    "motion scene phase");
		Require(runtime->IsClosed() && !player.ResourcesLive() && !root.Frame().Composed && commands.ClockState().TickCount == 0, "no-selection root-only phases must retain the actual runtime-closed gate before publication");
	}
}

static void TestFreshSceneSelectionAfterClearedSnapshotRetires() {
	Fixture fixture;
	auto old = fixture.Attach();
	old.SetVisible(true);
	old.Play("main");
	old.Progress(0);
	Require(old.GenericPlayer()->Scene() && fixture.Runtime->Identifiers().LiveCount() == 3, "preparation scope requires a genuine completed selected root and child");
	const auto old_identity = fixture.Asset->Identity;
	const auto old_project = fixture.Project;
	std::weak_ptr<const sgre::scene::LoadedMotionAsset> old_loaded = fixture.Asset;
	std::weak_ptr<const sgre::resource::PsbDocument> old_document = fixture.Asset->Document;
	std::weak_ptr<const std::vector<std::uint8_t>> old_pixels = fixture.Asset->Backing;
	std::weak_ptr<const sgre::resource::MotionResource> old_metadata = fixture.Asset->Model;
	old.Clear();
	Require(fixture.Manager.Unregister(old_project) && fixture.Cache.Count() == 0 && fixture.Releases == 1, "old selected source must actually unregister before the new scene is prepared");
	fixture.Asset.reset();
	fixture.Catalog.Prune();
	Require(old_loaded.expired() && old_document.expired() && old_pixels.expired() && !old_metadata.expired() && old.Commands().SelectedLayersCleared() && !old.GenericPlayer()->Scene() && !fixture.Runtime->ResourceBorrowsLive(), "old cleared owner keeps only actual cached metadata and its observably stale strict snapshot");

	fixture.Asset = fixture.Catalog.Prepare(Document(), std::make_shared<const std::vector<std::uint8_t>>(144, 255));
	fixture.Project = fixture.Manager.Register(fixture.Asset->Identity, fixture.Asset->Model, *fixture.Asset->Backing);
	Require(fixture.Project != old_project && fixture.Asset->Identity != old_identity && fixture.Uploads == 2 && fixture.Manager.Count() == 1, "new resource must have its own actual catalog identity and registered texture owner");
	const auto selection = fixture.Manager.ResolveTimeline("character", "main");
	Require(selection.has_value() && selection->Project == fixture.Project, "fresh selection must resolve through the current actual manager");
	RequireFailure([&] {
		static_cast<void>(fixture.Runtime->Prepare(*selection));
	},
	    "actual live selection and registered backing outside reentry");

	auto fresh = fixture.Attach();
	fresh.SetVisible(true);
	fresh.Play("main");
	fresh.Progress(0);
	Require(fresh.GenericPlayer()->Scene() && fresh.GenericPlayer()->ResourcesLive() && fixture.Runtime->Identifiers().LiveCount() == 3 && fixture.Runtime->PlayerCount() == 2 && !fixture.Runtime->ResourceBorrowsLive(), "new actual scene uses its own current snapshot while the cleared old snapshot remains honestly stale");
	const auto* fresh_scene = fresh.GenericPlayer()->Scene();
	const auto serial = fresh.Commands().SelectionSerial();
	old.Progress(1);
	Require(!old.GenericPlayer()->Scene() && old.GenericPlayer()->ResourcesLive() && old.Commands().SelectedLayersCleared(), "new scene preparation must not resurrect or invalidate the old cleared containing player");
	fixture.Tasks.Group(sgre::scene::MotionTaskGroup::Second).Draw(0x76543210);
	Require(fixture.Runtime->SceneContext().Size() == 1 && fixture.Runtime->SceneContext().IsBorrowing(*fresh_scene), "only the genuine new selected scene may enqueue a draw occurrence");
	RequireFailure([&] {
		fresh.Progress(1);
	},
	    "queued row borrows");
	const std::array<sgre::resource::MotionPoint, 1> sizes{{{4, 4}}};
	{
		const auto frame = fixture.Runtime->SceneContext().Prepare(Globals(sizes));
		Require(frame.Queue.size() == 2 && frame.Nodes.size() == 3, "fresh current resource must prepare its real root and child geometry after old source retirement");
	}
	fixture.Runtime->SceneContext().Clear();

	old.RequestRetire();
	fixture.Tasks.Group(sgre::scene::MotionTaskGroup::Second).Update();
	Require(!old.IsLive() && old_metadata.expired() && fixture.Runtime->ResourceBorrowsLive() && fresh.GenericPlayer()->Scene() == fresh_scene && fresh.Commands().SelectionSerial() == serial, "source-first true task retirement releases old metadata and strict union borrow without disturbing fresh selection");
	{
		const auto strict = fixture.Runtime->Prepare(*selection);
		Require(strict.Owner.UsesRuntime(*fixture.Runtime) && strict.Owner.Owns(fixture.Manager, *selection->Timeline, &strict.Timeline), "legacy full preparation remains usable after all of its actual union snapshots are live");
	}

	auto invalid = *selection;
	invalid.Identity = old_identity;
	RequireFailure([&] {
		static_cast<void>(fixture.Runtime->PrepareScene(invalid));
	},
	    "actual live selection and registered backing outside reentry");
	invalid = *selection;
	invalid.Timeline = nullptr;
	RequireFailure([&] {
		static_cast<void>(fixture.Runtime->PrepareScene(invalid));
	},
	    "actual live selection and registered backing outside reentry");
	sgre::resource::MotionObject foreign_character;
	invalid = *selection;
	invalid.Character = &foreign_character;
	RequireFailure([&] {
		static_cast<void>(fixture.Runtime->PrepareScene(invalid));
	},
	    "selection is outside its actual immutable registration");
	sgre::resource::MotionTimeline foreign_timeline;
	invalid = *selection;
	invalid.Timeline = &foreign_timeline;
	RequireFailure([&] {
		static_cast<void>(fixture.Runtime->PrepareScene(invalid));
	},
	    "selection is outside its actual immutable registration");

	const auto tick = fresh.Commands().ClockState().TickCount;
	const auto uids = fixture.Runtime->Identifiers().LiveCount();
	Require(fixture.Manager.Unregister(fixture.Project) && fixture.Cache.Count() == 0 && fixture.Releases == 2, "active new snapshot negative must actually remove its own selected texture registration");
	RequireFailure([&] {
		fresh.Progress(1);
	},
	    "motion scene phase");
	RequireFailure([&] {
		fixture.Tasks.Group(sgre::scene::MotionTaskGroup::Second).Draw(0);
	},
	    "motion scene phase");
	Require(!fresh.GenericPlayer()->ResourcesLive() && fresh.GenericPlayer()->Scene() == fresh_scene && fresh.Commands().ClockState().TickCount == tick && fixture.Runtime->Identifiers().LiveCount() == uids && fixture.Runtime->SceneContext().Size() == 0, "active selected dependency loss must still fail before clock, UID, geometry or publication mutation");
	fixture.Tasks.Clear();
	Require(fixture.Runtime->PlayerCount() == 0 && fixture.Runtime->Identifiers().LiveCount() == 0 && fixture.Uploads == fixture.Releases, "new scene and old cleared snapshot retire all real task, UID and texture owners");
	fixture.Runtime->Close();
	fixture.Runtime.reset();
}

static void TestScenePreparationKeepsEveryCurrentSnapshotMember() {
	Fixture fixture;
	fixture.Runtime->Close();
	fixture.Runtime.reset();
	const auto other = fixture.Catalog.Prepare(Document(), std::make_shared<const std::vector<std::uint8_t>>(144, 255));
	const auto other_project = fixture.Manager.Register(other->Identity, other->Model, *other->Backing);
	auto services = fixture.Services();
	services.ReadSourceAliases = [&](auto project) {
		if (project == other_project) {
			return std::span<const sgre::scene::MotionSourceAlias>(other->Aliases);
		}
		Require(project == fixture.Project, "snapshot aliases require an exact current registered owner");

		return std::span<const sgre::scene::MotionSourceAlias>(fixture.Asset->Aliases);
	};
	services.ReadRegisteredAsset = [&](auto project) -> const sgre::scene::LoadedMotionAsset& {
		if (project == other_project) {
			return *other;
		}
		Require(project == fixture.Project, "snapshot asset requires an exact current registered owner");

		return *fixture.Asset;
	};
	fixture.Runtime = std::make_unique<sgre::scene::MotionManagerRuntime>(fixture.Manager, fixture.Clock, fixture.Seed, std::move(services), 32);
	auto task = fixture.Attach();
	task.SetVisible(true);
	task.Play("main");
	task.Progress(0);
	const auto* scene = task.GenericPlayer()->Scene();
	const auto tick = task.Commands().ClockState().TickCount;
	const auto serial = task.Commands().SelectionSerial();
	const auto uids = fixture.Runtime->Identifiers().LiveCount();
	Require(scene && task.Commands().SelectedProject()->Project == fixture.Project && task.GenericPlayer()->ResourcesLive() && fixture.Manager.Count() == 2 && uids == 3, "current snapshot negative requires its selected resource and another real registered snapshot member");
	Require(fixture.Manager.Unregister(other_project) && fixture.Manager.Contains(fixture.Project) && fixture.Cache.Count() == 1, "snapshot negative must remove only the other member while selected resource and its sampled texture stay registered");
	Require(!task.GenericPlayer()->ResourcesLive() && !fixture.Runtime->ResourceBorrowsLive(), "own complete preparation snapshot must reject loss of an unselected member rather than reducing readiness to selected project alone");
	RequireFailure([&] {
		task.Progress(1);
	},
	    "motion scene phase");
	RequireFailure([&] {
		fixture.Tasks.Group(sgre::scene::MotionTaskGroup::Second).Draw(0);
	},
	    "motion scene phase");
	RequireFailure([&] {
		static_cast<void>(sgre::scene::MotionPlayerTask::AttachClone(*fixture.Runtime, fixture.Tasks, task, false, [](const auto&) {
		}));
	},
	    "motion");
	Require(task.GenericPlayer()->Scene() == scene && task.Commands().ClockState().TickCount == tick && task.Commands().SelectionSerial() == serial && fixture.Runtime->Identifiers().LiveCount() == uids && fixture.Runtime->PlayerCount() == 1 && fixture.Tasks.Group(sgre::scene::MotionTaskGroup::Second).Count() == 1 && fixture.Runtime->SceneContext().Size() == 0, "lost snapshot member must block phase and clone before clock, geometry, UID, task or publication mutation");
	fixture.Tasks.Clear();
	fixture.Runtime->Close();
	fixture.Runtime.reset();
	Require(fixture.Manager.Unregister(fixture.Project) && fixture.Cache.Count() == 0 && fixture.Uploads == 2 && fixture.Releases == 2, "complete snapshot rejection must preserve balanced actual retirement");
}

struct SceneCloneEndpointCleanup {
	sgre::scene::MotionManagerRuntime& Runtime;
	sgre::scene::MotionLayerTasks& Tasks;

	~SceneCloneEndpointCleanup() noexcept {
		try {
			if (!Runtime.IsClosed()) {
				Runtime.SceneContext().Clear();
				Tasks.Clear();
				Runtime.Close();
			}
		} catch (...) {
			std::terminate();
		}
	}
};

static void TestActualSceneCloneWithUnrelatedClearedSnapshots() {
	for (const bool cross_manager : {false, true}) {
		Fixture fixture;
		sgre::scene::MotionResourceManager destination(fixture.Cache);
		sgre::core::FrameClock destination_clock(0);
		sgre::scene::MotionLayerTasks destination_tasks(8);
		auto destination_project = destination.Register(fixture.Asset->Identity, fixture.Asset->Model, *fixture.Asset->Backing);
		sgre::scene::MotionManagerRuntimeServices services;
		services.SceneAssets = &fixture.Catalog;
		services.ReadSourceAliases = [&](auto project) {
			Require(project == destination_project, "destination clone aliases require its actual current registration");

			return std::span<const sgre::scene::MotionSourceAlias>(fixture.Asset->Aliases);
		};
		services.ReadRegisteredAsset = [&](auto project) -> const sgre::scene::LoadedMotionAsset& {
			Require(project == destination_project, "destination clone asset requires its actual current registration");

			return *fixture.Asset;
		};
		sgre::scene::MotionManagerRuntime destination_runtime(destination, destination_clock, fixture.Seed, std::move(services), 32);
		SceneCloneEndpointCleanup endpoint_cleanup{destination_runtime, destination_tasks};
		auto old_source = fixture.Attach();
		auto old_destination = sgre::scene::MotionPlayerTask::Attach(destination_runtime, destination_tasks, false, "character", [](const auto&) {
		});
		old_source.SetVisible(true);
		old_destination.SetVisible(true);
		old_source.Play("main");
		old_destination.Play("main");
		old_source.Progress(0);
		old_destination.Progress(0);
		const std::weak_ptr<const sgre::scene::LoadedMotionAsset> old_loaded = fixture.Asset;
		const std::weak_ptr<const sgre::resource::MotionResource> old_model = fixture.Asset->Model;
		old_source.Clear();
		old_destination.Clear();
		Require(fixture.Manager.Unregister(fixture.Project) && destination.Unregister(destination_project) && fixture.Cache.Count() == 0, "clone scope setup must genuinely retire both old source registrations");
		fixture.Asset.reset();
		fixture.Catalog.Prune();
		Require(old_loaded.expired() && !old_model.expired() && !fixture.Runtime->ResourceBorrowsLive() && !destination_runtime.ResourceBorrowsLive(), "source and destination each retain their own cleared historical snapshot without a loaded asset or live registration");

		fixture.Asset = fixture.Catalog.Prepare(Document(), std::make_shared<const std::vector<std::uint8_t>>(144, 255));
		fixture.Project = fixture.Manager.Register(fixture.Asset->Identity, fixture.Asset->Model, *fixture.Asset->Backing);
		destination_project = destination.Register(fixture.Asset->Identity, fixture.Asset->Model, *fixture.Asset->Backing);
		auto source = fixture.Attach();
		source.SetVisible(true);
		source.Play("main");
		source.Progress(1);
		auto& clone_runtime = cross_manager ? destination_runtime : *fixture.Runtime;
		auto& clone_tasks = cross_manager ? destination_tasks : fixture.Tasks;
		const auto random = source.Random().State();
		const auto tick = source.Commands().ClockState().TickCount;
		const auto* source_scene = source.GenericPlayer()->Scene();
		auto clone = sgre::scene::MotionPlayerTask::AttachClone(clone_runtime, clone_tasks, source, false, [](const auto&) {
		});
		const auto* clone_scene = clone.GenericPlayer()->Scene();
		Require(source_scene && clone_scene && source_scene != clone_scene && source_scene->PacketRows().size() == 3 && clone_scene->PacketRows().size() == 3 && source.Random().State() == random && clone.Random().State() == random && clone.Commands().ClockState().TickCount == tick && source.GenericPlayer()->ResourcesLive() && clone.GenericPlayer()->ResourcesLive() && !fixture.Runtime->ResourceBorrowsLive() && !destination_runtime.ResourceBorrowsLive(), "genuine clone uses its exact live scene snapshot despite unrelated cleared unions, retaining copied state without a source RNG draw");
		const auto source_packets = source_scene->Packets().Nodes();
		const auto clone_packets = clone_scene->Packets().Nodes();
		Require(source_packets.size() == clone_packets.size(), "clone must preserve completed packet layout before its first update");
		for (std::size_t index = 0; index != source_packets.size(); ++index) {
			const auto* a = source_packets[index].Command.Geometry;
			const auto* b = clone_packets[index].Command.Geometry;
			Require((a == nullptr) == (b == nullptr), "clone must preserve presence of completed actual geometry");
			if (!a) {
				continue;
			}
			Require(a != b && a->Positions.size() == b->Positions.size(), "clone geometry storage must be destination-owned before any continuation");
			for (std::size_t point = 0; point != a->Positions.size(); ++point) {
				Require(std::bit_cast<std::uint32_t>(a->Positions[point].X) == std::bit_cast<std::uint32_t>(b->Positions[point].X) && std::bit_cast<std::uint32_t>(a->Positions[point].Y) == std::bit_cast<std::uint32_t>(b->Positions[point].Y), "clone must preserve exact completed positions without replaying progress");
			}
		}
		const std::array<sgre::resource::MotionPoint, 1> sizes{{{4, 4}}};
		fixture.Tasks.Group(sgre::scene::MotionTaskGroup::Second).Draw(0);
		if (cross_manager) {
			destination_tasks.Group(sgre::scene::MotionTaskGroup::Second).Draw(0);
		}
		{
			const auto frame = fixture.Runtime->SceneContext().Prepare(Globals(sizes));
			Require(frame.Queue.size() == (cross_manager ? 2u : 4u), "source and newly cloned scene must publish completed actual geometry before clone update");
		}
		fixture.Runtime->SceneContext().Clear();
		if (cross_manager) {
			const auto frame = destination_runtime.SceneContext().Prepare(Globals(sizes));
			Require(frame.Queue.size() == 2, "destination manager must prepare its own clone occurrence before update");
			destination_runtime.SceneContext().Clear();
		}

		source.RequestRetire();
		fixture.Tasks.Group(sgre::scene::MotionTaskGroup::Second).Update();
		Require(!source.IsLive() && clone.IsLive() && clone.GenericPlayer()->Scene() == clone_scene, "source task must actually retire before clone continuation");
		if (cross_manager) {
			fixture.Tasks.Clear();
			fixture.Runtime->Close();
			fixture.Runtime.reset();
			Require(fixture.Manager.Unregister(fixture.Project) && destination.Contains(destination_project), "cross-manager source-first retirement must remove only the actual source registration");
		}
		const auto before = clone.Commands().ClockState().TickCount;
		clone.Progress(1);
		Require(clone.Commands().ClockState().TickCount == before + 1 && clone.GenericPlayer()->ResourcesLive() && clone.GenericPlayer()->Scene() == clone_scene, "clone must continue its independent actual clock and selected tree after source-first retirement");
		clone_tasks.Group(sgre::scene::MotionTaskGroup::Second).Draw(0);
		{
			const auto frame = clone_runtime.SceneContext().Prepare(Globals(sizes));
			Require(frame.Queue.size() == 2, "continued clone must produce its actual current packet rows");
		}
		clone_runtime.SceneContext().Clear();
		if (!cross_manager) {
			fixture.Tasks.Clear();
			fixture.Runtime->Close();
			fixture.Runtime.reset();
			Require(fixture.Manager.Unregister(fixture.Project), "same-manager clone source texture retires only after the actual clone");
		}
		destination_tasks.Clear();
		destination_runtime.Close();
		Require(destination.Unregister(destination_project) && fixture.Cache.Count() == 0 && fixture.Uploads == 2 && fixture.Releases == 2 && old_model.expired(), "both clone paths must balance real metadata, registrations and texture owners");
	}
}

int main() {
	try {
		TestFreshSceneSelectionAfterClearedSnapshotRetires();
		TestScenePreparationKeepsEveryCurrentSnapshotMember();
		TestActualSceneCloneWithUnrelatedClearedSnapshots();
		TestClearedTaskOutlivesFinalResourceUnregister();
		TestActualConstructorDefaultNormalization();
		TestActualPlainVelocityPhase();
		TestActualTimelineVariableRecords();
		TestTimelineVariablesUseActualSelectedChild();
		TestMalformedTimelineVariablesRemainRejected();
		TestTimelineVariableHiddenAndDescriptorOwners();
		TestClearRetiresSelectedTreeWithoutResettingPlayer();
		TestClearRetainsWholeTimelineScalar();
		TestActualTasksAndSharedOrdering();
		TestSceneOccurrenceCapacityIsIndependentOfPacketRows();
		TestRetainedIconMeshUsesNativeRectangle();
		TestActualMissingChildTimelineKeepsItsRealRoot();
		TestPreparationLeasesBoundLiveOwners();
		TestCurrentPreparationSurvivesSelectionHistory();
		TestDetachedEventPreparationLease();
		TestServicePairAndExactCatalog();
		TestRetainedChildRecomposesCurrentParent(false);
		TestRetainedChildRecomposesCurrentParent(true);
		std::cout << "actual Motion task nested scenes, manager-wide ordering, shared inputs and queued retirement passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
