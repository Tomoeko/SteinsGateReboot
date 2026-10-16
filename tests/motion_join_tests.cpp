#include "sgre/scene/motion_player_task.hpp"

#include "sgre/scene/motion_assets.hpp"
#include "sgre/scene/motion_random.hpp"
#include "sgre/scene/motion_scene_draw_context.hpp"
#include "sgre/scene/motion_scene_evaluation.hpp"
#include "sgre/scene/motion_scene_player.hpp"

#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
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
static void Reject(Operation operation, std::string_view message) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(message) != std::string_view::npos, error.what());
		return;
	}
	throw std::runtime_error("motion JOIN accepted an unsupported owner or phase");
}

template <typename Value>
static sgre::resource::PsbValue Atom(Value value) {
	sgre::resource::PsbValue result;
	result.Data = std::move(value);
	return result;
}

static sgre::resource::PsbValue Layer(std::string label, std::uint32_t kind, bool incoming, bool hidden, bool join_target = true) {
	const bool source_uid = label == "base" && kind == 0;
	sgre::resource::PsbArray frames;
	for (std::size_t index = 0; index != 3; ++index) {
		const auto x = incoming ? (index == 0 ? 100.0 : 16.0) : (index == 0 ? 2.0 : 6.0);
		auto content = Atom(sgre::resource::PsbObject{{"mask", Atom(std::int64_t{0x20603})}, {"src", Atom(std::string(kind == 3 ? "child" : "atlas"))}, {"icon", Atom(std::string(kind == 3 ? "show" : incoming ? "wide"
		                                                                                                                                                                                                        : "tile"))},
		    {"coord", Atom(sgre::resource::PsbArray{Atom(x), Atom(0.0), Atom(0.0)})}, {"ox", Atom(0.0)}, {"oy", Atom(0.0)}, {"opa", Atom(std::int64_t{255})}, {"color", Atom(std::int64_t{0x808080ff})}, {"bm", Atom(std::int64_t{0x10})}});
		if (kind == 3 && incoming) {
			auto& fields = std::get<sgre::resource::PsbObject>(content.Data);
			fields[0].second = Atom(std::int64_t{0xa0603});
			fields.emplace_back("motion", Atom(sgre::resource::PsbObject{{"mask", Atom(std::int64_t{10})}, {"dt", Atom(std::int64_t{1})}, {"dofst", Atom(37.0)}, {"timeOffset", Atom(3.0)}}));
		}
		const auto type = index == 2 || hidden ? std::int64_t{0} : std::int64_t{3};
		frames.push_back(Atom(sgre::resource::PsbObject{{"time", Atom(static_cast<double>(index * 4))}, {"type", Atom(type)}, {"content", content}}));
	}
	auto result = Atom(sgre::resource::PsbObject{{"label", Atom(std::move(label))}, {"type", Atom(static_cast<std::int64_t>(kind))}, {"inheritMask", Atom(std::int64_t{0})}, {"coordinate", Atom(std::int64_t{0})}, {"frameList", Atom(std::move(frames))}, {"transformOrder", Atom(sgre::resource::PsbArray{Atom(std::int64_t{0}), Atom(std::int64_t{3}), Atom(std::int64_t{2}), Atom(std::int64_t{1})})}, {"children", Atom(sgre::resource::PsbArray{})}, {"stencilType", Atom(std::int64_t{0})}, {"meshTransform", Atom(std::int64_t{0})}, {"objTriPriority", Atom(std::int64_t{0})}, {"parameterize", Atom(std::monostate{})}, {"joinTarget", Atom(join_target)}, {"groundCorrection", Atom(false)}, {"motionIndependentLayerInherit", Atom(false)}});
	if (source_uid) {
		std::get<sgre::resource::PsbObject>(result.Data).emplace_back("uid", Atom(std::string("base")));
	}
	if (kind == 1) {
		std::get<sgre::resource::PsbObject>(result.Data).emplace_back("shape", Atom(std::int64_t{2}));
	}
	if (kind == 12) {
		std::get<sgre::resource::PsbObject>(result.Data).emplace_back("stencilCompositeMaskLayerList", Atom(sgre::resource::PsbArray{}));
	}
	return result;
}

static sgre::resource::PsbValue Timeline(bool incoming, bool outgoing_hidden, bool incoming_hidden, bool include_child, bool join_target, bool changed_path, bool hidden_child = false) {
	auto mask = Layer("wipe", 12, incoming, incoming ? incoming_hidden : outgoing_hidden, join_target);
	for (auto& [name, value] : std::get<sgre::resource::PsbObject>(mask.Data)) {
		if (name == "children") {
			value = Atom(sgre::resource::PsbArray{Layer("border", 0, incoming, incoming ? incoming_hidden : outgoing_hidden, join_target)});
		}
	}
	sgre::resource::PsbArray layers{Layer(incoming && changed_path ? "other" : "base", 0, incoming, incoming ? incoming_hidden : outgoing_hidden, join_target), std::move(mask), Layer("textarea", 1, incoming, false, true)};
	if (include_child) {
		layers.push_back(Layer("child-player", 3, incoming, hidden_child, true));
	}
	auto priority = Atom(sgre::resource::PsbObject{{"time", Atom(0.0)}, {"type", Atom(std::int64_t{1})}, {"content", Atom(sgre::resource::PsbArray{Atom(std::int64_t{0}), Atom(std::int64_t{1}), Atom(std::int64_t{2}), Atom(std::int64_t{3})})}});
	if (include_child) {
		std::get<sgre::resource::PsbArray>(std::get<sgre::resource::PsbObject>(priority.Data)[2].second.Data).push_back(Atom(std::int64_t{4}));
	}
	const auto terminal = Atom(sgre::resource::PsbObject{{"time", Atom(8.0)}, {"type", Atom(std::int64_t{1})}, {"content", Atom(std::monostate{})}});
	auto result = Atom(sgre::resource::PsbObject{{"type", Atom(std::int64_t{0})}, {"bounds", Atom(sgre::resource::PsbObject{{"left", Atom(0.0)}, {"top", Atom(0.0)}, {"right", Atom(32.0)}, {"bottom", Atom(16.0)}})}, {"lastTime", Atom(8.0)}, {"loopTime", Atom(-1.0)}, {"layer", Atom(std::move(layers))}, {"priority", Atom(sgre::resource::PsbArray{priority, terminal})}, {"tag", Atom(sgre::resource::PsbArray{})}, {"parameter", Atom(std::monostate{})}, {"variable", Atom(std::monostate{})}, {"parameterize", Atom(std::monostate{})}, {"layerIndexMap", Atom(sgre::resource::PsbObject{{"base", Atom(std::int64_t{0})}, {"wipe", Atom(std::int64_t{1})}, {"border", Atom(std::int64_t{2})}, {"textarea", Atom(std::int64_t{3})}})}});
	if (include_child) {
		auto& fields = std::get<sgre::resource::PsbObject>(result.Data);
		std::get<sgre::resource::PsbObject>(fields.back().second.Data).emplace_back("child-player", Atom(std::int64_t{4}));
	}
	return result;
}

