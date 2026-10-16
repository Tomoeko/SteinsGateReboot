#include "motion_particle_scene_fixture.hpp"

#include "sgre/scene/motion_bezier_lattice.hpp"
#include "sgre/scene/motion_player_update.hpp"
#include "sgre/scene/motion_selected_layer_nodes.hpp"

struct CloneGeometrySnapshot {
	std::vector<std::vector<sgre::resource::MotionPoint>> Positions;
	std::size_t Globals = 0;
};

static CloneGeometrySnapshot Snapshot(const sgre::scene::MotionSceneEvaluation& scene) {
	CloneGeometrySnapshot result;
	result.Globals = scene.GlobalRows().size();
	for (const auto& node : scene.Packets().Nodes()) {
		const auto* geometry = node.Command.Geometry;
		result.Positions.emplace_back(geometry == nullptr ? std::vector<sgre::resource::MotionPoint>{} : std::vector<sgre::resource::MotionPoint>(geometry->Positions.begin(), geometry->Positions.end()));
	}
	return result;
}

static void RequireGeometry(const sgre::scene::MotionSceneEvaluation& scene, const CloneGeometrySnapshot& expected) {
	const auto nodes = scene.Packets().Nodes();
	Require(nodes.size() == expected.Positions.size() && scene.GlobalRows().size() == expected.Globals, "clone must publish the actual copied row graph before any update");
	for (std::size_t row = 0; row < nodes.size(); ++row) {
		const auto* geometry = nodes[row].Command.Geometry;
		const auto positions = geometry == nullptr ? std::span<const sgre::resource::MotionPoint>{} : geometry->Positions;
		Require(positions.size() == expected.Positions[row].size(), "clone geometry must preserve its actual completed allocation");
		for (std::size_t index = 0; index < positions.size(); ++index) {
			Require(std::bit_cast<std::uint32_t>(positions[index].X) == std::bit_cast<std::uint32_t>(expected.Positions[row][index].X) && std::bit_cast<std::uint32_t>(positions[index].Y) == std::bit_cast<std::uint32_t>(expected.Positions[row][index].Y), "clone must deep copy completed vertices without numeric recomposition");
		}
	}
}

static void TestSameRuntimeAndPendingRootInput() {
	ParticleSceneFixture fixture;
	auto source = fixture.Attach();
	source.SetVisible(true);
	source.Play("main");
	source.Progress(0);
	source.Progress(1);
	const auto* source_scene = source.GenericPlayer()->Scene();
	Require(source_scene != nullptr, "source must own its genuine generic scene");
	const auto completed = Snapshot(*source_scene);
	const auto random = source.Random().State();
	const auto event_count = fixture.Events.size();
	source.SetLeft(19);
	auto clone = sgre::scene::MotionPlayerTask::AttachClone(*fixture.Runtime, fixture.Tasks, source, false, [](const auto&) {
	});
	const auto* copied = clone.GenericPlayer()->Scene();
	Require(copied != nullptr && copied != source_scene && clone.Random().State() == random && source.Random().State() == random && fixture.Events.size() == event_count, "copy must create independent owners without clock/random/action replay");
	Require(clone.Commands().Names().CurrentMotion.empty() && clone.Commands().Names().RequestedMotion == "main" && clone.Commands().CachedTimeline() == source.Commands().CachedTimeline(), "native clone omits currentMotion while retaining selected atoms and requested names");
	Require(clone.Root().Overrides().Coordinate[0] == 19 && clone.Root().Overrides().Dirty && source.Root().Overrides().Dirty, "pending root input must remain pending in both independent owners");
	RequireGeometry(*copied, completed);
	RequireGeometry(*source_scene, completed);
	const auto source_nodes = source_scene->Packets().Nodes();
	const auto clone_nodes = copied->Packets().Nodes();
	for (std::size_t index = 0; index < source_nodes.size(); ++index) {
		if (source_nodes[index].Command.Geometry != nullptr) {
			Require(clone_nodes[index].Command.Geometry != source_nodes[index].Command.Geometry && (source_nodes[index].Command.Geometry->Positions.empty() || clone_nodes[index].Command.Geometry->Positions.data() != source_nodes[index].Command.Geometry->Positions.data()), "clone rows and vertex storage must be independently owned");
		}
	}
	fixture.Tasks.Group(sgre::scene::MotionTaskGroup::Second).Draw(0);
	Require(fixture.Runtime->SceneContext().Size() == 2, "both real tasks must draw before clone update");
	RequireFailure([&] {
		static_cast<void>(sgre::scene::MotionPlayerTask::AttachClone(*fixture.Runtime, fixture.Tasks, source, false, [](const auto&) {
		}));
	},
	    "queued");
	fixture.Runtime->SceneContext().Clear();
	source.Progress(1);
	clone.Progress(1);
	RequireGeometry(*copied, Snapshot(*source_scene));
	clone.SetTop(7);
	Require(source.Root().Overrides().Coordinate[1] == 0 && clone.Root().Overrides().Coordinate[1] == 7, "clone controls must not alias the source root");
}

