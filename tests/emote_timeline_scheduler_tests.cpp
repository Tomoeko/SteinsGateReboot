#include "sgre/scene/emote_timeline_scheduler.hpp"

#include "emote_timeline_scheduler_native_vectors.hpp"

#include <bit>
#include <thread>

#include "sgre/scene/emote_variable_controls.hpp"
#include "sgre/scene/motion_player_commands.hpp"

#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view fragment) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(fragment) != std::string_view::npos, error.what());
		return;
	}
	throw std::runtime_error("Emote timeline admitted malformed metadata or an unsupported actual phase");
}

template <typename Value>
static sgre::resource::PsbValue Atom(Value value) {
	sgre::resource::PsbValue result;
	result.Data = std::move(value);
	return result;
}

static sgre::resource::PsbValue Key(double time, double value, double easing = 0) {
	return Atom(sgre::resource::PsbObject{{"time", Atom(time)}, {"type", Atom(std::int64_t{1})}, {"content", Atom(sgre::resource::PsbObject{{"value", Atom(value)}, {"easing", Atom(easing)}})}});
}

static sgre::resource::PsbValue Hidden(double time) {
	return Atom(sgre::resource::PsbObject{{"time", Atom(time)}, {"type", Atom(std::int64_t{0})}});
}

static sgre::resource::PsbValue Track(std::string name, sgre::resource::PsbArray keys) {
	return Atom(sgre::resource::PsbObject{{"label", Atom(std::move(name))}, {"frameList", Atom(std::move(keys))}});
}

static sgre::resource::PsbValue Definition(sgre::resource::PsbArray tracks, double loop_begin = -1, double loop_end = -1, double last_time = -1) {
	return Atom(sgre::resource::PsbObject{{"loopBegin", Atom(loop_begin)}, {"loopEnd", Atom(loop_end)}, {"lastTime", Atom(last_time)}, {"variableList", Atom(std::move(tracks))}});
}

static sgre::resource::MotionTimeline PlainTimeline(std::string name) {
	sgre::resource::MotionTimeline result;
	result.Name = std::move(name);
	result.LastTime = 100;
	sgre::resource::PsbArray parameters;
	for (const auto* identifier : {"continuous", "other", "instant"}) {
		parameters.push_back(Atom(sgre::resource::PsbObject{{"id", Atom(std::string(identifier))}, {"discretization", Atom(false)}, {"rangeBegin", Atom(0.0)}, {"rangeEnd", Atom(100.0)}, {"division", Atom(100.0)}}));
	}
	result.Parameters = Atom(std::move(parameters));

	return result;
}

struct ActualMotionFixture {
	sgre::scene::TextureCache Cache;
	sgre::scene::MotionResourceManager Manager;
	sgre::scene::MotionPlayerCommands Parent;
	std::unique_ptr<sgre::scene::MotionTimelineParameters> ParentParameters;
	std::unique_ptr<sgre::scene::MotionPlayerCommands> Child;
	std::unique_ptr<sgre::scene::MotionTimelineParameters> ChildParameters;
	std::function<void()> Guard;
	std::size_t GuardCalls = 0;

	ActualMotionFixture()
	    : Cache([](auto, const auto&) -> sgre::render::SpriteTextureHandle {
		      throw std::runtime_error("Emote track fixture has no atlas or upload inputs");
	      },
	          [](auto) {
		          throw std::runtime_error("Emote track fixture has no texture retirement");
	          }),
	      Manager(Cache), Parent(Manager) {
		auto model = std::make_shared<sgre::resource::MotionResource>();
		sgre::resource::MotionObject character;
		character.Name = "character";
		character.Timelines.push_back(PlainTimeline("base"));
		character.Timelines.push_back(PlainTimeline("child"));
		model->Objects.push_back(std::move(character));
		sgre::scene::MotionProjectIdentity identity;
		identity.DocumentInstance = Cache.AllocateDocumentIdentity();
		identity.RootOffset = 44;
		(void)Manager.Register(identity, model, {});
		Parent.SetCharacter("character");
		Parent.Play("base");
		ParentParameters = std::make_unique<sgre::scene::MotionTimelineParameters>(Parent.ParameterValues(), *Parent.CachedTimeline());
		Child = std::make_unique<sgre::scene::MotionPlayerCommands>(Manager, Parent);
		Child->SetCharacter("character");
		Child->Play("child");
		ChildParameters = std::make_unique<sgre::scene::MotionTimelineParameters>(Child->ParameterValues(), *Child->CachedTimeline());
	}