static std::shared_ptr<sgre::resource::PsbDocument> Document(bool outgoing_hidden, bool incoming_hidden, bool include_child, bool join_target, bool changed_path, bool empty_child = false, bool unmatched_child = false) {
	sgre::resource::PsbChunkReference chunk;
	auto texture = Atom(sgre::resource::PsbObject{{"type", Atom(std::string("A8"))}, {"width", Atom(std::int64_t{4})}, {"height", Atom(std::int64_t{4})}, {"truncated_width", Atom(std::int64_t{4})}, {"truncated_height", Atom(std::int64_t{4})}, {"pixel", Atom(chunk)}});
	texture.SerializedOffset = 64;
	const auto icon = [](double width) {
		return Atom(sgre::resource::PsbObject{{"left", Atom(0.0)}, {"top", Atom(0.0)}, {"width", Atom(width)}, {"height", Atom(2.0)}, {"originX", Atom(0.0)}, {"originY", Atom(0.0)}, {"attr", Atom(std::int64_t{2})}});
	};
	const auto source = Atom(sgre::resource::PsbObject{{"texture", texture}, {"icon", Atom(sgre::resource::PsbObject{{"tile", icon(2)}, {"wide", icon(3)}})}});
	const auto object = Atom(sgre::resource::PsbObject{{"type", Atom(std::int64_t{0})}, {"motion", Atom(sgre::resource::PsbObject{{"show", Timeline(false, outgoing_hidden, incoming_hidden, include_child, true, false, empty_child)}, {"hide", Timeline(true, outgoing_hidden, incoming_hidden, include_child && !unmatched_child, join_target, changed_path)}})}});
	const auto child = Atom(sgre::resource::PsbObject{{"type", Atom(std::int64_t{0})}, {"motion", Atom(sgre::resource::PsbObject{{"show", Timeline(false, false, false, false, false, false)}})}});
	auto document = std::make_shared<sgre::resource::PsbDocument>();
	document->Header.Version = 4;
	document->Header.RootOffset = 44;
	document->Header.DataOffset = 128;
	document->SerializedSize = 144;
	document->Chunks = {{128, 16}};
	document->Root = Atom(sgre::resource::PsbObject{{"id", Atom(std::string("motion"))}, {"spec", Atom(std::string("win"))}, {"version", Atom(3.03)}, {"label", Atom(std::string("Independent numeric JOIN fixture"))}, {"easing", Atom(sgre::resource::PsbArray{})}, {"source", Atom(sgre::resource::PsbObject{{"atlas", source}})}, {"object", Atom(sgre::resource::PsbObject{{"character", object}, {"child", child}})}, {"sourceIconRenameMap", Atom(sgre::resource::PsbObject{})}});
	document->Root.SerializedOffset = 44;
	return document;
}

struct Fixture {
	std::array<std::uint32_t, sgre::scene::kMotionSharedSeedWords> Seed{};
	std::size_t TextureOwner = 0;
	sgre::scene::TextureCache Cache;
	sgre::scene::MotionAssetCatalog Catalog;
	std::shared_ptr<const sgre::scene::LoadedMotionAsset> Asset;
	sgre::scene::MotionResourceManager Manager;
	sgre::scene::MotionProjectId Project;
	sgre::core::FrameClock Clock;
	std::unique_ptr<sgre::scene::MotionManagerRuntime> Runtime;
	sgre::scene::MotionLayerTasks Tasks;

	explicit Fixture(bool outgoing_hidden = false, bool incoming_hidden = false, bool include_child = false, bool join_target = true, bool changed_path = false, bool empty_child = false, bool unmatched_child = false, std::shared_ptr<const sgre::resource::PsbDocument> supplied_document = {})
	    : Cache([this](auto, const auto&) {
		      return sgre::render::SpriteTextureHandle{1, 1, &TextureOwner};
	      },
	          [](auto) {
	          }),
	      Catalog(Cache), Manager(Cache), Clock(0), Tasks(8) {
		Seed.fill(17);
		Asset = Catalog.Prepare(supplied_document ? std::move(supplied_document) : Document(outgoing_hidden, incoming_hidden, include_child, join_target, changed_path, empty_child, unmatched_child), std::make_shared<const std::vector<std::uint8_t>>(144, 255));
		Project = Manager.Register(Asset->Identity, Asset->Model, *Asset->Backing);
		sgre::scene::MotionManagerRuntimeServices services;
		services.ReadSourceAliases = [this](auto) {
			return std::span<const sgre::scene::MotionSourceAlias>(Asset->Aliases);
		};
		services.SceneAssets = &Catalog;
		services.ReadRegisteredAsset = [this](auto project) -> const sgre::scene::LoadedMotionAsset& {
			Require(project == Project, "JOIN borrowed a foreign actual project");
			return *Asset;
		};
		Runtime = std::make_unique<sgre::scene::MotionManagerRuntime>(Manager, Clock, Seed, services, 64);
	}

	~Fixture() {
		Runtime->SceneContext().Clear();
		Tasks.Clear();
	}

	sgre::scene::MotionPlayerTask Attach() {
		return sgre::scene::MotionPlayerTask::Attach(*Runtime, Tasks, false, "character", [](const auto&) {
			throw std::runtime_error("action-free JOIN fixture delivered an event");
		});
	}
};