struct Destination {
	ParticleSceneFixture& Source;
	sgre::scene::MotionResourceManager Manager;
	sgre::scene::MotionProjectId Project;
	std::unique_ptr<sgre::scene::MotionManagerRuntime> Runtime;
	sgre::scene::MotionLayerTasks Tasks;
	std::shared_ptr<const sgre::scene::LoadedMotionAsset> EarlierAsset;

	explicit Destination(ParticleSceneFixture& source)
	    : Source(source), Manager(source.Cache), Tasks(8) {
		EarlierAsset = Source.Catalog.Prepare(Document(), std::make_shared<const std::vector<std::uint8_t>>(144, 255));
		static_cast<void>(Manager.Register(EarlierAsset->Identity, EarlierAsset->Model, *EarlierAsset->Backing));
		Project = Manager.Register(source.Asset->Identity, source.Asset->Model, *source.Asset->Backing);
		sgre::scene::MotionManagerRuntimeServices services;
		services.SceneAssets = &Source.Catalog;
		services.ReadRegisteredAsset = [this](auto project) -> const sgre::scene::LoadedMotionAsset& {
			return project == Project ? *Source.Asset : *EarlierAsset;
		};
		services.ReadSourceAliases = [this](auto project) {
			return std::span<const sgre::scene::MotionSourceAlias>(project == Project ? Source.Asset->Aliases : EarlierAsset->Aliases);
		};
		Runtime = std::make_unique<sgre::scene::MotionManagerRuntime>(Manager, Source.Clock, Source.Seed, std::move(services), 32);
	}

	~Destination() {
		Runtime->SceneContext().Clear();
		Tasks.Clear();
	}
};

static void RetireCloneSource(ParticleSceneFixture& fixture, const sgre::scene::MotionPlayerTask& source, bool close_runtime) {
	auto retired = fixture.Tasks.Group(sgre::scene::MotionTaskGroup::Second).Extract(source.Handle());
	retired.Release();
	Require(!source.IsLive(), "pending clone must outlive the actual source task owner");
	if (close_runtime) {
		Require(fixture.Runtime->CanClose(), "independent pending clone must add no source runtime pin");
		fixture.Runtime->Close();
		fixture.Runtime.reset();
		Require(fixture.Manager.Unregister(fixture.Project), "pending clone's destination alias must survive source registration retirement");
		fixture.Manager.Close();
	}
}

