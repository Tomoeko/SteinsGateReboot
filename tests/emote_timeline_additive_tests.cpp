#include "sgre/scene/emote_timeline_scheduler.hpp"

#include "emote_timeline_additive_native_vectors.hpp"

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

static void ApplyExpected(sgre::scene::EmoteVariableControls& controls, const std::vector<NativeAdditiveWrite>& writes) {
	for (const auto& write : writes) {
		controls.Set(write.Name, std::bit_cast<float>(write.Value), std::bit_cast<float>(write.Duration), std::bit_cast<float>(write.Easing));
	}
}

static void TestOriginalDifferenceAndFadePublication() {
	for (const auto& native : kNativeAdditiveCases) {
		ActualMotionFixture actual;
		ActualMotionFixture oracle;
		auto controls = actual.Controls();
		auto expected = oracle.Controls();
		controls->Set("continuous", 7);
		const auto definition = Labeled("sequence", Definition({Track("continuous", {Key(0, 5), Key(2, 10), Key(4, 15), Hidden(6), Key(8, 25), Hidden(10)}), Track("instant", {Key(0, 6), Key(2, 11), Key(4, 16), Hidden(6), Key(8, 26), Hidden(10)})}, native.LoopBegin, native.LoopEnd, native.LastTime));
		const auto document = Document({definition});
		sgre::scene::EmoteTimelineScheduler scheduler(*controls, document, [] {
		});
		scheduler.Play("sequence", native.Flags);
		ApplyExpected(*expected, native.Initial);
		for (const auto& phase : native.Phases) {
			const auto delta = std::bit_cast<float>(phase.Delta);
			if (phase.HasFade) {
				scheduler.SetBlendRatio("sequence", std::bit_cast<float>(phase.FadeTarget), std::bit_cast<float>(phase.FadeDuration), 0, phase.Stop);
			}
			scheduler.Update(delta);
			ApplyExpected(*expected, phase.Writes);
			const auto state = scheduler.State("sequence");
			Require(state && state->Playing == phase.Active && state->Looped == phase.Looped && std::bit_cast<std::uint32_t>(state->Position) == phase.Position && std::bit_cast<std::uint32_t>(state->BlendRatio) == phase.Blend, "actual difference scheduler lifetime/loop/position/blend must equal original instruction output");
			Require(std::bit_cast<std::uint32_t>(scheduler.ApplyDifference("continuous", 7)) == phase.AddedToSeven, "true per-track cached difference and weighted active summation must equal original2baf30 exactly");
			controls->UpdateWithTimelines(delta, scheduler);
			expected->Update(delta);
			Require(controls->Read("continuous") == 7, "difference playback must preserve the genuine raw control value");
			Require(std::bit_cast<std::uint32_t>(actual.Parent.ParameterValues().Read("continuous")) == phase.AddedToSeven && std::bit_cast<std::uint32_t>(actual.Child->ParameterValues().Read("continuous")) == phase.AddedToSeven, "final difference publication must reach both actual registered parameter owners without overwriting base state");
			Require(controls->Read("instant") == expected->Read("instant"), "instant tracks retain their ordinary Set route or are skipped by flag4, never becoming additive transitions");
		}
	}
}