static void TestNumericJoin(bool outgoing_hidden, bool incoming_hidden, bool join_target, bool changed_path) {
	Fixture fixture(outgoing_hidden, incoming_hidden, false, join_target, changed_path);
	auto task = fixture.Attach();
	task.SetLeft(7);
	task.SetVisible(true);
	task.Play("show");
	task.Progress(0);
	task.Progress(2);
	const auto* old_scene = task.GenericPlayer()->Scene();
	const auto old_ref = old_scene->FindLayer("base");
	Require(old_ref.has_value() && !task.Commands().ClockState().TickAssigned && task.Commands().ClockState().EvaluationTime == 2, "JOIN fixture requires an actual elapsed outgoing clock");
	const auto old_serial = task.Commands().SelectionSerial();
	task.Play("hide", sgre::scene::MotionPlayFlags::Join);
	Require(task.Commands().SelectionSerial() != old_serial && task.Commands().Names().CurrentMotion == "hide", "JOIN must perform the genuine incoming selection");
	task.Progress(0);
	const auto* scene = task.GenericPlayer()->Scene();
	const auto base = scene->FindLayer(changed_path ? "other" : "base");
	// A changed-path fixture intentionally leaves the old LayerIndexMap key unresolved; use its real packet owner instead.
	float expected_local_x = 4;
	if (incoming_hidden) {
		expected_local_x = 0;
	} else if (outgoing_hidden || !join_target || changed_path) {
		expected_local_x = 100;
	}
	const float expected_x = 7 + expected_local_x;
	if (!changed_path) {
		const float actual_x = base ? scene->LayerCoordinate(*base)[0] : -99999;
		Require(base && actual_x == expected_x, "JOIN local restore: expected " + std::to_string(expected_x) + ", actual " + std::to_string(actual_x) + ", outgoing hidden " + std::to_string(outgoing_hidden) + ", incoming hidden " + std::to_string(incoming_hidden) + ", joinTarget " + std::to_string(join_target));
	}
	for (const auto* row : scene->PacketRows()) {
		const auto state = scene->DrawState(*row);
		Require(incoming_hidden || (state.Source && state.Source->State().Icon == "wide"), "JOIN must retain the incoming source/icon instead of outgoing raw source pointers");
		if (row->Node().Raw->Label == "base" || row->Node().Raw->Label == "other") {
			Require(state.Numeric->Hierarchy.Attributes.Coordinate[0] == expected_x, "changed JOIN path or flags must not reuse an unrelated outgoing slot");
		}
	}
	const auto shape = scene->FindLayer("textarea");
	Require(shape && scene->LayerCoordinate(*shape)[0] == 107, "native JOIN excludes type1 even when joinTarget is true");
	if (!outgoing_hidden && !incoming_hidden && join_target && !changed_path) {
		task.Progress(2);
		const auto after = task.GenericPlayer()->Scene();
		Require(after->LayerCoordinate(*after->FindLayer("base"))[0] == 17, "JOIN must tween captured local4 toward incoming following16 without replacing timing");
		task.Progress(2);
		Require(after->LayerCoordinate(*after->FindLayer("base"))[0] == 23, "the next actual physical frame must decode its incoming value");
		task.SetTickCount(0);
		task.Progress(0);
		Require(after->LayerCoordinate(*after->FindLayer("base"))[0] == 107, "a real seek must expire the consumed JOIN decoded-slot override");
	}
	Reject([&] {
		static_cast<void>(scene->LayerCoordinate(*old_ref));
	},
	    "retired actual root");
}

static void TestExternalRestoreKeepsImmutableSourceNames() {
	Fixture fixture;
	std::uint32_t physical_source = 0;
	sgre::scene::NativeTextureSnapshot description;
	description.Texture = {71, 1, &physical_source};
	description.Size = {8, 4};
	description.ActiveBounds = {0, 0, 8, 4};
	sgre::scene::NativeTextureReference texture([&] {
		return description;
	},
	    [](auto, auto) {
	    });
	auto task = fixture.Attach();
	struct Cleanup {
		Fixture& Owners;
		sgre::scene::MotionPlayerTask& Task;
		~Cleanup() noexcept {
			try {
				Owners.Runtime->SceneContext().Clear();
				if (Task.IsLive()) {
					Task.ClearAlternateTextures();
				}
			} catch (...) {
				std::terminate();
			}
		}
	} cleanup{fixture, task};
	task.RegisterAlternateTexture("base", texture, {0, 0, 8, 4}, {0, 0});
	task.SetVisible(true);
	task.Play("show");
	task.Progress(0);
	task.Progress(2);
	task.Play("hide", sgre::scene::MotionPlayFlags::Join);
	task.Progress(0);
	const auto* scene = task.GenericPlayer()->Scene();
	const sgre::scene::MotionStaticSourceBinding* binding = nullptr;
	const sgre::scene::PreparedMotionFrame* incoming = nullptr;
	for (const auto* row : scene->PacketRows()) {
		if (row->Node().Raw->Label == "base") {
			binding = scene->DrawState(*row).Source;
			incoming = &row->Node().Prepared->Frames[0];
		}
	}
	Require(binding && incoming && binding->State().External && binding->State().External->Installed, "JOIN fixture must retain an actual installed UID texture through incoming local selection");
	Require(task.UnregisterAlternateTexture("base"), "actual source listener must unregister while the joined slot is current");
	const auto& restored = binding->State();
	Require(restored.Ready && restored.Binding && restored.Icon == "wide" && restored.Source == "atlas", "actual external restoration must resolve the incoming authored source");
	Require(restored.Source.data() == incoming->Attributes.Source.data() && restored.Icon.data() == incoming->Attributes.Icon.data(), "source cache names must borrow the immutable incoming owner, never the expiring joined numeric slot");
	task.Progress(4);
	Require(binding->State().Ready && binding->State().Source == "atlas" && binding->State().Icon == "wide", "decoding over the joined physical slot must preserve live cached source names");
	task.SetTickCount(0);
	task.Progress(0);
	Require(binding->State().Source.data() == incoming->Attributes.Source.data() && binding->State().Icon.data() == incoming->Attributes.Icon.data(), "seek and unchanged-name suppression must retain immutable source storage");
	texture.RequireCanRelease();
}