static void TestPendingAssignedRootClone(bool cross_manager) {
	ParticleSceneFixture fixture;
	auto source = fixture.Attach();
	source.SetVisible(true);
	source.Play("main");
	source.Progress(0);
	source.Progress(1);
	const auto* original = source.GenericPlayer()->Scene();
	const auto completed = Snapshot(*original);
	Require(completed.Globals == 2, "pending fixture requires the real ordinary sprite and selected child rows");
	source.SetTickCount(5);
	Require(source.Commands().ClockState().TickAssigned && source.Commands().ClockState().InitialTagDispatchPending, "actual SetTickCount must leave both native assignment receipts pending");
	RequireGeometry(*original, completed);

	std::unique_ptr<Destination> destination;
	if (cross_manager) {
		destination = std::make_unique<Destination>(fixture);
	}
	auto& runtime = destination ? *destination->Runtime : *fixture.Runtime;
	auto& tasks = destination ? destination->Tasks : fixture.Tasks;
	auto clone = sgre::scene::MotionPlayerTask::AttachClone(runtime, tasks, source, false, [](const auto&) {
	});
	const auto* copied = clone.GenericPlayer()->Scene();
	auto pending = completed;
	pending.Globals = 0;
	RequireGeometry(*copied, pending);
	RequireGeometry(*original, completed);
	Require(clone.Commands().ClockState().TickCount == 5 && clone.Commands().ClockState().EvaluationTime == 5 && clone.Commands().ClockState().TickAssigned && clone.Commands().ClockState().InitialTagDispatchPending, "clone constructor must copy the pending clock without Seek or Progress");

	fixture.Tasks.Group(sgre::scene::MotionTaskGroup::Second).Draw(0);
	Require(fixture.Runtime->SceneContext().Size() == 1, "SetTickCount source must retain its completed Draw while pending clone contributes no draw borrow");
	fixture.Runtime->SceneContext().Clear();
	if (destination) {
		destination->Tasks.Group(sgre::scene::MotionTaskGroup::Second).Draw(0);
		Require(destination->Runtime->SceneContext().Size() == 0, "pending destination clone must append no constructor collection");
	}
	RetireCloneSource(fixture, source, cross_manager);

	clone.Progress(0);
	Require(!clone.Commands().ClockState().TickAssigned && !clone.Commands().ClockState().InitialTagDispatchPending && clone.Commands().ClockState().TickCount == 5 && clone.Commands().ClockState().EvaluationTime == 5, "first genuine clone Progress must consume the copied assignment at its actual requested tick");
	RequireGeometry(*copied, completed);
	tasks.Group(sgre::scene::MotionTaskGroup::Second).Draw(0);
	Require(runtime.SceneContext().Size() == 1, "first genuine Progress must republish the independent clone rows");
	runtime.SceneContext().Clear();
	clone.Progress(1);
	Require(clone.Commands().ClockState().TickCount == 6, "following Progress must use the copied clock rather than restart the source timeline");
}

struct ClonePhaseOwner {
	sgre::scene::MotionManagerRuntime& Runtime;
	sgre::scene::MotionAssetCatalog& Catalog;
	sgre::scene::MotionPreparationLease Preparation;
	sgre::scene::MotionPlayerCommands Commands;
	sgre::scene::MotionRootScene Root;
	sgre::scene::MotionPlayerRenderControls Render;
	std::unique_ptr<sgre::scene::MotionPlayerUpdate> Update;
	std::unique_ptr<sgre::scene::MotionSelectedLayerNodes> Selected;
	std::unique_ptr<sgre::scene::MotionSceneEvaluation> Scene;

	ClonePhaseOwner(sgre::scene::MotionManagerRuntime& runtime, sgre::scene::MotionAssetCatalog& catalog)
	    : Runtime(runtime), Catalog(catalog), Commands(runtime.Manager()) {
		Commands.SetCharacter("character");
		Commands.Play("main");
		Root.SetVisible(true);
		const auto prepared = Runtime.PrepareScene(*Commands.SelectedProject());
		Preparation = prepared.Owner;
		Update = std::make_unique<sgre::scene::MotionPlayerUpdate>(Commands, UpdateServices());
		Selected = std::make_unique<sgre::scene::MotionSelectedLayerNodes>(Commands, Root, Render, *Commands.CachedTimeline(), prepared.Timeline, Runtime.Identifiers(), ConstructionServices(), SlotServices());
		Scene = std::make_unique<sgre::scene::MotionSceneEvaluation>(*Selected, Commands, Root, Render, Catalog, Runtime, *Update, SceneServices());
	}

