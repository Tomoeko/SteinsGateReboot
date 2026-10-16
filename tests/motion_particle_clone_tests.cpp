#include "motion_particle_scene_fixture.hpp"

static sgre::resource::PsbValue& Field(sgre::resource::PsbValue& object, std::string_view key) {
	for (auto& [name, value] : std::get<sgre::resource::PsbObject>(object.Data)) {
		if (name == key) {
			return value;
		}
	}
	throw std::runtime_error("particle clone fixture requires its declared PSB field");
}

static std::shared_ptr<sgre::resource::PsbDocument> CloneDocument(std::int32_t mode, bool nested_child = false, bool outside = false) {
	auto document = Document();
	auto& objects = Field(document->Root, "object");
	auto& main = Field(Field(Field(objects, "character"), "motion"), "main");
	auto emitter = Layer("emitter", false);
	Field(emitter, "type") = Atom(std::int64_t{4});
	auto& fields = std::get<sgre::resource::PsbObject>(emitter.Data);
	fields.emplace_back("particle", Atom(static_cast<std::int64_t>(mode)));
	fields.emplace_back("particleMaxNum", Atom(std::int64_t{32}));
	fields.emplace_back("particleInheritAngle", Atom(false));
	fields.emplace_back("particleDeleteOutsideScreen", Atom(outside));
	fields.emplace_back("particleTriVolume", Atom(false));
	fields.emplace_back("particleInheritOpacity", Atom(std::int64_t{1}));
	fields.emplace_back("particleInheritVelocity", Atom(std::int64_t{0}));
	fields.emplace_back("particleFlyDirection", Atom(std::int64_t{0}));
	fields.emplace_back("particleApplyZoomToVelocity", Atom(std::int64_t{1}));
	fields.emplace_back("particleAccelRatio", Atom(0.5));
	fields.emplace_back("particleMotionList", Atom(sgre::resource::PsbArray{Atom(sgre::resource::PsbArray{Atom(std::string("child")), Atom(std::string("main"))})}));
	const auto particle = Atom(sgre::resource::PsbObject{{"mask", Atom(std::int64_t{0x3f})}, {"trigger", Atom(std::int64_t{1})}, {"fmin", Atom(1.0)}, {"fmax", Atom(1.0)}, {"vmin", Atom(0.5)}, {"vmax", Atom(2.0)}, {"amin", Atom(-90.0)}, {"amax", Atom(360.0)}, {"zmin", Atom(0.5)}, {"zmax", Atom(2.0)}, {"range", Atom(30.0)}});
	const auto content = Atom(sgre::resource::PsbObject{{"mask", Atom(std::int64_t{0x100001})}, {"ox", Atom(0.25)}, {"oy", Atom(-0.125)}, {"prt", particle}});
	const auto first = Atom(sgre::resource::PsbObject{{"time", Atom(0.0)}, {"type", Atom(std::int64_t{3})}, {"content", content}});
	const auto end = Atom(sgre::resource::PsbObject{{"time", Atom(8.0)}, {"type", Atom(std::int64_t{0})}, {"content", Atom(std::monostate{})}});
	Field(emitter, "frameList") = Atom(sgre::resource::PsbArray{first, end});
	Field(main, "layer") = Atom(sgre::resource::PsbArray{emitter});
	Field(std::get<sgre::resource::PsbArray>(Field(main, "priority").Data).front(), "content") = Atom(sgre::resource::PsbArray{Atom(std::int64_t{0})});

	auto& child = Field(Field(Field(objects, "child"), "motion"), "main");
	auto group = Layer("group", false);
	Field(group, "type") = Atom(std::int64_t{2});
	Field(group, "children") = Atom(sgre::resource::PsbArray{Layer("sprite", false)});
	Field(child, "layer") = Atom(sgre::resource::PsbArray{group});
	Field(std::get<sgre::resource::PsbArray>(Field(child, "priority").Data).front(), "content") = Atom(sgre::resource::PsbArray{Atom(std::int64_t{0}), Atom(std::int64_t{1})});
	if (nested_child) {
		const auto leaf = Field(objects, "child");
		auto container = Layer("nested-child", true);
		Field(Field(std::get<sgre::resource::PsbArray>(Field(container, "frameList").Data).front(), "content"), "src") = Atom(std::string("leaf"));
		Field(child, "layer") = Atom(sgre::resource::PsbArray{container});
		Field(std::get<sgre::resource::PsbArray>(Field(child, "priority").Data).front(), "content") = Atom(sgre::resource::PsbArray{Atom(std::int64_t{0})});
		std::get<sgre::resource::PsbObject>(objects.Data).emplace_back("leaf", leaf);
	}
	if (outside) {
		std::get<sgre::resource::PsbObject>(document->Root.Data).emplace_back("screenSize", Atom(sgre::resource::PsbObject{{"width", Atom(4096.0)}, {"height", Atom(4096.0)}, {"originX", Atom(2048.0)}, {"originY", Atom(2048.0)}}));
	}
	return document;
}