	std::unique_ptr<sgre::scene::EmoteVariableControls> Controls() {
		sgre::scene::EmoteVariableProgram program;
		program.Transitions = {"continuous", "other"};
		program.Immediate = {"instant"};
		program.InstantVariables = {"instant"};

		return std::make_unique<sgre::scene::EmoteVariableControls>(
		    Parent, [this] {
			    ++GuardCalls;
			    if (Guard) {
				    Guard();
			    }
		    },
		    std::move(program));
	}
};

static sgre::resource::PsbValue Labeled(std::string label, sgre::resource::PsbValue definition) {
	std::get<sgre::resource::PsbObject>(definition.Data).emplace_back("label", Atom(std::move(label)));
	return definition;
}

static sgre::resource::PsbDocument Document(sgre::resource::PsbArray timelines) {
	sgre::resource::PsbDocument result;
	result.Root = Atom(sgre::resource::PsbObject{{"metadata", Atom(sgre::resource::PsbObject{{"timelineControl", Atom(std::move(timelines))}})}});
	return result;
}

static void ApplyExpected(sgre::scene::EmoteVariableControls& expected, const std::vector<NativeTimelineWrite>& writes) {
	for (const auto& write : writes) {
		expected.Set(write.Name, std::bit_cast<float>(write.Value), std::bit_cast<float>(write.Duration), std::bit_cast<float>(write.Easing));
	}
}

static void TestOriginalSchedulerStatesAndRealControlPublication() {
	for (const auto& native : kNativeTimelineCases) {
		ActualMotionFixture actual;
		ActualMotionFixture oracle;
		auto controls = actual.Controls();
		auto expected = oracle.Controls();
		const auto definition = Labeled("sequence", Definition({Track("continuous", {Key(0, 5), Key(2, 10), Key(4, 15), Hidden(6), Key(8, 25), Hidden(10)}), Track("instant", {Key(0, 6), Key(2, 11), Key(4, 16), Hidden(6), Key(8, 26), Hidden(10)})}, native.LoopBegin, native.LoopEnd, native.LastTime));
		const auto document = Document({definition});
		sgre::scene::EmoteTimelineScheduler scheduler(*controls, document, [] {
		});
		Require(scheduler.CatalogCount() == 1 && !scheduler.IsPlaying(), "native catalogue initialization must not activate an authored timeline");
		scheduler.Play("sequence", native.Flags);
		ApplyExpected(*expected, native.Initial);
		for (const auto& phase : native.Phases) {
			const auto delta = std::bit_cast<float>(phase.Delta);
			scheduler.Update(delta);
			ApplyExpected(*expected, phase.Writes);
			const auto state = scheduler.State("sequence");
			Require(state && state->Playing == phase.Active && state->Looped == phase.Looped && std::bit_cast<std::uint32_t>(state->Position) == phase.Position, "actual scheduler position/loop/lifetime must equal unchanged original instruction state");
			controls->Update(delta);
			expected->Update(delta);
			for (const auto* identifier : {"continuous", "instant"}) {
				Require(std::bit_cast<std::uint32_t>(controls->Read(identifier)) == std::bit_cast<std::uint32_t>(expected->Read(identifier)), "real actual channel must receive the original scheduler's value/duration/easing order");
				Require(actual.Parent.ParameterValues().Read(identifier) == oracle.Parent.ParameterValues().Read(identifier) && actual.Child->ParameterValues().Read(identifier) == oracle.Child->ParameterValues().Read(identifier), "scheduled values must reach both genuine parent/child registered parameters");
			}
		}
	}
}