	ClonePhaseOwner(sgre::scene::MotionManagerRuntime& runtime, sgre::scene::MotionAssetCatalog& catalog, const ClonePhaseOwner& source)
	    : Runtime(runtime), Catalog(catalog), Commands(runtime.Manager()), Root(source.Root), Render(source.Render) {
		Commands.CopyForClone(source.Commands);
		const auto prepared = Runtime.ClonePreparation(source.Preparation);
		Preparation = prepared.Owner;
		Update = std::make_unique<sgre::scene::MotionPlayerUpdate>(Commands, UpdateServices());
		Selected = std::make_unique<sgre::scene::MotionSelectedLayerNodes>(Commands, Root, Render, *source.Selected, Runtime.Identifiers(), ConstructionServices(), SlotServices());
		Scene = std::make_unique<sgre::scene::MotionSceneEvaluation>(*Selected, Commands, Root, Render, Catalog, Runtime, *Update, SceneServices(), *source.Scene);
	}

	sgre::scene::MotionPlayerUpdateServices UpdateServices() {
		sgre::scene::MotionPlayerUpdateServices services;
		services.ReadFrameDelta = [this] {
			return Runtime.ReadFrameDelta();
		};
		services.PrepareFrames = [this](auto traversal, const auto& frame) {
			Scene->PrepareFrames(traversal, frame);
		};
		services.ComposeLayers = [this](const auto& frame) {
			Scene->Compose(frame);
		};
		services.PublishDrawPackets = [this](const auto& frame) {
			Scene->Publish(frame);
		};
		services.Draw = [this] {
			Scene->Draw();
		};
		services.DeliverEvent = [](const auto&) {
		};
		return services;
	}

	sgre::scene::MotionChildConstructionServices ConstructionServices() {
		sgre::scene::MotionChildConstructionServices services;
		services.ReadManagerRandom = [this](const auto& manager) {
			Require(&manager == &Runtime.Manager(), "pending fixture must sample only its actual manager");
			return Runtime.NextUnitFloat();
		};
		services.ReadDocumentRoot = [this](auto project) -> const sgre::resource::PsbValue& {
			return Runtime.ReadRegisteredAsset(project).Document->Root;
		};
		return services;
	}

	sgre::scene::MotionChildFrameSlotServices SlotServices() {
		sgre::scene::MotionChildFrameSlotServices services;
		services.EnqueueAction = [this](auto layer, auto action) {
			Update->EnqueueAction(layer, action);
		};
		return services;
	}

	sgre::scene::MotionSceneEvaluationServices SceneServices() {
		sgre::scene::MotionSceneEvaluationServices services;
		services.ReadRegisteredAsset = [this](auto project) -> const sgre::scene::LoadedMotionAsset& {
			return Runtime.ReadRegisteredAsset(project);
		};
		services.PrepareOwnedChildTimeline = [this](const auto& selection) {
			return Runtime.PrepareScene(selection);
		};
		services.RequireUnqueued = [this] {
			Require(!Scene || !Runtime.SceneContext().IsBorrowing(*Scene), "pending fixture cannot mutate its real queued scene");
		};
		services.AppendDraw = [this](const auto& packets, auto rows) {
			Require(Scene && &packets == &Scene->Packets() && rows.data() == Scene->GlobalRows().data(), "pending fixture Draw must borrow its exact complete scene");
			Runtime.SceneContext().Append(*Scene);
		};
		services.DeliverEvent = [](const auto&) {
		};
		return services;
	}
};