struct ParticleCloneSnapshot {
	std::vector<std::uint32_t> Words;
	const sgre::scene::MotionClipGeometry* Geometry = nullptr;
	const sgre::resource::MotionPoint* Positions = nullptr;
	const sgre::resource::MotionLayer* Raw = nullptr;
	const sgre::scene::PreparedMotionLayer* Prepared = nullptr;
};

static ParticleCloneSnapshot Snapshot(const sgre::scene::MotionPlayerTask& task) {
	const auto* scene = task.GenericPlayer()->Scene();
	Require(scene != nullptr && scene->GlobalRows().size() == 1 && scene->PacketRows().size() == 1, "genuine single particle must publish its completed source row before copying");
	const auto state = scene->DrawState(*scene->GlobalRows().front());
	Require(state.Numeric && state.Numeric->Active && state.Source && state.Source->State().Binding && state.Render && !state.IndividualClip, "fixture requires its actual finite ordinary source and completed receiving owners");
	const auto nodes = scene->Packets().Nodes();
	Require(nodes.size() == 1 && nodes.front().Command.Geometry != nullptr, "real particle must retain its actual geometry allocation");
	ParticleCloneSnapshot result;
	result.Raw = scene->GlobalRows().front()->Node().Raw;
	result.Prepared = scene->GlobalRows().front()->Node().Prepared;
	result.Geometry = nodes.front().Command.Geometry;
	result.Positions = result.Geometry->Positions.data();
	for (const auto& point : result.Geometry->Positions) {
		result.Words.push_back(std::bit_cast<std::uint32_t>(point.X));
		result.Words.push_back(std::bit_cast<std::uint32_t>(point.Y));
	}
	const auto& hierarchy = state.Numeric->Hierarchy;
	for (const auto coordinate : hierarchy.Attributes.Coordinate) {
		result.Words.push_back(std::bit_cast<std::uint32_t>(coordinate));
	}
	for (const auto value : {hierarchy.Transform.XX, hierarchy.Transform.XY, hierarchy.Transform.YX, hierarchy.Transform.YY, hierarchy.Attributes.ZoomX, hierarchy.Attributes.ZoomY, hierarchy.Attributes.Angle}) {
		result.Words.push_back(std::bit_cast<std::uint32_t>(value));
	}
	result.Words.push_back(hierarchy.Attributes.Opacity);
	result.Words.push_back(hierarchy.ModulationColor);
	result.Words.push_back(state.Render->ColorFilter);
	result.Words.push_back(hierarchy.Attributes.BlendMode);
	return result;
}

struct ParticleCloneDestination {
	ParticleSceneFixture& Source;
	sgre::scene::MotionResourceManager Manager;
	sgre::scene::MotionProjectId Prefix;
	sgre::scene::MotionProjectId Project;
	std::shared_ptr<const sgre::scene::LoadedMotionAsset> PrefixAsset;
	std::unique_ptr<sgre::scene::MotionManagerRuntime> Runtime;
	sgre::scene::MotionLayerTasks Tasks;

	explicit ParticleCloneDestination(ParticleSceneFixture& source)
	    : Source(source), Manager(source.Cache), Tasks(8) {
		PrefixAsset = Source.Catalog.Prepare(Document(), std::make_shared<const std::vector<std::uint8_t>>(144, 255));
		Prefix = Manager.Register(PrefixAsset->Identity, PrefixAsset->Model, *PrefixAsset->Backing);
		Project = Manager.Register(source.Asset->Identity, source.Asset->Model, *source.Asset->Backing);
		sgre::scene::MotionManagerRuntimeServices services;
		services.SceneAssets = &Source.Catalog;
		services.ReadRegisteredAsset = [this](auto project) -> const sgre::scene::LoadedMotionAsset& {
			if (project == Project) {
				return *Source.Asset;
			}
			Require(project == Prefix, "destination may only lend an actual registered immutable asset");
			return *PrefixAsset;
		};
		services.ReadSourceAliases = [this](auto project) {
			Require(project == Project || project == Prefix, "destination aliases require their exact registration");
			return std::span<const sgre::scene::MotionSourceAlias>(project == Project ? Source.Asset->Aliases : PrefixAsset->Aliases);
		};
		Runtime = std::make_unique<sgre::scene::MotionManagerRuntime>(Manager, Source.Clock, Source.Seed, std::move(services), 32);
	}

