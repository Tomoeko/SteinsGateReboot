#include "motion_particle_scene_fixture.hpp"

static sgre::resource::PsbValue& RowsProperty(sgre::resource::PsbValue& value, std::string_view name) {
	for (auto& [key, property] : std::get<sgre::resource::PsbObject>(value.Data)) {
		if (key == name) {
			return property;
		}
	}
	throw std::runtime_error("particle rows fixture requires its actual existing PSB property");
}

static sgre::resource::PsbValue RowsEmitter(std::string label, std::int64_t count) {
	auto emitter = Layer(std::move(label), false);
	RowsProperty(emitter, "type") = Atom(std::int64_t{4});
	auto& fields = std::get<sgre::resource::PsbObject>(emitter.Data);
	fields.emplace_back("particle", Atom(std::int64_t{0}));
	fields.emplace_back("particleMaxNum", Atom(std::int64_t{16}));
	fields.emplace_back("particleInheritAngle", Atom(false));
	fields.emplace_back("particleDeleteOutsideScreen", Atom(false));
	fields.emplace_back("particleTriVolume", Atom(false));
	fields.emplace_back("particleInheritOpacity", Atom(std::int64_t{1}));
	fields.emplace_back("particleInheritVelocity", Atom(std::int64_t{0}));
	fields.emplace_back("particleFlyDirection", Atom(std::int64_t{0}));
	fields.emplace_back("particleApplyZoomToVelocity", Atom(std::int64_t{0}));
	fields.emplace_back("particleAccelRatio", Atom(1.0));
	fields.emplace_back("particleMotionList", Atom(sgre::resource::PsbArray{Atom(sgre::resource::PsbArray{Atom(std::string("child")), Atom(std::string("main"))})}));
	const auto particle = Atom(sgre::resource::PsbObject{{"mask", Atom(std::int64_t{63})}, {"trigger", Atom(std::int64_t{1})}, {"fmin", Atom(static_cast<double>(count))}, {"fmax", Atom(static_cast<double>(count))}, {"vmin", Atom(0.0)}, {"vmax", Atom(0.0)}, {"amin", Atom(0.0)}, {"amax", Atom(0.0)}, {"zmin", Atom(1.0)}, {"zmax", Atom(1.0)}, {"range", Atom(0.0)}});
	const auto content = Atom(sgre::resource::PsbObject{{"mask", Atom(std::int64_t{0x100002})}, {"coord", Atom(sgre::resource::PsbArray{Atom(0.0), Atom(0.0), Atom(0.0)})}, {"prt", particle}});
	RowsProperty(emitter, "frameList") = Atom(sgre::resource::PsbArray{Atom(sgre::resource::PsbObject{{"time", Atom(0.0)}, {"type", Atom(std::int64_t{2})}, {"content", content}}), Atom(sgre::resource::PsbObject{{"time", Atom(2.0)}, {"type", Atom(std::int64_t{0})}, {"content", Atom(std::monostate{})}}), Atom(sgre::resource::PsbObject{{"time", Atom(8.0)}, {"type", Atom(std::int64_t{0})}, {"content", Atom(std::monostate{})}})});
	return emitter;
}