static void TestPendingAssignedChildClone(bool cross_manager) {
	ParticleSceneFixture fixture;
	auto source = std::make_unique<ClonePhaseOwner>(*fixture.Runtime, fixture.Catalog);
	source->Scene->Progress(0);
	source->Scene->Progress(1);
	const auto completed = Snapshot(*source->Scene);
	Require(completed.Globals == 2 && source->Commands.ClockState().TickCount == 1, "nested pending fixture requires the actual completed sibling and child");
	source->Selected->Child(1).Commands().SetTickCount(5);
	Require(!source->Commands.ClockState().TickAssigned && source->Selected->Child(1).Commands().ClockState().TickAssigned, "only the genuinely assigned nested player must be pending");
	RequireGeometry(*source->Scene, completed);
	source->Scene->Draw();
	Require(fixture.Runtime->SceneContext().Size() == 1, "source's existing nested collection survives a child-only SetTickCount");
	fixture.Runtime->SceneContext().Clear();

	std::unique_ptr<Destination> destination;
	if (cross_manager) {
		destination = std::make_unique<Destination>(fixture);
	}
	auto& runtime = destination ? *destination->Runtime : *fixture.Runtime;
	auto clone = std::make_unique<ClonePhaseOwner>(runtime, fixture.Catalog, *source);
	auto pending = completed;
	pending.Globals = 1;
	RequireGeometry(*clone->Scene, pending);
	RequireGeometry(*source->Scene, completed);
	const auto& child = clone->Selected->Child(1).Commands();
	Require(!clone->Commands.ClockState().TickAssigned && child.ClockState().TickAssigned && child.ClockState().TickCount == 5 && child.Names().CurrentMotion.empty() && child.Names().RequestedMotion == "main", "nested clone must preserve its own pending clock and native current-name omission");
	source.reset();
	if (cross_manager) {
		Require(fixture.Runtime->CanClose(), "nested pending clone must not retain source phase or preparation owners");
		fixture.Runtime->Close();
		fixture.Runtime.reset();
		Require(fixture.Manager.Unregister(fixture.Project), "nested pending clone must outlive its source registration");
		fixture.Manager.Close();
	}

	clone->Scene->Draw();
	const std::array<sgre::resource::MotionPoint, 2> sizes{{{4, 4}, {4, 4}}};
	const auto frame = runtime.SceneContext().Prepare(ParticleSceneGlobals(sizes));
	Require(frame.Queue.size() == 1, "pending child must contribute no rows while the ordinary sibling remains drawable");
	runtime.SceneContext().Clear();
	clone->Scene->Progress(0);
	Require(clone->Commands.ClockState().TickCount == 1 && !child.ClockState().TickAssigned && child.ClockState().TickCount == 5 && child.ClockState().EvaluationTime == 5, "genuine containing Progress0 must consume the child's assignment without advancing the root clock");
	RequireGeometry(*clone->Scene, completed);
	clone->Scene->Draw();
	const auto resumed = runtime.SceneContext().Prepare(ParticleSceneGlobals(sizes));
	Require(resumed.Queue.size() == 2, "first genuine containing Progress must restore actual nested rows");
	runtime.SceneContext().Clear();
	clone->Scene->Progress(1);
	Require(clone->Commands.ClockState().TickCount == 2 && child.ClockState().TickCount == 6, "later containing Progress must advance both real clocks from their independent copied positions");
}

static void TestDestinationAliasAndSourceFirstRetirement() {
	ParticleSceneFixture fixture;
	auto source = fixture.Attach();
	source.SetVisible(true);
	source.Play("main");
	source.Progress(0);
	source.Progress(1);
	const auto expected = Snapshot(*source.GenericPlayer()->Scene());
	Destination destination(fixture);
	const auto manager_random = destination.Runtime->NextUnitFloat();
	sgre::scene::MotionSharedRandom expected_random(fixture.Seed);
	Require(manager_random == expected_random.NextUnitFloat(), "destination setup must use its genuine independent stream");
	auto clone = sgre::scene::MotionPlayerTask::AttachClone(*destination.Runtime, destination.Tasks, source, false, [](const auto&) {
	});
	Require(!source.Root().Overrides().Dirty && clone.Root().Overrides().Dirty, "native top-level clone must mark its new root pending without changing the clean source");
	Require(clone.Commands().SelectedProject()->Project == destination.Project && clone.Commands().SelectedProject()->Project != fixture.Project && clone.Commands().SelectedProject()->Timeline == source.Commands().SelectedProject()->Timeline, "copy must use exact immutable aliases rather than reusing source project numbers");
	Require(destination.Runtime->NextUnitFloat() == expected_random.NextUnitFloat(), "copy constructor must not consume a manager random sample");
	RequireGeometry(*clone.GenericPlayer()->Scene(), expected);
	fixture.Tasks.Clear();
	Require(!source.IsLive() && fixture.Runtime->CanClose(), "source task/preparation runtime must retire while cloned frames and rows remain live");
	fixture.Runtime->Close();
	fixture.Runtime.reset();
	Require(fixture.Manager.Unregister(fixture.Project), "source registration must retire independently of the destination alias");
	fixture.Manager.Close();
	destination.Tasks.Group(sgre::scene::MotionTaskGroup::Second).Draw(0);
	const std::array<sgre::resource::MotionPoint, 2> sizes{{{4, 4}, {4, 4}}};
	const auto frame = destination.Runtime->SceneContext().Prepare(ParticleSceneGlobals(sizes));
	Require(frame.Queue.size() == expected.Globals && !frame.Queue.empty(), "source-first retirement must retain actual destination texture-bound draw rows");
	destination.Runtime->SceneContext().Clear();
	clone.Progress(1);
	destination.Tasks.Group(sgre::scene::MotionTaskGroup::Second).Draw(0);
	Require(destination.Runtime->SceneContext().Size() == 1, "independent destination must continue its genuine nested update after source retirement");
	destination.Runtime->SceneContext().Clear();
}