	~ParticleCloneDestination() {
		if (Runtime) {
			Runtime->SceneContext().Clear();
		}
		Tasks.Clear();
	}
};

static void ReleaseTask(sgre::scene::MotionLayerTasks& tasks, const sgre::scene::MotionPlayerTask& task) {
	auto retained = tasks.Group(sgre::scene::MotionTaskGroup::Second).Extract(task.Handle());
	retained.Release();
	Require(!task.IsLive(), "actual task release must expire the weak source or destination view");
}

static void CloseSourceRuntime(ParticleSceneFixture& fixture) {
	Require(fixture.Runtime->CanClose() && fixture.Runtime->Identifiers().LiveCount() == 0, "all source particle contexts and UIDs must retire before closing their actual runtime");
	fixture.Runtime->Close();
	fixture.Runtime.reset();
	Require(fixture.Manager.ReferenceCount(fixture.Project) == 1 && fixture.Manager.Unregister(fixture.Project), "source-first retirement must not add a hidden registration retain");
	fixture.Manager.Close();
}

static void FinishReceivingOwners(ParticleSceneFixture& fixture, ParticleCloneDestination* destination, sgre::scene::MotionPlayerTask& source, sgre::scene::MotionPlayerTask& clone) {
	if (source.IsLive()) {
		ReleaseTask(fixture.Tasks, source);
	}
	if (destination) {
		destination->Runtime->SceneContext().Clear();
		ReleaseTask(destination->Tasks, clone);
		Require(destination->Runtime->CanClose() && destination->Runtime->Identifiers().LiveCount() == 0, "destination retirement must release every independently copied particle UID and preparation");
		destination->Runtime->Close();
		destination->Runtime.reset();
		Require(destination->Manager.ReferenceCount(destination->Project) == 1 && destination->Manager.ReferenceCount(destination->Prefix) == 1, "clone owners must borrow exact registrations without retaining either alias");
		Require(destination->Manager.Unregister(destination->Project) && destination->Manager.Unregister(destination->Prefix), "destination texture registrations must retire after their independent selected owners");
		destination->Manager.Close();
		destination->PrefixAsset.reset();
	} else {
		fixture.Runtime->SceneContext().Clear();
		ReleaseTask(fixture.Tasks, clone);
	}
	if (fixture.Runtime) {
		CloseSourceRuntime(fixture);
	}
	fixture.Asset.reset();
	fixture.Catalog.Prune();
	Require(fixture.Catalog.DocumentCount() == 0 && fixture.Cache.Count() == 0 && fixture.Uploads == fixture.Releases, "source-first clone retirement must balance all catalog, texture and selected UID owners");
}