static void TestRealQueuedGuardAndTickSkip() {
	Fixture fixture(false, false, true);
	auto task = fixture.Attach();
	task.SetVisible(true);
	task.Play("show");
	task.Progress(0);
	const auto serial = task.Commands().SelectionSerial();
	task.Play("hide", sgre::scene::MotionPlayFlags::Join);
	task.Progress(0);
	Require(task.Commands().SelectionSerial() == serial + 1 && task.Commands().Names().CurrentMotion == "hide", "actual owning child JOIN must install the next incoming root");
	// Native51c0a0 exits on TickAssigned before inspecting any outgoing node.
	task.SetTickCount(1);
	task.Play("hide", sgre::scene::MotionPlayFlags::Force | sgre::scene::MotionPlayFlags::Join);
	task.Progress(0);
	const auto* scene = task.GenericPlayer()->Scene();
	Require(scene->LayerCoordinate(*scene->FindLayer("base"))[0] == 100, "explicit tick assignment must suppress capture, not incoming selection");
	fixture.Tasks.Group(sgre::scene::MotionTaskGroup::Second).Draw(0);
	const auto queued_serial = task.Commands().SelectionSerial();
	Reject([&] {
		task.Play("show", sgre::scene::MotionPlayFlags::Join);
	},
	    "queued");
	Require(task.Commands().SelectionSerial() == queued_serial, "actual queued rows must remain immutable across rejected JOIN");
	fixture.Runtime->SceneContext().Clear();
}

struct ChildUidObserver final : sgre::scene::MotionLayerUidObserver {
	const sgre::scene::MotionPlayerCommands* Root = nullptr;
	std::size_t Added = 0;
	std::size_t Removed = 0;

	void LayerAdded(std::string_view, const sgre::scene::MotionLayerUidMatch& match) noexcept override {
		if (&match.Layer.Player() != Root) {
			++Added;
		}
	}

	void LayerRemoved(std::string_view, const sgre::scene::MotionLayerUidMatch& match) noexcept override {
		if (&match.Layer.Player() != Root) {
			++Removed;
		}
	}
};

static void TestRealOwningChildJoin(bool repeated_initial_selection) {
	Fixture fixture(false, false, true);
	auto task = fixture.Attach();
	ChildUidObserver observer;
	observer.Root = &task.Commands();
	auto& uids = task.Commands().LayerUids();
	uids.Subscribe("base", observer);
	try {
		task.SetVisible(true);
		task.Play("show");
		task.Progress(0);
		task.Progress(2);
		const auto* scene = task.GenericPlayer()->Scene();
		const auto child_reference = scene->FindLayer("child-player");
		Require(child_reference.has_value(), "actual containing LayerIndexMap must resolve the true kind3 player");
		const auto* child = &scene->LayerChild(*child_reference);
		const auto* child_commands = &child->Commands();
		const auto* child_root = &child->Root();
		const auto random = child->Random().State();
		const auto child_serial = child_commands->SelectionSerial();
		const auto child_tick = child_commands->ClockState().TickCount;
		task.SetVariable("child-player::drive", 5.0f);
		Require(child_commands->ParameterValues().Read("drive") == 5, "outgoing descriptor must target this exact actual child membership before transfer");
		Require(observer.Added == 1 && observer.Removed == 0 && !child->Control(0).Specified && !child->Control(1).Specified, "outgoing actual child owns one registered subtree and zero default parent controls");
		std::uint32_t identifier = 0;
		for (const auto& match : uids.Matches("base")) {
			if (&match.Layer.Player() == child_commands) {
				identifier = match.Layer.Node().Identifier;
			}
		}
		Require(identifier != 0, "actual child UID owner must exist before JOIN");
		task.Play("hide", sgre::scene::MotionPlayFlags::Join);
		if (repeated_initial_selection) {
			Require(task.Commands().ClockState().TickAssigned, "second initial selection requires the actual native capture bypass");
			task.Play("hide", sgre::scene::MotionPlayFlags::Force | sgre::scene::MotionPlayFlags::Join);
		}
		Require(task.GenericPlayer()->Scene() == scene, "actual evaluator implementation must enclose outgoing root destruction and retained child phases");
		task.Progress(0);
		const auto incoming_reference = scene->FindLayer("child-player");
		Require(incoming_reference.has_value(), "incoming true child name must resolve after owning JOIN");
		const auto& retained = scene->LayerChild(*incoming_reference);
		Require(&retained == child && &retained.Commands() == child_commands && &retained.Root() == child_root && retained.Random().State() == random && retained.Commands().SelectionSerial() == child_serial && retained.Commands().ClockState().TickCount == child_tick, "JOIN must move actual child/commands/root/random/selection and clock ownership, not clone a pose");
		Require(retained.Control(0).Specified && retained.Control(1).Specified && retained.Control(0).DirectionMode == 1 && retained.Control(1).DirectionMode == 1 && retained.Control(0).DirectionOffset == 37 && retained.Control(1).DirectionOffset == 37 && retained.Control(0).TimeOffset == 3 && retained.Control(1).TimeOffset == 3, "retained player must use both newly prepared incoming node controls");
		Require(observer.Added == 1 && observer.Removed == 0, "retained actual child UID subtree must not be destroyed or re-created with the outgoing root");
		Require(retained.Commands().ParameterValues().Read("drive") == 5, "final native descriptor rebind must address the retained child rather than the discarded incoming constructor");
		task.SetVariable("child-player/drive", 9.0f);
		Require(retained.Commands().ParameterValues().Read("drive") == 9 && task.GetVariable("child-player::drive") == 9, "a later real descriptor write must retain canonical membership without a dangling fresh-player target");
		bool same_identifier = false;
		for (const auto& match : uids.Matches("base")) {
			if (&match.Layer.Player() == child_commands) {
				same_identifier = match.Layer.Node().Identifier == identifier;
			}
		}
		Require(same_identifier && scene->Packets().Nodes().size() == 7, "real child row/geometry publication must retain its old UID while rebinding the new containing rows");
		sgre::scene::MotionSharedRandom expected(fixture.Seed);
		for (std::size_t sample = 0; sample < (repeated_initial_selection ? 4U : 3U); ++sample) {
			static_cast<void>(expected.NextUnitFloat());
		}
		Require(fixture.Runtime->NextUnitFloat() == expected.NextUnitFloat(), "the discarded incoming child must still consume exactly its genuine constructor manager sample");
		task.Play("show", sgre::scene::MotionPlayFlags::Force);
		Require(observer.Removed == 1, "ordinary root replacement must retire the retained actual child exactly once");
		task.Progress(0);
		Require(observer.Added == 2, "new ordinary selection must construct its own real child UID subtree");
		uids.Unsubscribe("base", observer);
	} catch (...) {
		uids.Unsubscribe("base", observer);
		throw;
	}
	fixture.Tasks.Clear();
	Require(!task.IsLive() && fixture.Runtime->Identifiers().LiveCount() == 0, "owning JOIN fixture must retire all actual parent and retained-child identifiers");
}