static sgre::resource::PsbValue& MutableField(sgre::resource::PsbValue& object, std::string_view key) {
	for (auto& [name, value] : std::get<sgre::resource::PsbObject>(object.Data)) {
		if (name == key) {
			return value;
		}
	}
	throw std::runtime_error("clone fixture requires an existing authored metadata field");
}

static std::shared_ptr<sgre::resource::PsbDocument> ClippedDocument(bool deforming) {
	auto document = Document();
	auto& objects = MutableField(document->Root, "object");
	auto& main = MutableField(MutableField(MutableField(objects, "character"), "motion"), "main");
	auto& layers = std::get<sgre::resource::PsbArray>(MutableField(main, "layer").Data);
	auto child = layers[1];
	auto writer = Layer("writer", false);
	MutableField(writer, "stencilType") = Atom(std::int64_t{1});
	MutableField(writer, "children") = Atom(sgre::resource::PsbArray{std::move(child)});
	if (deforming) {
		MutableField(writer, "meshTransform") = Atom(std::int64_t{1});
		auto& properties = std::get<sgre::resource::PsbObject>(writer.Data);
		properties.emplace_back("meshSyncChildMask", Atom(std::int64_t{8}));
		properties.emplace_back("meshDivision", Atom(std::int64_t{4}));
		properties.emplace_back("meshCombine", Atom(false));
		auto& frame = std::get<sgre::resource::PsbArray>(MutableField(writer, "frameList").Data).front();
		auto& content = MutableField(frame, "content");
		MutableField(content, "mask") = Atom(std::int64_t{0x02060603});
		auto patch = sgre::scene::IdentityMotionBezierPatch();
		patch.Controls[5].Y += 0.125f;
		patch.Controls[10].X += 0.25f;
		sgre::resource::PsbArray controls;
		for (const auto& point : patch.Controls) {
			controls.push_back(Atom(static_cast<double>(point.X)));
			controls.push_back(Atom(static_cast<double>(point.Y)));
		}
		std::get<sgre::resource::PsbObject>(content.Data).emplace_back("mesh", Atom(sgre::resource::PsbObject{{"cc", Atom(std::monostate{})}, {"bp", Atom(std::move(controls))}}));
	}

	auto clip = Layer("clip", false);
	MutableField(clip, "type") = Atom(std::int64_t{7});
	MutableField(clip, "children") = Atom(sgre::resource::PsbArray{std::move(writer)});
	layers = {std::move(clip)};
	auto& priority = std::get<sgre::resource::PsbArray>(MutableField(main, "priority").Data).front();
	MutableField(priority, "content") = Atom(sgre::resource::PsbArray{Atom(std::int64_t{0}), Atom(std::int64_t{1}), Atom(std::int64_t{2})});

	return document;
}