static std::shared_ptr<sgre::resource::PsbDocument> RowsDocument(bool containing_mask) {
	auto document = Document();
	auto& timeline = RowsProperty(RowsProperty(RowsProperty(RowsProperty(document->Root, "object"), "character"), "motion"), "main");
	auto first = RowsEmitter("first-emitter", 2);
	auto second = RowsEmitter("other-emitter", 1);
	sgre::resource::PsbArray layers;
	sgre::resource::PsbArray order;
	if (containing_mask) {
		auto mask = Layer("containing-mask", false);
		RowsProperty(mask, "type") = Atom(std::int64_t{12});
		RowsProperty(mask, "stencilType") = Atom(std::int64_t{5});
		std::get<sgre::resource::PsbObject>(mask.Data).emplace_back("stencilCompositeMaskLayerList", Atom(sgre::resource::PsbArray{}));
		RowsProperty(mask, "children") = Atom(sgre::resource::PsbArray{first});
		layers = {mask, second};
		order = {Atom(std::int64_t{0}), Atom(std::int64_t{1}), Atom(std::int64_t{1})};
	} else {
		layers = {first, second};
		order = {Atom(std::int64_t{0}), Atom(std::int64_t{0})};
	}
	RowsProperty(timeline, "layer") = Atom(std::move(layers));
	auto& priority = std::get<sgre::resource::PsbArray>(RowsProperty(timeline, "priority").Data);
	RowsProperty(priority.front(), "content") = Atom(std::move(order));
	return document;
}

static void CheckRepeatedRows(const sgre::scene::MotionSceneEvaluation& scene) {
	const auto rows = scene.GlobalRows();
	Require(rows.size() == 4 && rows[0] != rows[1] && rows[0] == rows[2] && rows[1] == rows[3], "each repeated priority occurrence must append the exact forward particle pair; the unrelated emitter must not contribute");
	for (const auto* row : rows) {
		const auto draw = scene.DrawState(*row);
		Require(row->Node().Prepared->Type == 0 && draw.Source != nullptr && draw.Numeric != nullptr && draw.Numeric->Active, "emitter output must retain each real selected child source and numeric owner, without its own drawing row");
	}
}

static void TestPriorityCapacityAndHiddenEmitter(bool containing_mask) {
	ParticleSceneFixture fixture(false, false, false, false, 64, false, 0, RowsDocument(containing_mask));
	auto task = fixture.Attach();
	task.SetVisible(true);
	task.Play("main");
	task.Progress(0);
	const auto* scene = task.GenericPlayer()->Scene();
	Require(scene != nullptr && scene->PacketRows().size() == (containing_mask ? 4 : 3), "packet graph must retain all three actual particle contexts, including the emitter absent from current draw priority");
	CheckRepeatedRows(*scene);
	if (containing_mask) {
		const sgre::scene::MotionStencilRowState* mask = nullptr;
		for (const auto* row : scene->PacketRows()) {
			if (row->Node().Prepared->Type == 12) {
				mask = row;
			}
		}
		Require(mask != nullptr && mask->Published(), "actual containing mask must publish its persistent local row");
		for (const auto* row : scene->GlobalRows()) {
			Require(row->Ancestor() == mask, "emitted selected rows must resolve the genuine containing stencil transfer through the actual particle owner");
		}
	}
	task.Progress(2);
	CheckRepeatedRows(*scene);
	Require(fixture.Events.size() == (containing_mask ? 4 : 3), "hidden emitter must continue publishing surviving rows without a second burst or duplicate child update");
	fixture.Tasks.Group(sgre::scene::MotionTaskGroup::Second).Draw(0);
	const std::array<sgre::resource::MotionPoint, 1> sizes{{{4, 4}}};
	const auto frame = fixture.Runtime->SceneContext().Prepare(ParticleSceneGlobals(sizes));
	Require(frame.Queue.size() == 4 && frame.Nodes.size() == (containing_mask ? 4 : 3), "normal manager publication must preserve repeated row occurrences while packet storage keeps every distinct child owner exactly once");
	RequireFailure([&] {
		task.Progress(0);
	},
	    "queued row borrows");
	fixture.Runtime->SceneContext().Clear();
	fixture.Tasks.Clear();
	Require(fixture.Runtime->Identifiers().LiveCount() == 0 && fixture.Runtime->CanClose(), "all actual rows, particles, containing links and packet borrows must retire before runtime closure");
}

int main() {
	try {
		TestPriorityCapacityAndHiddenEmitter(false);
		TestPriorityCapacityAndHiddenEmitter(true);
		std::cout << "Actual particle row priority/hidden/stencil/packet ownership PASS\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