static void TestPlayStopParallelAndNormalization() {
	ActualMotionFixture actual;
	auto controls = actual.Controls();
	const auto document = Document({Labeled("Ａ１", Definition({Track("instant", {Key(0, 5), Key(2, 7), Hidden(6)})})), Labeled("B2", Definition({Track("instant", {Key(0, 11), Key(2, 13), Hidden(6)})}))});
	sgre::scene::EmoteTimelineScheduler scheduler(*controls, document, [] {
	});
	scheduler.Play("A1");
	Require(scheduler.IsPlaying("Ａ１") && controls->Read("instant") == 5, "native normalization converts fullwidth ASCII while matching its genuine catalogue owner");
	scheduler.Play("B2", 1);
	Require(scheduler.ActiveCount() == 2 && controls->Read("instant") == 11, "parallel play retains its prior active occurrence and immediately dispatches the new timeline seek");
	scheduler.Play("Ａ１", 1);
	Require(scheduler.ActiveCount() == 2 && controls->Read("instant") == 5, "replay rewinds one existing occurrence without appending or moving it");
	scheduler.Update(2);
	Require(controls->Read("instant") == 13, "actual active order stays A1 then B2 after replay, so the second crossed owner wins");
	scheduler.Stop("B2");
	Require(scheduler.ActiveCount() == 1 && scheduler.IsPlaying("A1"), "stop removes only the exact normalized active name");
	scheduler.Play("unknown", 1);
	Require(scheduler.ActiveCount() == 1, "unknown parallel label leaves current actual players alive");
	scheduler.Play("unknown");
	Require(!scheduler.IsPlaying() && controls->Read("instant") == 13, "unknown nonparallel label stops actual players without inventing a variable reset");
	scheduler.Play("B2");
	scheduler.Stop("unknown");
	Require(scheduler.IsPlaying("B2"), "stop of an unknown actual label has no list mutation");
	scheduler.Stop();
	Require(!scheduler.IsPlaying() && scheduler.State("B2")->Position == 0, "stop-all erases active occurrences and retains the real prepared state");
	RequireFailure([&] {
		scheduler.Play("B2", 8);
	},
	    "flags exceed recovered");
	Require(!scheduler.IsPlaying(), "unsupported difference mode must reject before partial active-list mutation");
}

static void TestLazyAdmissionAndContainingGuards() {
	ActualMotionFixture actual;
	auto controls = actual.Controls();
	const auto document = Document({Labeled("good", Definition({Track("instant", {Key(0, 5), Hidden(6)})})), Labeled("unprepared", Atom(sgre::resource::PsbObject{}))});
	bool queued = false;
	sgre::scene::EmoteTimelineScheduler scheduler(*controls, document, [&] {
		if (queued) {
			throw std::runtime_error("actual scene input queued");
		}
	});
	scheduler.Play("good");
	queued = true;
	RequireFailure([&] {
		scheduler.Update(1);
	},
	    "queued");
	queued = false;
	Require(scheduler.State("good")->Position == 0, "rejected actual scene borrow must not advance schedule state");
	RequireFailure([&] {
		scheduler.Update(-1);
	},
	    "nonnegative");
	scheduler.Update(1);
	Require(scheduler.State("good")->Position == 1, "precondition rejection leaves the genuine scheduler usable");
	RequireFailure([&] {
		scheduler.Play("unprepared");
	},
	    "loopBegin");
	RequireFailure([&] {
		scheduler.Update(1);
	},
	    "idle valid");

	const auto loop_document = Document({Labeled("loop", Definition({Track("instant", {Key(0, 9), Hidden(1)})}, 0, 0, 1))});
	sgre::scene::EmoteTimelineScheduler invalid_loop(*controls, loop_document, [] {
	});
	invalid_loop.Play("loop");
	RequireFailure([&] {
		invalid_loop.Update(1);
	},
	    "positive native interval");
	Require(invalid_loop.State("loop")->Position == 0, "native unbounded zero-loop input must reject before any crossed output");

	const auto valid_document = Document({Labeled("valid", Definition({Track("instant", {Key(0, 9), Hidden(1)})}, 0, 1, 1))});
	sgre::scene::EmoteTimelineScheduler bounded(*controls, valid_document, [] {
	});
	bounded.Play("valid");
	RequireFailure([&] {
		bounded.Update(1000000);
	},
	    "bounded crossed-loop work");
	bool thread_rejected = false;
	std::thread foreign([&] {
		try {
			bounded.Update(1);
		} catch (const std::runtime_error&) {
			thread_rejected = true;
		}
	});
	foreign.join();
	Require(thread_rejected && bounded.State("valid")->Position == 0, "foreign thread must not consume the actual task's scheduler phase");

	sgre::scene::EmoteTimelineScheduler* reentry = nullptr;
	sgre::scene::EmoteTimelineScheduler reentered(*controls, valid_document, [] {
	});
	reentry = &reentered;
	actual.Guard = [&] {
		reentry->Stop();
	};
	RequireFailure([&] {
		reentered.Play("valid");
	},
	    "idle valid");
	actual.Guard = {};
	RequireFailure([&] {
		reentered.Update(0);
	},
	    "idle valid");
}