static void TestRealDifferenceReplayParallelAndFadeOwners() {
	ActualMotionFixture actual;
	auto controls = actual.Controls();
	controls->Set("continuous", 7);
	const auto document = Document({Labeled("first", Definition({Track("continuous", {Key(0, 5), Hidden(2)})})), Labeled("second", Definition({Track("continuous", {Key(0, 3), Hidden(2)})}))});
	sgre::scene::EmoteTimelineScheduler scheduler(*controls, document, [] {
	});
	scheduler.Play("first", 2);
	scheduler.Update(1);
	Require(scheduler.ApplyDifference("continuous", 7) == 12, "actual first transition reaches its authored additive value");
	scheduler.Play("first", 2);
	scheduler.Update(0);
	Require(scheduler.ApplyDifference("continuous", 7) == 12, "native Reset clears the controller but preserves the last completed per-track cache until a positive scheduler phase");
	scheduler.Update(.25f);
	Require(scheduler.ApplyDifference("continuous", 7) == 8.25f, "actual replay advances from zero controller state rather than retained cache");
	scheduler.Play("second", 3);
	scheduler.Update(.75f);
	Require(scheduler.ActiveCount() == 2 && scheduler.ApplyDifference("continuous", 7) == 14.25f, "parallel difference owners add each cached contribution after its own genuine transition phase");
	scheduler.Stop("second");
	Require(scheduler.ApplyDifference("continuous", 7) == 12, "stopping an occurrence immediately removes its contribution while retaining the prepared controller");
	scheduler.Stop();
	scheduler.FadeIn("first", 1);
	Require(scheduler.State("first")->Flags == 3 && scheduler.State("first")->Playing, "native inactive fade-in creates a real parallel difference occurrence");
	scheduler.Update(.25f);
	Require(scheduler.State("first")->BlendRatio == .25f && scheduler.ApplyDifference("continuous", 7) == 7.3125f, "native fade-in begins from real zero blend and advancing authored difference controller");
	scheduler.FadeOut("first", .5f);
	scheduler.Update(.25f);
	Require(scheduler.IsPlaying("first") && scheduler.State("first")->BlendRatio == .125f, "half-finished fade retains its genuine active occurrence");
	scheduler.Update(.25f);
	Require(!scheduler.IsPlaying("first") && scheduler.ApplyDifference("continuous", 7) == 7, "completed fade-out retires its occurrence before final raw-plus-additive publication");

	ActualMotionFixture foreign;
	auto other_controls = foreign.Controls();
	RequireFailure([&] {
		other_controls->UpdateWithTimelines(1, scheduler);
	},
	    "exact actual control owner");
}

static void TestNamedBlendThroughActualParameters() {
	ActualMotionFixture actual;
	auto controls = actual.Controls();
	controls->Set("continuous", 7);
	const auto document = Document({Labeled("sequence", Definition({Track("continuous", {Key(0, 8), Key(2, 8), Hidden(100)})}, -1, -1, 100))});
	sgre::scene::EmoteTimelineScheduler scheduler(*controls, document, [] {
	});
	scheduler.Play("sequence", 2);
	scheduler.Update(1);
	controls->UpdateWithTimelines(1, scheduler);
	Require(actual.Parent.ParameterValues().Read("continuous") == 15 && actual.Child->ParameterValues().Read("continuous") == 15, "actual difference track must first publish its original eight-unit contribution");
	scheduler.SetBlendRatio("sequence", 0);
	scheduler.SetBlendRatio("sequence", 1, 2, 101);
	scheduler.Update(1);
	Require(scheduler.State("sequence")->BlendRatio == .25f, "named quadratic ratio must advance on the actual timeline blend owner");
	controls->UpdateWithTimelines(1, scheduler);
	Require(controls->Read("continuous") == 7 && actual.Parent.ParameterValues().Read("continuous") == 9 && actual.Child->ParameterValues().Read("continuous") == 9 && actual.ChildParameters->Entries()[0].ControlFlags == 0, "named timeline blend must retain the raw scalar and publish its exact weighted difference through real parent/child cells");
	scheduler.Update(1);
	controls->UpdateWithTimelines(1, scheduler);
	Require(scheduler.State("sequence")->BlendRatio == 1 && actual.Parent.ParameterValues().Read("continuous") == 15 && actual.Child->ParameterValues().Read("continuous") == 15, "named timeline blend completion must retain the genuine active difference owner");
	scheduler.SetBlendRatio("sequence", 0);
	scheduler.SetBlendRatio("sequence", 1, 2, 130);
	scheduler.Update(1);
	Require(std::bit_cast<std::uint32_t>(scheduler.State("sequence")->BlendRatio) == 0x3f8b39acu, "native back-out timeline overshoot must remain unclamped");
	controls->UpdateWithTimelines(1, scheduler);
	Require(actual.Parent.ParameterValues().Read("continuous") > 15 && actual.Child->ParameterValues().Read("continuous") == actual.Parent.ParameterValues().Read("continuous"), "actual overshooting blend must reach both parameter owners without a synthetic result clamp");
}

int main() {
	try {
		TestOriginalDifferenceAndFadePublication();
		TestRealDifferenceReplayParallelAndFadeOwners();
		TestNamedBlendThroughActualParameters();
		std::cout << "original difference/fade scheduling and actual parameter publication passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
