#include "motion_particle_scene_fixture.hpp"
#include "motion_particle_scene_native_vectors.hpp"

static sgre::resource::PsbValue& ParticleProperty(sgre::resource::PsbValue& value, std::string_view name) {
	for (auto& [key, property] : std::get<sgre::resource::PsbObject>(value.Data)) {
		if (key == name) {
			return property;
		}
	}
	throw std::runtime_error("particle fixture requires an actual existing PSB property");
}

static std::shared_ptr<sgre::resource::PsbDocument> ParticleDocument(std::int64_t maximum, std::int64_t trigger, float frequency, bool missing = false, bool nested = false) {
	auto document = Document();
	auto& characters = ParticleProperty(document->Root, "object");
	auto& timeline = ParticleProperty(ParticleProperty(ParticleProperty(characters, "character"), "motion"), "main");
	auto emitter = Layer("emitter", false);
	ParticleProperty(emitter, "type") = Atom(std::int64_t{4});
	auto& fields = std::get<sgre::resource::PsbObject>(emitter.Data);
	fields.emplace_back("particle", Atom(std::int64_t{0}));
	fields.emplace_back("particleMaxNum", Atom(maximum));
	fields.emplace_back("particleInheritAngle", Atom(false));
	fields.emplace_back("particleDeleteOutsideScreen", Atom(false));
	fields.emplace_back("particleTriVolume", Atom(false));
	fields.emplace_back("particleInheritOpacity", Atom(std::int64_t{1}));
	fields.emplace_back("particleInheritVelocity", Atom(std::int64_t{0}));
	fields.emplace_back("particleFlyDirection", Atom(std::int64_t{0}));
	fields.emplace_back("particleApplyZoomToVelocity", Atom(std::int64_t{0}));
	fields.emplace_back("particleAccelRatio", Atom(1.0));
	const auto target = Atom(sgre::resource::PsbArray{Atom(std::string(missing ? "absent" : "child")), Atom(std::string("main"))});
	fields.emplace_back("particleMotionList", Atom(sgre::resource::PsbArray{target}));
	const auto particle = Atom(sgre::resource::PsbObject{{"mask", Atom(std::int64_t{0x3f})}, {"trigger", Atom(trigger)}, {"fmin", Atom(static_cast<double>(frequency))}, {"fmax", Atom(static_cast<double>(frequency))}, {"vmin", Atom(0.0)}, {"vmax", Atom(0.0)}, {"amin", Atom(0.0)}, {"amax", Atom(0.0)}, {"zmin", Atom(1.0)}, {"zmax", Atom(1.0)}, {"range", Atom(0.0)}});
	const auto content = Atom(sgre::resource::PsbObject{{"mask", Atom(std::int64_t{0x100002})}, {"coord", Atom(sgre::resource::PsbArray{Atom(0.0), Atom(0.0), Atom(0.0)})}, {"prt", particle}});
	const auto first = Atom(sgre::resource::PsbObject{{"time", Atom(0.0)}, {"type", Atom(std::int64_t{2})}, {"content", content}});
	const auto hidden = Atom(sgre::resource::PsbObject{{"time", Atom(2.0)}, {"type", Atom(std::int64_t{0})}, {"content", Atom(std::monostate{})}});
	const auto end = Atom(sgre::resource::PsbObject{{"time", Atom(8.0)}, {"type", Atom(std::int64_t{0})}, {"content", Atom(std::monostate{})}});
	ParticleProperty(emitter, "frameList") = Atom(sgre::resource::PsbArray{first, hidden, end});
	ParticleProperty(timeline, "layer") = Atom(sgre::resource::PsbArray{emitter});
	auto& priority = std::get<sgre::resource::PsbArray>(ParticleProperty(timeline, "priority").Data);
	ParticleProperty(priority.front(), "content") = Atom(sgre::resource::PsbArray{Atom(std::int64_t{0})});
	if (nested) {
		// Genuine selected kind3 parent then emitted ordinary child, preserving ultimate parameter/UID ancestry.
		auto wrapper = Timeline(false);
		auto container = Layer("container", true);
		ParticleProperty(ParticleProperty(std::get<sgre::resource::PsbArray>(ParticleProperty(container, "frameList").Data).front(), "content"), "src") = Atom(std::string("emitter-owner"));
		ParticleProperty(wrapper, "layer") = Atom(sgre::resource::PsbArray{container});
		auto& wrapper_priority = std::get<sgre::resource::PsbArray>(ParticleProperty(wrapper, "priority").Data);
		ParticleProperty(wrapper_priority.front(), "content") = Atom(sgre::resource::PsbArray{Atom(std::int64_t{0})});
		const auto owner = Atom(sgre::resource::PsbObject{{"type", Atom(std::int64_t{0})}, {"motion", Atom(sgre::resource::PsbObject{{"main", timeline}})}});
		std::get<sgre::resource::PsbObject>(characters.Data).emplace_back("emitter-owner", owner);
		ParticleProperty(ParticleProperty(ParticleProperty(characters, "character"), "motion"), "main") = std::move(wrapper);
	}
	return document;
}