static void TestGenuineBirthClone(std::int32_t mode, bool cross_manager) {
	ParticleSceneFixture fixture(false, false, false, false, 64, false, 0, CloneDocument(mode));
	auto source = fixture.Attach();
	source.SetVisible(true);
	source.Play("main");
	source.Progress(1);
	source.Progress(1);
	Require(fixture.Runtime->Identifiers().LiveCount() == 3 && source.Commands().ClockState().TickCount == 1, "source must genuinely emit one playing ordinary0/2 child and complete both parent updates");
	const auto expected = Snapshot(source);
	const auto random = source.Random().State();
	const auto events = fixture.Events.size();
	std::unique_ptr<ParticleCloneDestination> destination;
	if (cross_manager) {
		destination = std::make_unique<ParticleCloneDestination>(fixture);
	}
	auto& receiving_runtime = cross_manager ? *destination->Runtime : *fixture.Runtime;
	auto& receiving_tasks = cross_manager ? destination->Tasks : fixture.Tasks;
	auto clone = sgre::scene::MotionPlayerTask::AttachClone(receiving_runtime, receiving_tasks, source, false, [](const auto&) {
	});
	const auto actual = Snapshot(clone);
	Require(actual.Raw == expected.Raw && actual.Prepared == expected.Prepared, "particle clone must preserve the exact immutable raw/prepared metadata choices");
	Require(actual.Words == expected.Words && actual.Geometry != expected.Geometry && actual.Positions != expected.Positions, "whole clone must deep-copy completed particle geometry/numerics before any destination update");
	Require(clone.Random().State() == random && source.Random().State() == random && fixture.Events.size() == events && clone.Commands().Names().CurrentMotion.empty(), "copy must preserve genuine random/clock state without replaying selections or actions");
	if (cross_manager) {
		Require(clone.Commands().SelectedProject()->Project == destination->Project && destination->Project != fixture.Project, "destination must resolve the exact immutable project alias with a shifted local index");
		sgre::scene::MotionSharedRandom expected_manager(fixture.Seed);
		Require(destination->Runtime->NextUnitFloat() == expected_manager.NextUnitFloat(), "particle copy must not sample the destination manager stream");
	}
	receiving_tasks.Group(sgre::scene::MotionTaskGroup::Second).Draw(0);
	Require(receiving_runtime.SceneContext().Size() == (cross_manager ? 1 : 2), "real task Draw must consume copied particle rows before first update");
	RequireFailure([&] {
		clone.Progress(0);
	},
	    "queued");
	receiving_runtime.SceneContext().Clear();

	for (const auto elapsed : {0.0f, 0.5f, 1.0f}) {
		source.Progress(elapsed);
		clone.Progress(elapsed);
		Require(Snapshot(source).Words == Snapshot(clone).Words && source.Random().State() == clone.Random().State(), "retained child clocks/velocity/cadence must continue equally without a replayed birth");
		Require(receiving_runtime.Identifiers().LiveCount() == (cross_manager ? 3 : 6), "unchanged emitter must retain its copied actual child rather than constructing a replacement");
	}
	ReleaseTask(fixture.Tasks, source);
	if (cross_manager) {
		CloseSourceRuntime(fixture);
	}
	clone.Progress(0.5f);
	Require(Snapshot(clone).Words.size() == expected.Words.size(), "cloned Context and preparation must continue after source-first task/runtime retirement");
	receiving_tasks.Group(sgre::scene::MotionTaskGroup::Second).Draw(0);
	const std::array<sgre::resource::MotionPoint, 2> sizes{{{4, 4}, {4, 4}}};
	Require(receiving_runtime.SceneContext().Prepare(ParticleSceneGlobals(sizes)).Queue.size() == 1, "source-first copied particle must submit through its real destination texture/source owner");
	receiving_runtime.SceneContext().Clear();
	FinishReceivingOwners(fixture, destination.get(), source, clone);
}

struct RetainedTreeSnapshot {
	std::size_t GlobalRows = 0;
	std::vector<std::uint32_t> Words;
	std::vector<const sgre::resource::MotionLayer*> Raw;
	std::vector<const sgre::scene::PreparedMotionLayer*> Prepared;
	std::vector<const sgre::scene::MotionClipGeometry*> Geometry;
	std::vector<const sgre::resource::MotionPoint*> Positions;
};

static RetainedTreeSnapshot SnapshotTree(const sgre::scene::MotionPlayerTask& task) {
	const auto* scene = task.GenericPlayer()->Scene();
	Require(scene != nullptr, "retained particle copy requires its real completed containing scene");
	RetainedTreeSnapshot result;
	result.GlobalRows = scene->GlobalRows().size();
	for (const auto* row : scene->PacketRows()) {
		result.Raw.push_back(row->Node().Raw);
		result.Prepared.push_back(row->Node().Prepared);
	}
	for (const auto& node : scene->Packets().Nodes()) {
		result.Geometry.push_back(node.Command.Geometry);
		result.Positions.push_back(node.Command.Geometry ? node.Command.Geometry->Positions.data() : nullptr);
		result.Words.push_back(node.Command.Geometry ? static_cast<std::uint32_t>(node.Command.Geometry->Positions.size()) : 0);
		if (node.Command.Geometry) {
			for (const auto& point : node.Command.Geometry->Positions) {
				result.Words.push_back(std::bit_cast<std::uint32_t>(point.X));
				result.Words.push_back(std::bit_cast<std::uint32_t>(point.Y));
			}
		}
	}
	return result;
}

static void RequireCopiedTree(const RetainedTreeSnapshot& source, const RetainedTreeSnapshot& clone) {
	Require(clone.GlobalRows == source.GlobalRows && clone.Words == source.Words && clone.Raw == source.Raw && clone.Prepared == source.Prepared, "copy before update must retain completed rows/geometry and exact immutable raw/prepared choices");
	Require(clone.Geometry.size() == source.Geometry.size(), "cloned completed packet storage must retain the actual source shape");
	for (std::size_t index = 0; index != source.Geometry.size(); ++index) {
		if (source.Geometry[index]) {
			Require(clone.Geometry[index] && clone.Geometry[index] != source.Geometry[index] && clone.Positions[index] != source.Positions[index], "each copied retained particle must own independent completed geometry storage");
		}
	}
}