static void TestActualEmptyChildJoin() {
	Fixture fixture(false, false, true, true, false, true);
	auto task = fixture.Attach();
	task.SetVisible(true);
	task.Play("show");
	task.Progress(0);
	task.Progress(2);
	const auto* scene = task.GenericPlayer()->Scene();
	const auto reference = scene->FindLayer("child-player");
	Require(reference.has_value(), "hidden outgoing child still owns its actual containing node");
	const auto* child = &scene->LayerChild(*reference);
	const auto random = child->Random().State();
	Require(child->Commands().Names().CurrentMotion.empty() && child->Commands().Names().RequestedMotion.empty() && child->Commands().SelectionSerial() == 0, "hidden actual child has a genuine unselected root rather than fabricated geometry");
	task.Play("hide", sgre::scene::MotionPlayFlags::Join);
	task.Progress(0);
	const auto retained_reference = scene->FindLayer("child-player");
	Require(retained_reference.has_value(), "incoming child node must remain a real selected catalogue entry");
	const auto& retained = scene->LayerChild(*retained_reference);
	Require(&retained == child && retained.Random().State() == random && retained.Commands().SelectionSerial() == 1 && retained.Commands().Names().CurrentMotion == "show" && retained.Commands().ClockState().TickCount == 3, "JOIN moves even a hidden empty player, then its genuine incoming control selects and times the actual child timeline");
	Require(scene->Packets().Nodes().size() == 7, "previously empty retained child must publish its newly selected real rows");
	fixture.Tasks.Clear();
	Require(!task.IsLive() && fixture.Runtime->Identifiers().LiveCount() == 0, "empty-player JOIN must retire all subsequently selected actual child nodes");
}

static void TestUnmatchedChildSnapshotRetirement() {
	Fixture fixture(false, false, true, true, false, false, true);
	auto task = fixture.Attach();
	ChildUidObserver observer;
	observer.Root = &task.Commands();
	auto& uids = task.Commands().LayerUids();
	uids.Subscribe("base", observer);
	try {
		task.SetVisible(true);
		task.Play("show");
		task.Progress(0);
		task.Progress(2);
		Require(observer.Added == 1 && observer.Removed == 0, "unmatched fixture needs a real outgoing selected child subtree");
		task.SetVariable("child-player::drive", 7.0f);
		task.Play("hide", sgre::scene::MotionPlayFlags::Join);
		task.Progress(0);
		const auto* scene = task.GenericPlayer()->Scene();
		Require(!scene->FindLayer("child-player") && observer.Removed == 1 && scene->Packets().Nodes().size() == 3, "unmatched JOIN capsule must retire its actual child subtree once and publish only genuine incoming rows");
		task.SetVariable("child-player::drive", 9.0f);
		Require(task.GetVariable("child-player::drive") == 9, "final descriptor rebind must clear unmatched child targets while retaining its real controller value");
		uids.Unsubscribe("base", observer);
	} catch (...) {
		uids.Unsubscribe("base", observer);
		throw;
	}
	fixture.Tasks.Clear();
	Require(fixture.Runtime->Identifiers().LiveCount() == 0, "unmatched capsule retirement must return every actual node identity");
}

static sgre::resource::PsbValue& NestedField(sgre::resource::PsbValue& object, std::string_view name) {
	for (auto& [key, value] : std::get<sgre::resource::PsbObject>(object.Data)) {
		if (key == name) {
			return value;
		}
	}
	throw std::runtime_error("nested JOIN fixture requires its declared metadata field");
}

static std::shared_ptr<sgre::resource::PsbDocument> NestedDocument(bool owning_grandchild, bool reset = false) {
	auto document = Document(false, false, owning_grandchild, true, false);
	auto& characters = std::get<sgre::resource::PsbObject>(NestedField(document->Root, "object").Data);
	for (auto& [name, value] : characters) {
		if (name == "character") {
			name = "nested";
		}
	}
	auto wrapper = Timeline(false, false, false, false, false, false);
	auto container = Layer("container", 3, false, false, false);
	auto& frames = std::get<sgre::resource::PsbArray>(NestedField(container, "frameList").Data);
	for (std::size_t index = 0; index < 2; ++index) {
		auto& content = NestedField(frames[index], "content");
		NestedField(content, "src") = Atom(std::string("nested"));
		NestedField(content, "icon") = Atom(std::string(index == 0 ? "show" : "hide"));
		NestedField(content, "mask") = Atom(std::int64_t{0xa0603});
		NestedField(content, "coord") = Atom(sgre::resource::PsbArray{Atom(2.0), Atom(0.0), Atom(0.0)});
		std::get<sgre::resource::PsbObject>(content.Data).emplace_back("motion", Atom(sgre::resource::PsbObject{{"mask", Atom(std::int64_t{1})}, {"flags", Atom(std::int64_t{8})}, {"timeOffset", Atom(0.0)}}));
	}
	if (reset) {
		auto incoming = frames[1];
		NestedField(incoming, "time") = Atom(6.0);
		NestedField(NestedField(frames[1], "content"), "src") = Atom(std::string{});
		NestedField(NestedField(frames[1], "content"), "icon") = Atom(std::string{});
		frames.insert(frames.begin() + 2, std::move(incoming));
	}
	NestedField(wrapper, "layer") = Atom(sgre::resource::PsbArray{container});
	NestedField(wrapper, "layerIndexMap") = Atom(sgre::resource::PsbObject{{"container", Atom(std::int64_t{0})}});
	NestedField(std::get<sgre::resource::PsbArray>(NestedField(wrapper, "priority").Data).front(), "content") = Atom(sgre::resource::PsbArray{Atom(std::int64_t{0})});
	characters.emplace_back("character", Atom(sgre::resource::PsbObject{{"type", Atom(std::int64_t{0})}, {"motion", Atom(sgre::resource::PsbObject{{"main", wrapper}})}}));
	return document;
}

