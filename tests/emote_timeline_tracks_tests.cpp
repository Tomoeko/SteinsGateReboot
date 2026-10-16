#include "sgre/scene/emote_timeline_tracks.hpp"

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

static void RequireNear(float actual, float expected, std::string_view message) {
	if (std::abs(actual - expected) > 0.0001f) {
		throw std::runtime_error(std::string(message) + ": expected " + std::to_string(expected) + ", actual " + std::to_string(actual));
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

static void TestNativePreparation() {
	ActualMotionFixture actual;
	auto controls = actual.Controls();
	auto metadata = Definition({Track("continuous", {Key(2, 10), Key(4, 10), Key(6, 10), Key(8, 10), Hidden(10)}), Track("instant", {Hidden(20)})}, 5);
	sgre::scene::EmoteTimelineTracks tracks(*controls, metadata);
	metadata = Atom(false);
	const auto& prepared = tracks.Definition();
	Require(prepared.LastTime == 20 && prepared.LoopBegin == 5 && prepared.LoopEnd == -1, "lastTime fallback must include actual hidden boundaries without normalizing loop metadata");
	Require(prepared.Tracks.size() == 2 && prepared.Tracks[1].Instant && !prepared.Tracks[0].Instant, "lazy preparation must snapshot actual instant membership for each named track");
	const auto& keys = prepared.Tracks[0].Keys;
	Require(keys.size() == 6 && keys[0].Time == 0 && keys[0].Hidden && keys[0].Value == 0 && keys[0].Easing == 0, "a positive first time must prepend the native all-zero hidden boundary");
	Require(!keys[1].Hidden && !keys[2].Hidden && keys[3].Hidden && !keys[4].Hidden && keys[5].Hidden, "loopBegin protection and sequential equal-value suppression differ");
	Require(actual.Parent.ParameterValues().LocalValueCount() == 0 && tracks.Position() == 0 && tracks.Cursors()[0] == 0, "preparation must own metadata without publishing or inventing a seek");
	RequireFailure([&] {
		tracks.CrossTo(0);
	},
	    "real seek");
	tracks.Seek(0);
	Require(controls->TransitionValue("continuous") == 0, "prepended hidden frame must not dispatch the first future value early");
	tracks.Seek(5);
	Require(controls->TransitionValue("continuous") == 10 && tracks.Cursors()[0] == 2, "seek must retain the actual key owning loopBegin and clamp negative remaining duration to zero");
}

static void TestSeekAndActualParameterPublication() {
	ActualMotionFixture actual;
	auto controls = actual.Controls();
	sgre::scene::EmoteTimelineTracks tracks(*controls, Definition({Track("continuous", {Key(0, 36), Key(6, 60), Hidden(10)})}));
	tracks.Seek(2);
	Require(controls->TransitionValue("continuous") == 0 && actual.ParentParameters->Entries()[0].Position == 0, "seek target must wait for actual control evaluation and plain Motion publication");
	controls->Update(1);
	RequireNear(controls->Read("continuous"), 12, "seek remaining duration must subtract target position and the native final tick");
	RequireNear(actual.ParentParameters->Entries()[0].Position, 12, "actual containing parameter was not published");
	RequireNear(actual.ChildParameters->Entries()[0].Position, 12, "actual selected child parameter fanout was bypassed");
	controls->Update(2);
	RequireNear(controls->Read("continuous"), 36, "seek transition did not complete at the native remaining duration");
	const auto calls = actual.GuardCalls;
	tracks.CrossTo(6, false);
	Require(actual.GuardCalls == calls && tracks.Cursors()[0] == 0, "exclusive crossing must leave a key exactly on the boundary undispatched");
	tracks.CrossTo(6);
	Require(tracks.Cursors()[0] == 1, "inclusive crossing did not acquire the boundary key");
	controls->Update(1);
	RequireNear(controls->Read("continuous"), 44, "crossed transition duration must use final supplied position, not key time alone");
	const auto after_visible = actual.GuardCalls;
	tracks.CrossTo(10);
	Require(actual.GuardCalls == after_visible && tracks.Cursors()[0] == 2, "a hidden terminal advances the real cursor without a second Set");
	controls->Update(2);
	RequireNear(actual.ChildParameters->Entries()[0].Position, 60, "hidden terminal must preserve the genuine active control transition");
	tracks.Seek(20);
	Require(controls->TransitionValue("continuous") == 60 && tracks.Cursors()[0] == 2, "seek past terminal must retain the last visible candidate before the final hidden key");
	RequireFailure([&] {
		tracks.CrossTo(19);
	},
	    "forward position");
	RequireFailure([&] {
		tracks.Seek(-1);
	},
	    "nonnegative");
	RequireFailure([&] {
		tracks.Seek(std::numeric_limits<float>::infinity());
	},
	    "finite");
	Require(actual.Cache.Count() == 0, "timeline evaluation must not allocate texture resources");
}

static void TestCrossedOrderAndPolicy() {
	ActualMotionFixture actual;
	auto controls = actual.Controls();
	const auto definition = Definition({Track("instant", {Key(0, 1), Key(2, 3), Key(4, 7), Hidden(6)}), Track("instant", {Key(0, 11), Key(4, 19), Hidden(6)}), Track("other", {Key(0, 23), Hidden(6)})});
	sgre::scene::EmoteTimelineTracks tracks(*controls, definition, 1);
	tracks.Seek(0, false);
	const auto calls = actual.GuardCalls;
	tracks.CrossTo(4);
	Require(actual.GuardCalls == calls + 3 && tracks.Cursors()[0] == 2 && tracks.Cursors()[1] == 1, "every crossed visible key and duplicate track must dispatch in actual serialized order");
	Require(controls->Read("instant") == 19 && actual.Parent.ParameterValues().Read("instant") == 0, "last duplicate track wins internally while real Motion publication stays deferred");
	controls->Update(0);
	Require(actual.ParentParameters->Entries()[2].Position == 19 && actual.ChildParameters->Entries()[2].Position == 19, "instant timeline output must pass through both real parameter registrations");
	const auto terminal_calls = actual.GuardCalls;
	tracks.CrossTo(6);
	Require(actual.GuardCalls == terminal_calls, "terminal hidden crossings must not create synthetic writes");

	sgre::scene::EmoteTimelineTracks skipped(*controls, definition, 4);
	controls->Set("instant", 31);
	skipped.Seek(0);
	Require(controls->Read("instant") == 31 && controls->TransitionValue("other") == 0, "skip-instant policy must skip entire instant tracks while normal track starts its genuine duration");
	Require(skipped.Cursors()[0] == 0 && skipped.Cursors()[1] == 0 && skipped.Cursors()[2] == 0, "skipped cursor storage must not acquire a fake seek result");
	const auto before_crossing = actual.GuardCalls;
	skipped.CrossTo(4);
	Require(actual.GuardCalls == before_crossing && skipped.Cursors()[0] == 0, "skip-instant crossing must not call ordinary setters for skipped tracks");
	controls->Update(5);
	RequireNear(controls->Read("other"), 23, "ordinary channel must remain active under skip-instant policy");
	const auto previous = controls->Read("instant");
	RequireFailure([&] {
		sgre::scene::EmoteTimelineTracks unknown(*controls, definition, 8);
	},
	    "flags");
	Require(controls->Read("instant") == previous, "unsupported timeline policy must reject before any control mutation");
}

static void TestMalformedInputAndFailurePhase() {
	ActualMotionFixture actual;
	auto controls = actual.Controls();
	RequireFailure([&] {
		sgre::scene::EmoteTimelineTracks tracks(*controls, Atom(false));
	},
	    "loopBegin");
	RequireFailure([&] {
		sgre::scene::EmoteTimelineTracks tracks(*controls, Definition({Track("continuous", {})}));
	},
	    "nonempty");
	RequireFailure([&] {
		sgre::scene::EmoteTimelineTracks tracks(*controls, Definition({Track("continuous", {Key(-1, 1), Hidden(2)})}));
	},
	    "ordered nonnegative");
	RequireFailure([&] {
		sgre::scene::EmoteTimelineTracks tracks(*controls, Definition({Track("continuous", {Key(2, 1), Hidden(1)})}));
	},
	    "ordered nonnegative");
	RequireFailure([&] {
		sgre::scene::EmoteTimelineTracks tracks(*controls, Definition({Track("continuous", {Key(0, 1), Key(2, 2)})}));
	},
	    "hidden terminal");
	RequireFailure([&] {
		sgre::scene::EmoteTimelineTracks tracks(*controls, Definition({Track("continuous", {Key(0, std::numeric_limits<double>::quiet_NaN()), Hidden(2)})}));
	},
	    "finite numeric");
	RequireFailure([&] {
		sgre::scene::EmoteTimelineTracks tracks(*controls, Definition({Track(std::string(65537, 'n'), {Hidden(0)})}));
	},
	    "name");
	const auto bad_type = Atom(sgre::resource::PsbObject{{"time", Atom(0.0)}, {"type", Atom(std::string("visible"))}});
	RequireFailure([&] {
		sgre::scene::EmoteTimelineTracks tracks(*controls, Definition({Track("continuous", {bad_type})}));
	},
	    "frame type");
	const auto fractional_hidden = Atom(sgre::resource::PsbObject{{"time", Atom(0.0)}, {"type", Atom(0.999999999)}});
	sgre::scene::EmoteTimelineTracks hidden(*controls, Definition({Track("continuous", {fractional_hidden})}));
	Require(hidden.Definition().Tracks[0].Keys[0].Hidden, "frame type must use native integer conversion rather than generic scalar truth");
	hidden.Seek(0);
	Require(actual.Parent.ParameterValues().LocalValueCount() == 0, "metadata errors or hidden-only seeks must not publish real Motion values");

	sgre::scene::EmoteTimelineTracks tracks(*controls, Definition({Track("instant", {Key(0, 1), Key(2, 9), Hidden(4)})}));
	tracks.Seek(0, false);
	actual.Guard = [] {
		throw std::runtime_error("actual containing scene is queued");
	};
	RequireFailure([&] {
		tracks.CrossTo(2);
	},
	    "queued");
	actual.Guard = {};
	RequireFailure([&] {
		tracks.Seek(0);
	},
	    "invalid after failed dispatch");
	Require(controls->Read("instant") == 0, "a rejected actual setter must not fabricate a successful timeline write");

	sgre::scene::EmoteTimelineTracks reentered(*controls, Definition({Track("instant", {Key(0, 1), Hidden(2)})}));
	actual.Guard = [&] {
		reentered.Seek(0);
	};
	RequireFailure([&] {
		reentered.Seek(0);
	},
	    "reentered");
	actual.Guard = {};
	RequireFailure([&] {
		reentered.CrossTo(0);
	},
	    "invalid after failed dispatch");
}

static void TestAssignedTracksRetainMetadataAndCompletedCache() {
	ActualMotionFixture source_motion;
	ActualMotionFixture destination_motion;
	auto source_controls = source_motion.Controls();
	auto destination_controls = destination_motion.Controls();
	sgre::scene::EmoteTimelineTracks source(*source_controls, Definition({Track("continuous", {Key(0, 12), Hidden(6)})}), 2);
	source.Seek(0);
	source.AdvanceDifference(1);
	sgre::scene::EmoteTimelineTracks destination(*destination_controls, Definition({Track("continuous", {Key(0, 99), Key(2, 100), Hidden(6)})}), 2);
	destination.Seek(0);
	destination.AdvanceDifference(.25f);
	const auto retained_cache = destination.ApplyDifference("continuous", 10, 1);
	const auto calls = destination_motion.GuardCalls;
	destination.AssignState(source, 3);
	Require(destination_motion.GuardCalls == calls && destination_controls->Read("continuous") == 0, "state import must not dispatch timeline keys or variable IO");
	Require(destination.Definition().Tracks.front().Keys.front().Value == 99 && destination.Cursors().front() == 0, "assignment retains destination metadata and recomputes its own cursor");
	Require(destination.ApplyDifference("continuous", 10, 1) == retained_cache, "assignment retains completed destination difference cache until positive advance");
	destination.AdvanceDifference(1);
	RequireNear(destination.ApplyDifference("continuous", 10, 1), 14.8f, "imported transition continues source elapsed/start/target rather than resetting to its current value");
	// A retained native transition exists independently of the current additive playback flag.
	source.Reset(0);
	source.Seek(1, false);
	destination.AssignState(source, 3);
	destination.AdvanceDifference(1);
	RequireNear(destination.ApplyDifference("continuous", 0, 1), 4.8f, "source flags0 must retain and copy its previously allocated difference transition");
}

static void TestAssignedTracksFirstSourceMatchAndFreshCache() {
	ActualMotionFixture source_motion;
	ActualMotionFixture destination_motion;
	auto source_controls = source_motion.Controls();
	auto destination_controls = destination_motion.Controls();
	auto source = std::make_unique<sgre::scene::EmoteTimelineTracks>(*source_controls, Definition({Track("continuous", {Key(0, 12), Hidden(6)}), Track("continuous", {Key(0, 90), Hidden(6)})}), 2);
	source->Seek(0);
	source->AdvanceDifference(1);
	sgre::scene::EmoteTimelineTracks destination(*destination_controls, Definition({Track("continuous", {Key(0, 77), Hidden(6)}), Track("continuous", {Key(0, 88), Hidden(6)})}));
	destination.AssignState(*source, 3);
	Require(destination.ApplyDifference("continuous", 5, 1) == 5, "fresh destination cache must not borrow source completed additive values");
	source.reset();
	source_controls.reset();
	destination.AdvanceDifference(1);
	RequireNear(destination.ApplyDifference("continuous", 0, 1), 9.6f, "each duplicate destination track must own a copy of the first matching source transition after source retirement");
}

static void TestPassRawKeyDurationAndRetainedCursor() {
	ActualMotionFixture actual;
	auto controls = actual.Controls();
	sgre::scene::EmoteTimelineTracks tracks(*controls, Definition({Track("continuous", {Key(0, 8), Key(2, 20), Key(4, 40), Hidden(6)})}));
	tracks.Seek(1, false);
	const auto cursor = tracks.Cursors().front();
	const auto guard_calls = actual.GuardCalls;
	tracks.Pass();
	Require(tracks.Position() == 1 && tracks.Cursors().front() == cursor && actual.GuardCalls == guard_calls + 2, "native pass dispatches both future visible keys without consuming the saved cursor or time");
	Require(controls->Read("continuous") == 0 && actual.Parent.ParameterValues().LocalValueCount() == 0, "pass changes actual transition targets without publishing a plain Motion phase");
	controls->Update(1);
	RequireNear(controls->Read("continuous"), 10, "pass uses raw final key time4 as duration, rather than crossed remaining duration or immediate completion");
	tracks.Pass();
	controls->Update(1);
	RequireNear(controls->Read("continuous"), 17.5f, "repeat ordinary pass redispatches from the unchanged cursor and current scalar value");
}

static void TestPassPlainInstantPolicyAndMetadataOrder() {
	ActualMotionFixture actual;
	auto controls = actual.Controls();
	sgre::scene::EmoteTimelineTracks tracks(*controls, Definition({Track("instant", {Key(0, 1), Key(2, 7), Hidden(4)}), Track("instant", {Key(0, 2), Key(2, 9), Hidden(4)})}), 4);
	tracks.Seek(0);
	Require(controls->Read("instant") == 0, "flags4 fixture must skip ordinary instant seek publication");
	tracks.Pass();
	Require(controls->Read("instant") == 9 && tracks.Cursors()[0] == 0 && tracks.Cursors()[1] == 0 && tracks.Position() == 0, "plain flags4 pass bypasses the normal instant-skip policy and preserves metadata dispatch order");
	controls->Set("instant", 33);
	tracks.Pass();
	Require(controls->Read("instant") == 9, "plain flags4 must not inherit the difference already-passed early return");
}

static void TestPassDifferenceOnceAndSubsequentInstantSuppression() {
	ActualMotionFixture actual;
	auto controls = actual.Controls();
	sgre::scene::EmoteTimelineTracks tracks(*controls, Definition({Track("continuous", {Key(0, 12), Key(2, 24), Hidden(6)}), Track("instant", {Key(0, 1), Key(2, 9), Hidden(6)})}), 2);
	tracks.Seek(0);
	tracks.AdvanceDifference(1);
	const auto retained = tracks.ApplyDifference("continuous", 10, 1);
	tracks.Pass();
	Require(controls->Read("instant") == 9 && controls->Read("continuous") == 0 && tracks.ApplyDifference("continuous", 10, 1) == retained, "difference pass dispatches instant keys only and neither advances nor replaces its retained additive state");
	controls->Set("instant", 33);
	tracks.Pass();
	Require(controls->Read("instant") == 33 && tracks.Cursors()[0] == 0 && tracks.Cursors()[1] == 0, "difference flags6 pass is an idempotent no-dispatch operation");
	tracks.CrossTo(2);
	Require(controls->Read("instant") == 33 && tracks.Position() == 2, "difference pass flag4 reaches the actual track owner and suppresses later instant crossing");
}

static void TestPassSeekAdmissionAndFailedActualDispatch() {
	ActualMotionFixture actual;
	auto controls = actual.Controls();
	sgre::scene::EmoteTimelineTracks tracks(*controls, Definition({Track("instant", {Key(0, 1), Key(2, 9), Hidden(4)})}));
	RequireFailure([&] {
		tracks.Pass();
	},
	    "prepared seek");
	tracks.Seek(0, false);
	actual.Guard = [] {
		throw std::runtime_error("actual containing pass guard");
	};
	RequireFailure([&] {
		tracks.Pass();
	},
	    "containing pass guard");
	actual.Guard = {};
	Require(controls->Read("instant") == 0, "a rejected actual setter must not fabricate the future key value");
	RequireFailure([&] {
		tracks.Pass();
	},
	    "invalid after failed dispatch");
}

int main() {
	try {
		TestPassRawKeyDurationAndRetainedCursor();
		TestPassPlainInstantPolicyAndMetadataOrder();
		TestPassDifferenceOnceAndSubsequentInstantSuppression();
		TestPassSeekAdmissionAndFailedActualDispatch();
		TestNativePreparation();
		TestSeekAndActualParameterPublication();
		TestCrossedOrderAndPolicy();
		TestMalformedInputAndFailurePhase();
		TestAssignedTracksRetainMetadataAndCompletedCache();
		TestAssignedTracksFirstSourceMatchAndFreshCache();
		std::cout << "Emote timeline preparation, native seek/crossing and actual Motion publication passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