static void TestStoppedRetainedBeforePrune(bool cross_manager) {
	ParticleSceneFixture fixture(false, false, false, false, 64, false, 0, CloneDocument(0));
	auto source = fixture.Attach();
	source.SetVisible(true);
	source.Play("main");
	source.Progress(0);
	source.Progress(8);
	Require(!source.Commands().ClockState().Playing && fixture.Runtime->Identifiers().LiveCount() == 3, "genuine end tick must retain the stopped particle until the next parent pre-tick prune");
	const auto expected = SnapshotTree(source);
	const auto random = source.Random().State();
	const auto events = fixture.Events.size();
	std::unique_ptr<ParticleCloneDestination> destination;
	if (cross_manager) {
		destination = std::make_unique<ParticleCloneDestination>(fixture);
	}
	auto& runtime = destination ? *destination->Runtime : *fixture.Runtime;
	auto& tasks = destination ? destination->Tasks : fixture.Tasks;
	auto clone = sgre::scene::MotionPlayerTask::AttachClone(runtime, tasks, source, false, [](const auto&) {
	});
	RequireCopiedTree(expected, SnapshotTree(clone));
	Require(!clone.Commands().ClockState().Playing && source.Random().State() == random && clone.Random().State() == random && fixture.Events.size() == events, "copy must retain stopped child ownership without Play, birth, clock or random work");
	Require(runtime.Identifiers().LiveCount() == (cross_manager ? 3 : 6), "retained stopped descendants must be cloned even though their Playing flag is false");
	tasks.Group(sgre::scene::MotionTaskGroup::Second).Draw(0);
	runtime.SceneContext().Clear();
	RequireCopiedTree(expected, SnapshotTree(clone));
	ReleaseTask(fixture.Tasks, source);
	if (cross_manager) {
		CloseSourceRuntime(fixture);
	}
	clone.Progress(0);
	Require(runtime.Identifiers().LiveCount() == 1 && clone.GenericPlayer()->Scene()->GlobalRows().empty() && clone.Random().State() == random, "next genuine destination update must prune copied stopped children without an extra birth or random sample");
	FinishReceivingOwners(fixture, destination.get(), source, clone);
}

static std::shared_ptr<sgre::resource::PsbDocument> ScreenLatchDocument() {
	auto document = CloneDocument(0, false, true);
	auto& screen = Field(document->Root, "screenSize");
	Field(screen, "width") = Atom(2.0);
	Field(screen, "height") = Atom(2.0);
	Field(screen, "originX") = Atom(0.0);
	Field(screen, "originY") = Atom(0.0);
	auto& objects = Field(document->Root, "object");
	auto& main = Field(Field(Field(objects, "character"), "motion"), "main");
	auto& emitter = std::get<sgre::resource::PsbArray>(Field(main, "layer").Data).front();
	Field(emitter, "particleApplyZoomToVelocity") = Atom(std::int64_t{0});
	Field(emitter, "particleAccelRatio") = Atom(1.0);
	auto& content = Field(std::get<sgre::resource::PsbArray>(Field(emitter, "frameList").Data).front(), "content");
	Field(content, "ox") = Atom(0.0);
	Field(content, "oy") = Atom(0.0);
	auto& particle = Field(content, "prt");
	for (const auto name : {"vmin", "vmax", "zmin", "zmax"}) {
		Field(particle, name) = Atom(1.0);
	}
	for (const auto name : {"amin", "amax", "range"}) {
		Field(particle, name) = Atom(0.0);
	}
	auto& child = Field(Field(Field(objects, "child"), "motion"), "main");
	auto& bounds = Field(child, "bounds");
	Field(bounds, "right") = Atom(1.0);
	Field(bounds, "bottom") = Atom(1.0);
	return document;
}