static void TestAssignedSchedulerRetainsCatalogAndSourceOrder() {
	ActualMotionFixture source_motion;
	ActualMotionFixture destination_motion;
	auto source_controls = source_motion.Controls();
	auto destination_controls = destination_motion.Controls();
	const auto source_document = Document({Labeled("A", Definition({Track("instant", {Key(0, 5), Key(2, 7), Hidden(8)})})), Labeled("B", Definition({Track("instant", {Key(0, 15), Key(2, 17), Hidden(8)})})), Labeled("missing", Definition({Track("instant", {Key(0, 25), Hidden(8)})}))});
	const auto destination_document = Document({Labeled("A", Definition({Track("instant", {Key(0, 6), Key(2, 9), Hidden(8)})})), Labeled("B", Definition({Track("instant", {Key(0, 16), Key(2, 19), Hidden(8)})})), Labeled("dormant", Definition({Track("instant", {Key(0, 26), Hidden(8)})}))});
	sgre::scene::EmoteTimelineScheduler source(*source_controls, source_document, [] {
	});
	sgre::scene::EmoteTimelineScheduler destination(*destination_controls, destination_document, [] {
	});
	source.Play("B");
	source.Play("A", 1);
	source.Play("missing", 1);
	source.Update(1);
	destination.Play("dormant");
	destination.Update(1);
	destination_controls->Set("instant", 77);
	destination.AssignState(source);
	Require(destination.ActiveCount() == 2 && destination.CatalogCount() == 3 && !destination.State("missing"), "assignment clears active playback, skips unknown destination labels and retains the destination catalogue");
	Require(destination.State("dormant")->Position == 1 && !destination.State("dormant")->Playing, "inactive prepared destination entries retain their owned playback state");
	Require(destination.State("B")->Flags == 1 && destination.State("A")->Flags == 1 && destination_controls->Read("instant") == 77, "native assign ORs parallel flag without dispatching its seek");
	destination.Update(1);
	Require(destination_controls->Read("instant") == 9, "source active order B thenA must use destination keys and retain A as the final writer");
}

static void TestAssignedSchedulerBlendAndSourceRetirement() {
	ActualMotionFixture source_motion;
	ActualMotionFixture destination_motion;
	auto source_controls = source_motion.Controls();
	auto destination_controls = destination_motion.Controls();
	auto source_document = std::make_unique<sgre::resource::PsbDocument>(Document({Labeled("loop", Definition({Track("continuous", {Key(0, 12), Hidden(3)})}, 0, 3, 9))}));
	const auto destination_document = Document({Labeled("loop", Definition({Track("continuous", {Key(0, 90), Hidden(3)})}, 0, 3, 9))});
	auto source = std::make_unique<sgre::scene::EmoteTimelineScheduler>(*source_controls, *source_document, [] {
	});
	sgre::scene::EmoteTimelineScheduler destination(*destination_controls, destination_document, [] {
	});
	source->Play("loop", 3);
	source->Update(4);
	source->FadeOut("loop", 4);
	source->Update(1);
	Require(source->State("loop")->Looped && source->State("loop")->BlendRatio == .75f, "source fixture must own a looped timeline and an in-flight fade");
	destination.AssignState(*source);
	Require(!destination.State("loop")->Looped && destination.State("loop")->Position == 2 && destination.BlendRatio("loop") == .75f, "assignment resets looped but preserves source position, cached blend and in-flight transition");
	source.reset();
	source_document.reset();
	source_controls.reset();
	destination.Update(.5f);
	Require(destination.BlendRatio("loop") == .625f && destination.IsPlaying("loop"), "destination owns the complete blend transition after source scheduler/document retirement");
	destination.Update(3);
	Require(!destination.IsPlaying("loop"), "copied stop-when-fade-finished policy retires genuine destination playback");
}