static void RequireParticleRows(const sgre::scene::MotionPlayerTask& task, std::size_t count) {
	const auto* scene = task.GenericPlayer()->Scene();
	Require(scene != nullptr && scene->PacketRows().size() == count && scene->GlobalRows().size() == count, "actual emitter contributes no own row and each surviving ordinary child contributes its genuine source row");
	for (const auto* row : scene->GlobalRows()) {
		const auto state = scene->DrawState(*row);
		Require(row->Node().Prepared->Type == 0 && state.Source != nullptr && state.Numeric != nullptr && state.Numeric->Active, "particle rows must resolve their actual selected source/numeric owner");
	}
}

static void TestBurstMaximumHiddenAndRetirement() {
	ParticleSceneFixture fixture(false, false, false, false, 64, false, 0, ParticleDocument(3, 1, 4));
	auto task = fixture.Attach();
	task.SetVisible(true);
	task.Play("main");
	auto expected = task.Random();
	for (std::size_t index = 0; index < 4; ++index) {
		static_cast<void>(expected.NextUnitFloat());
	}
	task.Progress(0);
	RequireParticleRows(task, 3);
	Require(task.Random().State() == expected.State() && fixture.Events.size() == 3 && fixture.Runtime->Identifiers().LiveCount() == 4, "burst samples target for each real construction, evicts the oldest before ticking, and preserves the containing random stream");
	fixture.Tasks.Group(sgre::scene::MotionTaskGroup::Second).Draw(0);
	const auto live = fixture.Runtime->Identifiers().LiveCount();
	RequireFailure([&] {
		task.Progress(1);
	},
	    "queued row borrows");
	Require(fixture.Runtime->Identifiers().LiveCount() == live, "queued actual rows must prevent child retirement or emitter mutation");
	fixture.Runtime->SceneContext().Clear();
	task.Progress(1);
	RequireParticleRows(task, 3);
	task.Progress(1);
	RequireParticleRows(task, 3);
	Require(fixture.Events.size() == 3, "hidden emitter retains surviving child rows without refiring its burst");
	task.Progress(6);
	Require(fixture.Runtime->Identifiers().LiveCount() == 4, "child stopping in the current native tick remains owned until the next pre-tick prune");
	task.Progress(0);
	RequireParticleRows(task, 0);
	Require(fixture.Runtime->Identifiers().LiveCount() == 1, "next update retires genuinely stopped particles while retaining the emitter node");
	fixture.Tasks.Clear();
	Require(fixture.Runtime->Identifiers().LiveCount() == 0 && fixture.Runtime->CanClose(), "actual task retirement releases emitted selected trees, contexts and preparation leases");
}