static void TestDormantChildRootLinks(bool deforming, bool inactive = false) {
	ParticleSceneFixture fixture(false, false, false, false, 32, false, 0, ClippedDocument(deforming));
	auto source = fixture.Attach();
	source.SetVisible(true);
	source.Play("main");
	source.Progress(0);
	const auto* original = source.GenericPlayer()->Scene();
	Require(original != nullptr && original->PacketRows().size() == 3, "clipped native-domain fixture requires genuine parent and nested rows");
	bool source_clip = false;
	bool source_ancestor = false;
	for (const auto* row : original->PacketRows()) {
		source_clip = source_clip || original->DrawState(*row).IndividualClip != nullptr;
		source_ancestor = source_ancestor || row->Ancestor() != nullptr;
	}
	Require(source_clip && source_ancestor, "source must have actual live clip/stencil borrows before clone");
	if (deforming) {
		bool actual_grid = false;
		for (const auto* row : original->PacketRows()) {
			actual_grid = actual_grid || (row->Node().Prepared->MeshTransform == 1 && original->DrawState(*row).Subdivision.X > 1);
		}
		Require(actual_grid, "deformation copy case requires actual exported serialized mesh geometry");
	}
	if (inactive) {
		source.SetVisible(false);
		source.Progress(0);
		Require(original->GlobalRows().empty(), "genuinely hidden source must append no global drawing rows");
		bool retained = false;
		for (const auto* row : original->PacketRows()) {
			retained = retained || row->Ancestor() != nullptr;
		}
		Require(retained, "source's inactive nested rows must retain the previous native row ancestry before copy");
	}
	const auto positions = Snapshot(*original);
	Destination destination(fixture);
	auto clone = sgre::scene::MotionPlayerTask::AttachClone(*destination.Runtime, destination.Tasks, source, false, [](const auto&) {
	});
	const auto* copied = clone.GenericPlayer()->Scene();
	if (!inactive) {
		RequireGeometry(*copied, positions);
	} else {
		Require(copied->GlobalRows().empty(), "copied inactive root must have an empty genuine50AD30 global collection");
	}
	for (const auto* row : copied->PacketRows()) {
		Require(copied->DrawState(*row).IndividualClip == nullptr && row->Ancestor() == nullptr && (!inactive || row->Members().empty()), "whole original504CF0 clears nonroot current links and inactive members before immediate50AD30");
	}
	fixture.Tasks.Clear();
	Require(fixture.Runtime->CanClose(), "dormant copied root links must add no source runtime pin");
	fixture.Runtime->Close();
	fixture.Runtime.reset();
	Require(fixture.Manager.Unregister(fixture.Project), "copied root must not retain the source registration");
	fixture.Manager.Close();
	destination.Tasks.Group(sgre::scene::MotionTaskGroup::Second).Draw(0);
	destination.Runtime->SceneContext().Clear();
	clone.Progress(0);
	bool destination_clip = false;
	bool destination_ancestor = false;
	for (const auto* row : copied->PacketRows()) {
		destination_clip = destination_clip || copied->DrawState(*row).IndividualClip != nullptr;
		destination_ancestor = destination_ancestor || row->Ancestor() != nullptr;
	}
	if (inactive) {
		Require(!destination_clip && !destination_ancestor && copied->GlobalRows().empty(), "next genuine hidden parent transfer must preserve the native empty collection");
	} else {
		Require(destination_clip && destination_ancestor, "next genuine parent transfer must install destination clip/stencil before child composition");
	}
	destination.Tasks.Group(sgre::scene::MotionTaskGroup::Second).Draw(0);
	destination.Runtime->SceneContext().Clear();
}

int main() {
	try {
		TestSameRuntimeAndPendingRootInput();
		TestDestinationAliasAndSourceFirstRetirement();
		TestPendingAssignedRootClone(false);
		TestPendingAssignedRootClone(true);
		TestPendingAssignedChildClone(false);
		TestPendingAssignedChildClone(true);
		TestDormantChildRootLinks(false);
		TestDormantChildRootLinks(true);
		TestDormantChildRootLinks(true, true);
		std::cout << "motion scene clone tests passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