static void TestAnimatingTimelineLifetimeAndBlend() {
	ActualMotionFixture actual;
	auto controls = actual.Controls();
	const auto document = Document({Labeled("finite", Definition({}, -1, -1, 3)), Labeled("loop", Definition({}, 0, 4, 4))});
	sgre::scene::EmoteTimelineScheduler scheduler(*controls, document, [] {
	});
	Require(!scheduler.Animating(), "empty actual scheduler and settled controls are not animating");
	scheduler.Play("finite");
	Require(scheduler.Animating(), "native negative loopBegin counts even an active finite timeline without tracks");
	scheduler.Update(0);
	Require(scheduler.Animating() && scheduler.State("finite")->Position == 0, "animation query and zero elapsed must not consume finite timeline time");
	scheduler.Update(3);
	Require(!scheduler.Animating() && !scheduler.IsPlaying(), "finite timeline retirement removes its animation contribution");
	scheduler.Play("loop", 3);
	Require(scheduler.IsPlaying() && !scheduler.Animating(), "native steady looping playback alone does not count as animation");
	scheduler.SetBlendRatio("loop", .25f, 2);
	Require(scheduler.Animating(), "actual in-flight loop blend counts as animation");
	scheduler.Update(2);
	Require(scheduler.IsPlaying() && !scheduler.Animating() && scheduler.BlendRatio("loop") == .25f, "completed blend retains looping playback without retaining its animation predicate");
	scheduler.FadeOut("loop", 2);
	Require(scheduler.Animating(), "pending fade-out counts before its genuine update");
	scheduler.Update(2);
	Require(!scheduler.Animating() && !scheduler.IsPlaying(), "stop-when-blend-finished retires the actual loop and its coverage");
}

static void TestAnimatingExactTrackCoverageAndMembership() {
	ActualMotionFixture actual;
	auto controls = actual.Controls();
	const auto document = Document({Labeled("covered", Definition({Track("continuous", {Hidden(0), Hidden(4)}), Track("continuous", {Hidden(0), Hidden(4)})}, 0, 4, 4)), Labeled("wrong-case", Definition({Track("Continuous", {Hidden(0), Hidden(4)})}, 0, 4, 4))});
	sgre::scene::EmoteTimelineScheduler scheduler(*controls, document, [] {
	});
	controls->Set("continuous", 20, 8);
	Require(scheduler.Animating(), "uncovered dedicated scalar transition counts without active timelines");
	scheduler.Play("covered");
	const auto retained = controls->Read("continuous");
	const auto published = actual.Parent.ParameterValues().Read("continuous");
	for (std::size_t query = 0; query != 10; ++query) {
		Require(!scheduler.Animating(), "all active track labels mask their exact variable even when every key is hidden");
	}
	Require(controls->Read("continuous") == retained && actual.Parent.ParameterValues().Read("continuous") == published && scheduler.State("covered")->Position == 0, "repeated animation queries must not advance, publish or dispatch timeline variables");
	scheduler.Stop("covered");
	Require(scheduler.Animating(), "stopping a masking timeline invalidates cached names");
	scheduler.Play("wrong-case");
	Require(scheduler.Animating(), "native variable-name coverage is case-sensitive and does not normalize labels");
	scheduler.Play("covered", 1);
	Require(!scheduler.Animating(), "parallel timeline membership contributes the union of exact track names");
	scheduler.Play("unknown");
	Require(scheduler.Animating() && !scheduler.IsPlaying(), "unknown nonparallel playback clears prior coverage");
	scheduler.Play("covered");
	Require(!scheduler.Animating(), "replay rebuilds coverage from retained prepared destination definitions");
	scheduler.Stop();
	controls->Update(8);
	Require(!scheduler.Animating(), "completed uncovered scalar stops contributing after its genuine control update");
}