static void TestNestedPlayerJoin(bool owning_grandchild) {
	Fixture fixture(false, false, false, true, false, false, false, NestedDocument(owning_grandchild));
	auto task = fixture.Attach();
	task.SetVisible(true);
	task.Play("main");
	task.Progress(0);
	task.Progress(1);
	const auto* scene = task.GenericPlayer()->Scene();
	const auto container = scene->FindLayer("container");
	const auto base = scene->FindLayer("base");
	Require(container && base, "nested JOIN requires the genuine containing player and outgoing source layer");
	const auto nested = scene->LayerChild(*container).Borrow();
	const auto& previous = task.ChildCommands(nested);
	Require(previous.Names().CurrentMotion == "show" && previous.ClockState().TickCount == 1, "parent frame must have genuinely selected/ticked the outgoing nested player");
	float previous_x = 0;
	for (const auto* row : scene->PacketRows()) {
		if (row->Node().Raw->Label == "base") {
			const auto state = scene->DrawState(*row);
			previous_x = state.Numeric->Hierarchy.Attributes.Coordinate[0];
			break;
		}
	}
	std::optional<sgre::scene::MotionChildBorrow> grandchild;
	float grandchild_tick = 0;
	if (owning_grandchild) {
		const auto reference = scene->FindLayer("child-player");
		Require(reference.has_value(), "owning nested JOIN requires its actual selected grandchild");
		grandchild = scene->LayerChild(*reference).Borrow();
		grandchild_tick = task.ChildCommands(*grandchild).ClockState().TickCount;
	}
	fixture.Tasks.Group(sgre::scene::MotionTaskGroup::Second).Draw(0);
	Reject([&] {
		task.Progress(3);
	},
	    "queued");
	fixture.Runtime->SceneContext().Clear();

	// Parent521BF0 calls the real child Play(flags8); no direct child Play or prebuilt snapshot is supplied.
	task.Progress(3);
	Require(nested.IsLive() && task.ChildCommands(nested).Names().CurrentMotion == "hide" && task.ChildCommands(nested).ClockState().TickCount == 0 && task.ChildCommands(nested).SelectionSerial() == 2, "nested player's own JOIN map must survive its outgoing Context retirement and new selected construction");
	float incoming_x = 0;
	for (const auto* row : scene->PacketRows()) {
		if (row->Node().Raw->Label == "base") {
			incoming_x = scene->DrawState(*row).Numeric->Hierarchy.Attributes.Coordinate[0];
			break;
		}
	}
	Require(incoming_x == previous_x, "nested JOIN must restore outgoing actual numeric values instead of taking incoming x100");
	if (owning_grandchild) {
		const auto reference = scene->FindLayer("child-player");
		Require(reference && scene->LayerChild(*reference).Borrow().SameTarget(*grandchild) && task.ChildCommands(*grandchild).ClockState().TickCount == grandchild_tick + 3, "nested owning JOIN must retain the exact grandchild player/clock while discarding the genuinely constructed incoming player");
	}
	task.Progress(1);
	Require(task.ChildCommands(nested).ClockState().TickCount == 1, "nested JOIN must consume its pending map once and continue its real new clock");
	fixture.Tasks.Group(sgre::scene::MotionTaskGroup::Second).Draw(0);
	Require(fixture.Runtime->SceneContext().Size() == 1, "nested JOIN must publish through the unchanged actual root task/manager draw owner");
	fixture.Runtime->SceneContext().Clear();
	task.SetTickCount(0);
	task.Progress(0);
	Require(task.ChildCommands(nested).Names().CurrentMotion == "show", "reverse physical crossing must capture a new genuine child JOIN map");
	fixture.Tasks.Clear();
	Require(!nested.IsLive() && (!owning_grandchild || !grandchild->IsLive()) && fixture.Runtime->Identifiers().LiveCount() == 0, "nested JOIN callbacks/maps/phases must retire before all real child owners and IDs");
}

static void TestCompletedEmptyTimelineJoin() {
	auto document = Document(false, false, true, true, false);
	auto& child = NestedField(NestedField(document->Root, "object"), "child");
	auto& timeline = NestedField(NestedField(child, "motion"), "show");
	NestedField(timeline, "layer") = Atom(sgre::resource::PsbArray{});
	NestedField(timeline, "layerIndexMap") = Atom(sgre::resource::PsbObject{});
	auto& priority = std::get<sgre::resource::PsbArray>(NestedField(timeline, "priority").Data);
	NestedField(priority.front(), "content") = Atom(sgre::resource::PsbArray{});
	Fixture fixture(false, false, false, true, false, false, false, std::move(document));
	auto task = fixture.Attach();
	task.SetVisible(true);
	task.Play("show");
	task.Progress(0);
	task.Progress(1);
	const auto* scene = task.GenericPlayer()->Scene();
	const auto reference = scene->FindLayer("child-player");
	Require(reference.has_value(), "zero-layer selected timeline still has its real containing player node");
	const auto child_borrow = scene->LayerChild(*reference).Borrow();
	const auto serial = task.ChildCommands(child_borrow).SelectionSerial();
	Require(serial == 1 && task.ChildCommands(child_borrow).Names().CurrentMotion == "show", "empty selected timeline must retain its completed actual selection, distinct from a reset facade");

	task.Play("hide", sgre::scene::MotionPlayFlags::Join);
	task.Progress(0);
	const auto retained = scene->FindLayer("child-player");
	Require(retained && scene->LayerChild(*retained).Borrow().SameTarget(child_borrow) && task.ChildCommands(child_borrow).SelectionSerial() == serial, "JOIN must preserve the completed zero-layer Context and its exact current player selection");
	fixture.Tasks.Clear();
	Require(!child_borrow.IsLive() && fixture.Runtime->Identifiers().LiveCount() == 0, "completed zero-layer JOIN must retire without a stale Context borrow");
}