static void TestAutomaticAndFailedTargets() {
	for (const auto missing : {false, true}) {
		ParticleSceneFixture fixture(false, false, false, false, 64, false, 0, ParticleDocument(2, 0, 60, missing));
		auto task = fixture.Attach();
		task.SetVisible(true);
		task.Play("main");
		task.Progress(0);
		RequireParticleRows(task, 0);
		task.Progress(1);
		RequireParticleRows(task, missing ? 0 : 1);
		Require(fixture.Runtime->Identifiers().LiveCount() == (missing ? 1 : 2), "automatic cadence retains a genuine successful selection or prunes an actual failed manager lookup");
		fixture.Tasks.Clear();
		Require(fixture.Runtime->Identifiers().LiveCount() == 0, "automatic particle retirement must release all genuine selected identifiers");
	}
}

static void TestActualNativeSelectedPhases() {
	for (std::size_t index = 0; index < kNativeMotionParticlePhases.size(); ++index) {
		ParticleSceneFixture fixture(false, false, false, false, 64, false, 0, ParticleDocument(3, 1, 4), static_cast<std::uint32_t>(index) * 13);
		auto task = fixture.Attach();
		task.SetVisible(true);
		task.Play("main");
		for (const auto& expected : kNativeMotionParticlePhases[index]) {
			const auto events = fixture.Events.size();
			task.Progress(std::bit_cast<float>(expected.ElapsedBits));
			const auto* scene = task.GenericPlayer()->Scene();
			Require(scene && scene->PacketRows().size() == expected.LiveChildren && scene->GlobalRows().size() == expected.DrawRows, "actual selected child counts and forward recursive row counts must match original522ab0/50ad30");
			Require(fixture.Runtime->Identifiers().LiveCount() == expected.LiveChildren + 1 && fixture.Events.size() - events == expected.Events && task.Random().State() == expected.Random, "actual child lifetime, action delivery and containing random words must match unchanged original phases");
		}
		fixture.Tasks.Clear();
		Require(fixture.Runtime->Identifiers().LiveCount() == 0, "native phase replay must retire every genuine selected particle owner");
	}
}

static void TestActualCrossProjectSelectedSource() {
	auto document = ParticleDocument(2, 1, 1);
	auto& root_characters = ParticleProperty(document->Root, "object");
	auto& timeline = ParticleProperty(ParticleProperty(ParticleProperty(root_characters, "character"), "motion"), "main");
	auto& emitter = std::get<sgre::resource::PsbArray>(ParticleProperty(timeline, "layer").Data).front();
	ParticleProperty(emitter, "particleMotionList") = Atom(sgre::resource::PsbArray{Atom(sgre::resource::PsbArray{Atom(std::string("cross")), Atom(std::string("main"))})});
	ParticleSceneFixture fixture(false, false, false, false, 64, false, 0, document);
	auto other = Document();
	auto& characters = std::get<sgre::resource::PsbObject>(ParticleProperty(other->Root, "object").Data);
	for (auto& [name, value] : characters) {
		if (name == "child") {
			name = "cross";
		}
	}
	const auto other_project = fixture.RegisterDocument(other);
	auto task = fixture.Attach();
	task.SetVisible(true);
	task.Play("main");
	task.Progress(0);
	RequireParticleRows(task, 1);
	const auto* scene = task.GenericPlayer()->Scene();
	const auto state = scene->DrawState(*scene->GlobalRows().front());
	Require(task.Commands().SelectedProject()->Project == fixture.Project && state.RequestingProject == other_project && state.Source->State().Binding && state.Source->State().Binding->Project == fixture.Manager.Identity(other_project), "actual manager preferred-project miss must construct the genuine foreign selected/source owner without redirecting its containing player");
	fixture.Tasks.Group(sgre::scene::MotionTaskGroup::Second).Draw(0);
	const std::array<sgre::resource::MotionPoint, 2> sizes{{{4, 4}, {4, 4}}};
	const auto frame = fixture.Runtime->SceneContext().Prepare(ParticleSceneGlobals(sizes));
	Require(frame.Nodes.size() == 1 && frame.Queue.size() == 1, "foreign registered particle source must submit through the same genuine manager scene context");
	fixture.Runtime->SceneContext().Clear();
	fixture.Tasks.Clear();
	Require(fixture.Runtime->Identifiers().LiveCount() == 0 && fixture.Manager.ReferenceCount(other_project) == 1, "cross-project particle retirement adds no registration retains or leaked selected owners");
}