static void TestAnimatingInstantPolicyAndAssignmentCoverage() {
	ActualMotionFixture source_motion;
	ActualMotionFixture destination_motion;
	sgre::scene::EmoteVariableProgram program;
	program.Transitions = {"continuous", "other"};
	program.InstantVariables = {"continuous"};
	sgre::scene::EmoteVariableControls source_controls(source_motion.Parent, [] {
	},
	    program);
	sgre::scene::EmoteVariableControls destination_controls(destination_motion.Parent, [] {
	},
	    program);
	const auto source_document = Document({Labeled("shared", Definition({Track("continuous", {Hidden(0), Hidden(4)})}, 0, 4, 4))});
	const auto destination_document = Document({Labeled("shared", Definition({Track("other", {Hidden(0), Hidden(4)})}, 0, 4, 4)), Labeled("cover", Definition({Track("continuous", {Hidden(0), Hidden(4)})}, 0, 4, 4))});
	auto source = std::make_unique<sgre::scene::EmoteTimelineScheduler>(source_controls, source_document, [] {
	});
	sgre::scene::EmoteTimelineScheduler destination(destination_controls, destination_document, [] {
	});
	source->Play("shared", 4);
	source_controls.Set("continuous", 30, 8);
	Require(!source->Animating(), "native query includes an instant-policy track even when flags skip its dispatch");
	destination.Play("cover");
	destination_controls.Set("continuous", 30, 8);
	Require(!destination.Animating(), "destination fixture primes its previous exact coverage cache");
	destination.AssignState(*source);
	Require(destination.IsPlaying("shared") && destination.Animating(), "assignment replaces cached membership with destination prepared track names rather than source names");
	destination_controls.Set("continuous", 0);
	destination_controls.Set("other", 40, 8);
	Require(!destination.Animating(), "assigned destination track metadata masks its own exact dedicated control");
	source.reset();
	Require(!destination.Animating(), "coverage views remain owned by destination after source scheduler retirement");
	destination.Stop();
	Require(destination.Animating(), "assigned mask disappears on real stop-all");
}

static void TestAnimatingContainingGuardAndThread() {
	ActualMotionFixture actual;
	auto controls = actual.Controls();
	const auto document = Document({});
	bool blocked = false;
	sgre::scene::EmoteTimelineScheduler scheduler(*controls, document, [&] {
		if (blocked) {
			throw std::runtime_error("actual containing query guard");
		}
	});
	blocked = true;
	RequireFailure([&] {
		(void)scheduler.Animating();
	},
	    "containing query guard");
	blocked = false;
	bool rejected = false;
	std::thread foreign([&] {
		try {
			(void)scheduler.Animating();
		} catch (const std::runtime_error&) {
			rejected = true;
		}
	});
	foreign.join();
	Require(rejected && !scheduler.Animating(), "foreign-thread queries reject while retaining genuine owner state");
}

int main() {
	try {
		TestAnimatingTimelineLifetimeAndBlend();
		TestAnimatingExactTrackCoverageAndMembership();
		TestAnimatingInstantPolicyAndAssignmentCoverage();
		TestAnimatingContainingGuardAndThread();
		TestOriginalSchedulerStatesAndRealControlPublication();
		TestPlayStopParallelAndNormalization();
		TestLazyAdmissionAndContainingGuards();
		TestAssignedSchedulerRetainsCatalogAndSourceOrder();
		TestAssignedSchedulerBlendAndSourceRetirement();
		std::cout << "original timeline scheduler states and actual control/parameter publication passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