static std::shared_ptr<sgre::resource::PsbDocument> RetiredEmptyGrandchildDocument() {
	auto document = NestedDocument(true);
	auto& characters = NestedField(document->Root, "object");
	auto& wrapper = NestedField(NestedField(NestedField(characters, "character"), "motion"), "main");
	auto& container = std::get<sgre::resource::PsbArray>(NestedField(wrapper, "layer").Data).front();
	auto& parent_frames = std::get<sgre::resource::PsbArray>(NestedField(container, "frameList").Data);
	NestedField(parent_frames[1], "time") = Atom(6.0);
	auto& outgoing = NestedField(NestedField(NestedField(characters, "nested"), "motion"), "show");
	for (auto& layer : std::get<sgre::resource::PsbArray>(NestedField(outgoing, "layer").Data)) {
		if (std::get<std::string>(NestedField(layer, "label").Data) != "child-player") {
			continue;
		}
		auto& frames = std::get<sgre::resource::PsbArray>(NestedField(layer, "frameList").Data);
		auto& content = NestedField(frames[1], "content");
		NestedField(content, "src") = Atom(std::string{});
		NestedField(content, "icon") = Atom(std::string{});
	}
	return document;
}

static void TestRetiredEmptyGrandchildJoin() {
	Fixture fixture(false, false, false, true, false, false, false, RetiredEmptyGrandchildDocument());
	auto task = fixture.Attach();
	task.SetVisible(true);
	task.Play("main");
	task.Progress(0);
	const auto* scene = task.GenericPlayer()->Scene();
	const auto reference = scene->FindLayer("child-player");
	Require(reference.has_value(), "retired-facade history requires a genuinely selected outgoing grandchild");
	const auto grandchild = scene->LayerChild(*reference).Borrow();
	const auto previous_serial = task.ChildCommands(grandchild).SelectionSerial();
	Require(previous_serial == 1 && task.ChildCommands(grandchild).Names().CurrentMotion == "show", "grandchild must first own its actual selected subtree");

	// The real empty physical control retires only that subtree, leaving its player and retired C++ facade.
	task.Progress(4);
	Require(grandchild.IsLive() && task.ChildCommands(grandchild).Names().CurrentMotion.empty() && task.ChildCommands(grandchild).Names().RequestedMotion.empty() && !task.ChildCommands(grandchild).ClockState().Playing, "empty child control must retain the live root-only capsule after actual selected-node retirement");

	// The containing nested player now changes timeline with JOIN and transfers that root-only capsule.
	task.Progress(2);
	const auto retained = scene->FindLayer("child-player");
	Require(retained && scene->LayerChild(*retained).Borrow().SameTarget(grandchild), "JOIN must move the exact reset grandchild rather than discard or recreate its player");
	Require(task.ChildCommands(grandchild).Names().CurrentMotion == "show" && task.ChildCommands(grandchild).SelectionSerial() == previous_serial + 1 && scene->GlobalRows().size() > 0, "incoming actual control must select and publish new descendants on the retained root-only player");
	fixture.Tasks.Group(sgre::scene::MotionTaskGroup::Second).Draw(0);
	fixture.Runtime->SceneContext().Clear();
	task.Progress(1);
	fixture.Tasks.Clear();
	Require(!grandchild.IsLive() && fixture.Runtime->Identifiers().LiveCount() == 0, "reset-capsule JOIN must release the real player, fresh descendants, callbacks and all IDs exactly once");
}

static void TestNestedResetThenJoin() {
	Fixture fixture(false, false, false, true, false, false, false, NestedDocument(false, true));
	auto task = fixture.Attach();
	task.SetVisible(true);
	task.Play("main");
	task.Progress(0);
	task.Progress(1);
	const auto* scene = task.GenericPlayer()->Scene();
	const auto reference = scene->FindLayer("container");
	Require(reference.has_value(), "reset history requires the exact actual containing child");
	const auto child = scene->LayerChild(*reference).Borrow();
	task.Progress(3);
	Require(child.IsLive() && task.ChildCommands(child).Names().CurrentMotion.empty() && !task.ChildCommands(child).ClockState().Playing, "native reset must retain the same actual player while retiring its selected rows");
	task.Progress(2);
	Require(task.ChildCommands(child).Names().CurrentMotion == "hide" && task.ChildCommands(child).ClockState().Playing && scene->GlobalRows().size() > 0, "JOIN after a real zero-node reset must capture no invented rows and select the genuine incoming timeline");
	fixture.Tasks.Clear();
	Require(!child.IsLive() && fixture.Runtime->Identifiers().LiveCount() == 0, "reset/re-entry must detach actual callbacks and release all selected IDs");
}