static void TestEnteredScreenLatchSurvivesClone(bool cross_manager) {
	ParticleSceneFixture fixture(false, false, false, false, 64, false, 0, ScreenLatchDocument());
	auto source = fixture.Attach();
	source.SetVisible(true);
	source.Play("main");
	source.Progress(0);
	source.Progress(4);
	Require(fixture.Runtime->Identifiers().LiveCount() == 3 && Snapshot(source).Words.size() > 0, "real inside-screen birth must retain a playing child after its movement outside the expanded screen");
	const auto expected = SnapshotTree(source);
	const auto random = source.Random().State();
	std::unique_ptr<ParticleCloneDestination> destination;
	if (cross_manager) {
		destination = std::make_unique<ParticleCloneDestination>(fixture);
	}
	auto& runtime = destination ? *destination->Runtime : *fixture.Runtime;
	auto& tasks = destination ? destination->Tasks : fixture.Tasks;
	auto clone = sgre::scene::MotionPlayerTask::AttachClone(runtime, tasks, source, false, [](const auto&) {
	});
	RequireCopiedTree(expected, SnapshotTree(clone));
	tasks.Group(sgre::scene::MotionTaskGroup::Second).Draw(0);
	runtime.SceneContext().Clear();
	source.Progress(0);
	clone.Progress(0);
	Require(source.GenericPlayer()->Scene()->GlobalRows().empty() && clone.GenericPlayer()->Scene()->GlobalRows().empty(), "copied entered-screen latch must retire both already-outside children on the next actual parent phase");
	Require(runtime.Identifiers().LiveCount() == (cross_manager ? 1 : 2) && clone.Random().State() == random && source.Random().State() == random, "screen retirement must release actual copied UIDs without consuming random or replacing the birth");
	ReleaseTask(fixture.Tasks, source);
	if (cross_manager) {
		CloseSourceRuntime(fixture);
	}
	clone.Progress(0);
	FinishReceivingOwners(fixture, destination.get(), source, clone);
}

static void TestNestedSelectedParticleClone(bool cross_manager) {
	ParticleSceneFixture fixture(false, false, false, false, 64, false, 0, CloneDocument(0, true));
	auto source = fixture.Attach();
	source.SetVisible(true);
	source.Play("main");
	source.Progress(0);
	source.Progress(1);
	Require(fixture.Runtime->Identifiers().LiveCount() == 4, "genuine emitted kind3 owner must contain its actual group2 and ordinary0 selected descendants");
	const auto expected = SnapshotTree(source);
	const auto random = source.Random().State();
	std::unique_ptr<ParticleCloneDestination> destination;
	if (cross_manager) {
		destination = std::make_unique<ParticleCloneDestination>(fixture);
	}
	auto& runtime = destination ? *destination->Runtime : *fixture.Runtime;
	auto& tasks = destination ? destination->Tasks : fixture.Tasks;
	auto clone = sgre::scene::MotionPlayerTask::AttachClone(runtime, tasks, source, false, [](const auto&) {
	});
	RequireCopiedTree(expected, SnapshotTree(clone));
	Require(runtime.Identifiers().LiveCount() == (cross_manager ? 4 : 8) && clone.Random().State() == random, "recursive particle clone must retain each actual selected UID and random state");
	tasks.Group(sgre::scene::MotionTaskGroup::Second).Draw(0);
	runtime.SceneContext().Clear();
	source.Progress(0.5f);
	clone.Progress(0.5f);
	RequireCopiedTree(SnapshotTree(source), SnapshotTree(clone));
	ReleaseTask(fixture.Tasks, source);
	if (cross_manager) {
		CloseSourceRuntime(fixture);
	}
	clone.Progress(0.5f);
	tasks.Group(sgre::scene::MotionTaskGroup::Second).Draw(0);
	const std::array<sgre::resource::MotionPoint, 2> sizes{{{4, 4}, {4, 4}}};
	Require(runtime.SceneContext().Prepare(ParticleSceneGlobals(sizes)).Queue.size() == 1, "source-first retained kind3 descendants must draw through their independent completed source/geometry owners");
	runtime.SceneContext().Clear();
	FinishReceivingOwners(fixture, destination.get(), source, clone);
}

int main() {
	try {
		for (std::int32_t mode = 0; mode < 3; ++mode) {
			TestGenuineBirthClone(mode, false);
			TestGenuineBirthClone(mode, true);
		}
		for (const auto cross_manager : {false, true}) {
			TestStoppedRetainedBeforePrune(cross_manager);
			TestEnteredScreenLatchSurvivesClone(cross_manager);
			TestNestedSelectedParticleClone(cross_manager);
		}
		std::cout << "Retained particle clone birth/rows/prune/screen/nested/source-first ownership PASS\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << "Retained particle clone failure: " << error.what() << '\n';
		return 1;
	}
}