static void TestNestedEmittedPlayers() {
	auto document = ParticleDocument(2, 1, 2);
	auto& characters = ParticleProperty(document->Root, "object");
	const auto leaf = ParticleProperty(characters, "child");
	auto emitted = ParticleProperty(ParticleProperty(ParticleProperty(characters, "character"), "motion"), "main");
	auto& emitter = std::get<sgre::resource::PsbArray>(ParticleProperty(emitted, "layer").Data).front();
	ParticleProperty(emitter, "particleMotionList") = Atom(sgre::resource::PsbArray{Atom(sgre::resource::PsbArray{Atom(std::string("leaf")), Atom(std::string("main"))})});
	ParticleProperty(ParticleProperty(characters, "child"), "motion") = Atom(sgre::resource::PsbObject{{"main", emitted}});
	std::get<sgre::resource::PsbObject>(characters.Data).emplace_back("leaf", leaf);
	ParticleSceneFixture fixture(false, false, false, false, 64, false, 0, document);
	auto task = fixture.Attach();
	task.SetVisible(true);
	task.Play("main");
	task.Progress(0);
	RequireParticleRows(task, 4);
	Require(fixture.Runtime->Identifiers().LiveCount() == 7 && fixture.Events.size() == 4, "each actual emitted ordinary player must construct its own kind4 emitter, source rows and independent child vector");
	task.Progress(1);
	RequireParticleRows(task, 4);
	Require(fixture.Runtime->Identifiers().LiveCount() == 7 && fixture.Events.size() == 4, "native523252 requires a physical frame change; transferred color must not repeat nested births or their actions");
	fixture.Tasks.Clear();
	Require(fixture.Runtime->Identifiers().LiveCount() == 0, "nested actual particle trees must retire all selected owners and row borrows");
}

static void TestKind3ContainingParticle() {
	ParticleSceneFixture fixture(false, false, false, false, 64, false, 0, ParticleDocument(2, 1, 2, false, true));
	auto task = fixture.Attach();
	task.SetVisible(true);
	task.Play("main");
	task.Progress(0);
	const auto* scene = task.GenericPlayer()->Scene();
	Require(scene && scene->PacketRows().size() == 3 && scene->GlobalRows().size() == 2 && fixture.Runtime->Identifiers().LiveCount() == 4, "real kind3 selected owner must retain its own row while emitted descendants publish genuine ordinary geometry");
	fixture.Tasks.Group(sgre::scene::MotionTaskGroup::Second).Draw(0);
	const std::array<sgre::resource::MotionPoint, 1> sizes{{{4, 4}}};
	const auto frame = fixture.Runtime->SceneContext().Prepare(ParticleSceneGlobals(sizes));
	Require(frame.Nodes.size() == 3 && frame.Queue.size() == 2, "actual containing task must submit the two selected particle geometry rows through its normal manager context");
	fixture.Runtime->SceneContext().Clear();
	fixture.Tasks.Clear();
	Require(fixture.Runtime->Identifiers().LiveCount() == 0, "nested real child/particle tree must retire completely");
}

int main() {
	try {
		TestActualNativeSelectedPhases();
		TestBurstMaximumHiddenAndRetirement();
		TestAutomaticAndFailedTargets();
		TestKind3ContainingParticle();
		TestNestedEmittedPlayers();
		TestActualCrossProjectSelectedSource();
		std::cout << "Actual kind4 selected children/clock/rows/retirement PASS\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << "Actual kind4 selected owner failure: " << error.what() << '\n';
		return 1;
	}
}