static void TestCopiedSelectedChildJoin() {
	Fixture fixture(false, false, true);
	auto source = fixture.Attach();
	source.SetVisible(true);
	source.Play("show");
	source.Progress(0);
	source.Progress(2);
	auto clone = sgre::scene::MotionPlayerTask::AttachClone(*fixture.Runtime, fixture.Tasks, source, false, [](const auto&) {
		throw std::runtime_error("action-free copied child JOIN fixture delivered an event");
	});
	const auto* scene = clone.GenericPlayer()->Scene();
	const auto reference = scene->FindLayer("child-player");
	Require(reference.has_value(), "copied JOIN fixture requires its real selected child before continuation");
	const auto child = scene->LayerChild(*reference).Borrow();
	const auto child_serial = clone.ChildCommands(child).SelectionSerial();
	const auto* child_timeline = clone.ChildCommands(child).CachedTimeline();
	Require(clone.ChildCommands(child).Names().CurrentMotion.empty() && clone.ChildCommands(child).Names().RequestedMotion == "show", "native copy must retain the selected child atom and requested name while leaving CurrentMotion empty");
	{
		auto retired = fixture.Tasks.Group(sgre::scene::MotionTaskGroup::Second).Extract(source.Handle());
		retired.Release();
	}
	Require(!source.IsLive() && child.IsLive(), "source-first retirement must leave the independent copied selected child alive");
	clone.Progress(0);
	clone.Progress(1);
	Require(!clone.Commands().ClockState().TickAssigned, "genuine elapsed continuation must admit the native JOIN capture path");
	clone.Play("hide", sgre::scene::MotionPlayFlags::Join);
	clone.Progress(0);
	const auto incoming = scene->FindLayer("child-player");
	Require(incoming.has_value() && scene->LayerChild(*incoming).Borrow().SameTarget(child), "JOIN must retain the actual copied child under the new incoming node controls");
	Require(clone.ChildCommands(child).Names().CurrentMotion.empty() && clone.ChildCommands(child).Names().RequestedMotion == "show" && clone.ChildCommands(child).CachedTimeline() == child_timeline && clone.ChildCommands(child).SelectionSerial() == child_serial, "same-request child Play must preserve the actual unnamed selection instead of reselecting or synthesizing a current name");
	Require(!scene->Packets().Nodes().empty(), "the retained unnamed child must complete actual inherited transfer and row publication");
	fixture.Tasks.Group(sgre::scene::MotionTaskGroup::Second).Draw(0);
	Reject([&] {
		clone.Progress(0);
	},
	    "queued");
	fixture.Runtime->SceneContext().Clear();
	clone.Progress(1);
	Require(child.IsLive() && clone.ChildCommands(child).SelectionSerial() == child_serial, "a later real phase must continue the same retained selection without replacing its owner");
	fixture.Tasks.Clear();
	Require(!clone.IsLive() && !child.IsLive() && fixture.Runtime->Identifiers().LiveCount() == 0, "copied JOIN retirement must release both selected trees and every actual child identifier");
}

static void TestClipGroupJoin(bool outgoing_hidden, bool incoming_hidden, bool join_target, bool changed_path, bool outgoing_eligible) {
	auto document = Document(outgoing_hidden, incoming_hidden, false, join_target, false);
	auto& motions = NestedField(NestedField(NestedField(document->Root, "object"), "character"), "motion");
	for (auto& [name, timeline] : std::get<sgre::resource::PsbObject>(motions.Data)) {
		auto& clip = std::get<sgre::resource::PsbArray>(NestedField(timeline, "layer").Data)[1];
		NestedField(clip, "type") = Atom(std::int64_t{7});
		if (name == "show") {
			NestedField(clip, "joinTarget") = Atom(outgoing_eligible);
		} else if (changed_path) {
			NestedField(clip, "label") = Atom(std::string("new-clip"));
		}
	}
	Fixture fixture(false, false, false, true, false, false, false, document);
	auto task = fixture.Attach();
	task.SetLeft(7);
	task.SetVisible(true);
	task.Play("show");
	task.Progress(0);
	task.Progress(2);
	const auto* scene = task.GenericPlayer()->Scene();
	const auto outgoing = scene->FindLayer("wipe");
	Require(outgoing && scene->LayerType(*outgoing) == 7, "clip JOIN requires an actual kind7 selected node");
	task.Play("hide", sgre::scene::MotionPlayFlags::Join);
	task.Progress(0);
	const auto incoming = scene->FindLayer("wipe");
	Require(incoming && scene->LayerType(*incoming) == 7, "clip JOIN must retain the genuine incoming node kind");
	const bool transfers = !outgoing_hidden && !incoming_hidden && join_target && !changed_path && outgoing_eligible;
	const auto expected = incoming_hidden ? 7.0f : transfers ? 11.0f
	                                                         : 107.0f;
	Require(scene->LayerCoordinate(*incoming)[0] == expected, "clip JOIN must apply path, kind, eligibility and visible numeric snapshot rules");
	for (const auto* row : scene->PacketRows()) {
		Require(row->Node().Raw->Type != 7, "structural JOIN clip groups must not invent sprite rows");
	}
	if (transfers) {
		task.Progress(2);
		Require(scene->LayerCoordinate(*incoming)[0] == 17, "clip JOIN must tween the captured current slot toward the incoming following slot");
		task.Progress(2);
		Require(scene->LayerCoordinate(*incoming)[0] == 23, "clip JOIN must decode the genuine next physical frame");
		task.SetTickCount(0);
		task.Progress(0);
		Require(scene->LayerCoordinate(*incoming)[0] == 107, "clip JOIN override must expire on a real seek");
	}
	Reject([&] {
		static_cast<void>(scene->LayerCoordinate(*outgoing));
	},
	    "retired actual root");
}

int main() {
	try {
		TestClipGroupJoin(false, false, true, false, true);
		TestClipGroupJoin(true, false, true, false, true);
		TestClipGroupJoin(false, true, true, false, true);
		TestClipGroupJoin(false, false, false, false, true);
		TestClipGroupJoin(false, false, true, true, true);
		TestClipGroupJoin(false, false, true, false, false);
		TestCopiedSelectedChildJoin();
		TestNestedPlayerJoin(false);
		TestNestedPlayerJoin(true);
		TestNestedResetThenJoin();
		TestRetiredEmptyGrandchildJoin();
		TestCompletedEmptyTimelineJoin();
		TestNumericJoin(false, false, true, false);
		TestNumericJoin(true, false, true, false);
		TestNumericJoin(false, true, true, false);
		TestNumericJoin(false, false, false, false);
		TestNumericJoin(false, false, true, true);
		TestExternalRestoreKeepsImmutableSourceNames();
		TestRealQueuedGuardAndTickSkip();
		TestRealOwningChildJoin(false);
		TestRealOwningChildJoin(true);
		TestActualEmptyChildJoin();
		TestUnmatchedChildSnapshotRetirement();
		std::cout << "actual Motion JOIN local values, incoming sources, physical slots and ownership guards passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
