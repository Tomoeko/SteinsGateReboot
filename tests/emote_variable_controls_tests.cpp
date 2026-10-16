#include "sgre/scene/emote_variable_controls.hpp"

#include "sgre/scene/motion_player_commands.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
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
	throw std::runtime_error("Emote variable owner admitted a missing controller or active publication");
}

template <typename Value>
static sgre::resource::PsbValue Atom(Value value) {
	sgre::resource::PsbValue result;
	result.Data = std::move(value);
	return result;
}

static sgre::resource::PsbValue Enabled(std::string label) {
	return Atom(sgre::resource::PsbObject{{"enabled", Atom(true)}, {"label", Atom(std::move(label))}});
}

static sgre::resource::PsbDocument Metadata() {
	const auto option = [](std::string label, double on, double off) {
		return Atom(sgre::resource::PsbObject{{"label", Atom(std::move(label))}, {"lower", Atom(std::int64_t{0})}, {"upper", Atom(std::int64_t{0})}, {"onValue", Atom(on)}, {"offValue", Atom(off)}, {"imm", Atom(false)}});
	};
	const auto selector = Atom(sgre::resource::PsbObject{{"enabled", Atom(true)}, {"label", Atom(std::string("selection"))}, {"optionList", Atom(sgre::resource::PsbArray{option("visible", 100, 0), option("detail", 0, 1)})}});
	sgre::resource::PsbDocument document;
	document.Root = Atom(sgre::resource::PsbObject{{"metadata", Atom(sgre::resource::PsbObject{{"base", Atom(sgre::resource::PsbObject{{"chara", Atom(std::string("character"))}, {"motion", Atom(std::string("base"))}})}, {"transitionControl", Atom(sgre::resource::PsbArray{Enabled("visible"), Enabled("detail")})}, {"immediateControl", Atom(sgre::resource::PsbArray{Enabled("instant")})}, {"extensionVariableList", Atom(sgre::resource::PsbArray{Atom(std::string("extension")), Atom(std::string("ui-only")), Atom(std::string("visible"))})}, {"variableList", Atom(sgre::resource::PsbArray{Atom(sgre::resource::PsbObject{{"label", Atom(std::string("ui-only"))}})})}, {"customSelectorList", Atom(sgre::resource::PsbArray{selector})}})}});
	return document;
}

static sgre::resource::MotionTimeline Timeline(std::string name) {
	sgre::resource::MotionTimeline timeline;
	timeline.Name = std::move(name);
	timeline.LastTime = 100;
	timeline.Parameters = Atom(sgre::resource::PsbArray{Atom(sgre::resource::PsbObject{{"id", Atom(std::string("visible"))}, {"discretization", Atom(false)}, {"rangeBegin", Atom(0.0)}, {"rangeEnd", Atom(100.0)}, {"division", Atom(100.0)}})});
	return timeline;
}

static void TestActualPublicationPhase() {
	sgre::scene::TextureCache cache([](auto, const auto&) -> sgre::render::SpriteTextureHandle {
		throw std::runtime_error("variable publisher fixture has no texture inputs");
	},
	    [](auto) {
		    throw std::runtime_error("variable publisher fixture has no texture release");
	    });
	sgre::scene::MotionResourceManager manager(cache);
	auto model = std::make_shared<sgre::resource::MotionResource>();
	sgre::resource::MotionObject character;
	character.Name = "character";
	character.Timelines.push_back(Timeline("base"));
	character.Timelines.push_back(Timeline("child"));
	model->Objects.push_back(std::move(character));
	sgre::scene::MotionProjectIdentity identity;
	identity.DocumentInstance = cache.AllocateDocumentIdentity();
	identity.RootOffset = 44;
	const auto project = manager.Register(identity, model, {});
	sgre::scene::MotionPlayerCommands parent(manager);
	parent.SetCharacter("character");
	parent.Play("base");
	sgre::scene::MotionTimelineParameters parent_parameters(parent.ParameterValues(), *parent.CachedTimeline());
	sgre::scene::MotionPlayerCommands child(manager, parent);
	child.SetCharacter("character");
	child.Play("child");
	sgre::scene::MotionTimelineParameters child_parameters(child.ParameterValues(), *child.CachedTimeline());
	bool reject_mutation = false;
	std::size_t guard_calls = 0;
	sgre::scene::EmoteVariableControls controls(parent, [&] {
		++guard_calls;
		if (reject_mutation) {
			throw std::runtime_error("actual containing scene is queued");
		}
	},
	    sgre::scene::ReadEmoteVariableProgram(Metadata()));
	Require(parent.ParameterValues().LocalValueCount() == 0 && controls.Read("visible") == 0 && parent_parameters.Entries()[0].Position == 0, "selector construction must stage control values without writing plain Motion early");
	controls.Update(0);
	Require(controls.Read("visible") == 100 && parent_parameters.Entries()[0].Position == 100 && child_parameters.Entries()[0].Position == 100 && child_parameters.Entries()[0].ControlFlags == 0, "native dirty zero-delta control phase must publish exact selector values through actual Motion fanout");
	Require(controls.Read("detail") == 0 && controls.PublishedVariableCount() == 4 && parent.ParameterValues().Read("ui-only") == 0, "native extension dedup/UI existence or immediate channel lazy insertion differs");
	controls.Set("visible", 0, 4, 0);
	Require(controls.Read("visible") == 100 && parent_parameters.Entries()[0].Position == 100, "new transition target must not replace the last actually published value early");
	controls.Update(1);
	Require(controls.Read("visible") == 75 && child_parameters.Entries()[0].Position == 75, "actual one-tick linear transition publication differs");
	controls.Update(1);
	Require(controls.Read("visible") == 50, "native transition must advance from elapsed duration rather than replace start each tick");
	controls.Set("selection", 1.9f);
	controls.Update(0);
	Require(controls.Read("selection") == 1 && controls.Read("visible") == 0 && controls.Read("detail") == 1 && parent_parameters.Entries()[0].Position == 0, "native selector float truncation/control fanout differs");
	controls.Set("instant", 9, 12, 0);
	Require(controls.Read("instant") == 9 && parent.ParameterValues().Read("instant") == 0, "immediate Emote storage must still wait for actual Motion publication");
	controls.Update(0);
	Require(parent.ParameterValues().Read("instant") == 9, "native immediate storage was never published");
	controls.Set("no-controller", 88);
	controls.Update(0);
	Require(parent.ParameterValues().Read("no-controller") == 0, "unknown Emote variable must not invent a plain local channel");
	const auto current = parent.ParameterValues().Read("visible");
	reject_mutation = true;
	RequireFailure([&] {
		controls.Set("visible", 100);
	},
	    "queued");
	RequireFailure([&] {
		controls.Update(1);
	},
	    "queued");
	Require(parent.ParameterValues().Read("visible") == current && manager.ReferenceCount(project) == 1 && cache.Count() == 0 && guard_calls > 4, "containing guard must run before mutation without new resource/random/texture ownership");
	reject_mutation = false;
	controls.Set("selection", 0, 2);
	Require(controls.Read("selection") == 1 && controls.Read("visible") == 0 && parent_parameters.Entries()[0].Position == 0, "queued selector target must leave its prior selected and published values until real Update");
	controls.Update(1);
	Require(controls.Read("selection") == 0 && controls.Read("visible") == 50 && parent_parameters.Entries()[0].Position == 50 && child_parameters.Entries()[0].Position == 50 && child_parameters.Entries()[0].ControlFlags == 0, "selector must start before scalar transitions and publish its actual one-tick channel result through parent/child registrations");
	controls.Update(1);
	Require(controls.Read("visible") == 100 && child_parameters.Entries()[0].Position == 100, "selector-driven actual scalar transition did not complete on its second elapsed tick");
	controls.Set("selection", 1, 2, 101);
	Require(controls.Read("selection") == 0 && controls.Read("visible") == 100 && child_parameters.Entries()[0].Position == 100, "named selector job must retain the previous actual publication until its real update phase");
	controls.Update(1);
	Require(controls.Read("selection") == 1 && controls.Read("visible") == 75 && controls.Read("detail") == .25f && parent_parameters.Entries()[0].Position == 75 && child_parameters.Entries()[0].Position == 75 && child_parameters.Entries()[0].ControlFlags == 0, "native quadratic easing must reach scalar fanout through the real queued selector before publication");
	controls.Update(1);
	Require(controls.Read("visible") == 0 && controls.Read("detail") == 1 && child_parameters.Entries()[0].Position == 0, "queued named-easing selector must complete its two actual scalar transitions");
	controls.Set("visible", 100, 2, 102);
	controls.Update(1);
	Require(controls.Read("visible") == 75 && parent_parameters.Entries()[0].Position == 75 && child_parameters.Entries()[0].Position == 75, "direct scalar quadratic-out easing must publish the original ratio through genuine parameter registrations");
	controls.Update(1);
	Require(controls.Read("visible") == 100 && child_parameters.Entries()[0].Position == 100, "direct named scalar transition must complete at the original endpoint");
	RequireFailure([&] {
		controls.Update(-1);
	},
	    "nonnegative");
}

static void TestUnsupportedFamilyRejectsBeforeMutation() {
	auto program = sgre::scene::ReadEmoteVariableProgram(Metadata());
	program.UnsupportedFamilies.push_back("actual eye controller");
	sgre::scene::TextureCache cache([](auto, const auto&) -> sgre::render::SpriteTextureHandle {
		throw std::runtime_error("unsupported constructor must never upload");
	},
	    [](auto) {
	    });
	sgre::scene::MotionResourceManager manager(cache);
	sgre::scene::MotionPlayerCommands commands(manager);
	std::size_t called = 0;
	sgre::scene::EmoteVariableControls incomplete(commands, [&] {
		++called;
	},
	    std::move(program));
	Require(incomplete.MissingFamilies().size() == 1 && incomplete.TransitionValue("visible") == 100 && incomplete.TransitionValue("detail") == 0 && !incomplete.TransitionValue("missing"), "known native selector/channel initialization must survive an independently reported missing family");
	Require(incomplete.Read("visible") == 0 && commands.ParameterValues().LocalValueCount() == 0, "initialization diagnostics must not publish a partial pose to actual Motion");
	RequireFailure([&] {
		incomplete.Update(0);
	},
	    "actual eye controller");
	RequireFailure([&] {
		incomplete.Set("visible", 1);
	},
	    "actual eye controller");
	Require(called > 0 && commands.ParameterValues().LocalValueCount() == 0, "missing enabled-family update reached actual target mutation");
}

static void TestGraphAndMouthPublication() {
	sgre::scene::TextureCache cache([](auto, const auto&) -> sgre::render::SpriteTextureHandle {
		throw std::runtime_error("Emote graph publisher fixture has no texture input");
	},
	    [](auto) {
	    });
	sgre::scene::MotionResourceManager manager(cache);
	sgre::scene::MotionPlayerCommands commands(manager);
	sgre::scene::EmoteVariableProgram program;
	sgre::scene::EmoteGraphChannelDefinition eyebrow;
	eyebrow.Name = "eyebrow";
	eyebrow.InitialFrame = 2;
	eyebrow.Graph.Edges = {{0, 10}, {20, 30}};
	eyebrow.Graph.Nodes = {{10, 20}};
	program.Eyebrows.push_back(std::move(eyebrow));
	program.Mouths.push_back({"mouth", "talk", 3});
	sgre::scene::EmoteVariableControls controls(commands, [] {
	},
	    std::move(program));
	Require(controls.MissingFamilies().empty() && controls.TransitionValue("eyebrow") == 2 && controls.TransitionValue("mouth") == 3 && controls.TransitionValue("talk") == 0 && commands.ParameterValues().LocalValueCount() == 0, "native eyebrow/mouth initialization must precede actual Motion publication");
	controls.Update(1);
	Require(commands.ParameterValues().Read("eyebrow") == 2 && commands.ParameterValues().Read("mouth") == 3 && commands.ParameterValues().Read("talk") == 0, "actual graph/mouth control update did not publish through the real Motion values");
	controls.Set("eyebrow", 28, 4);
	controls.Set("talk", 8, 4);
	controls.Update(1);
	Require(controls.Read("eyebrow") == 6 && controls.Read("talk") == 2, "native graph-equivalent route and queued first talk step differ");
	controls.Set("talk", 8, 20);
	controls.Update(1);
	Require(controls.Read("eyebrow") == 10 && controls.Read("talk") == 4, "repeated talk target with a longer duration must preserve its actual transition");
	controls.Set("mouth", 7.9f, 4, 110);
	controls.Update(1);
	Require(controls.Read("eyebrow") == 24 && controls.Read("mouth") == 7 && controls.Read("talk") == 6, "mouth frame setter must truncate independently of talk easing/duration");
	controls.Update(1);
	Require(controls.Read("eyebrow") == 28 && controls.Read("talk") == 8, "native graph/talk completion differs");
	controls.Set("talk", 0, 2);
	controls.Update(1);
	Require(controls.Read("talk") == 4, "new talk transition did not start from the actual prior mouth value");
	controls.Set("talk", 3);
	controls.Update(0);
	Require(controls.Read("talk") == 3, "immediate talk must cancel the active native default transition");
}

static void TestPartialEyeInitialization() {
	sgre::scene::TextureCache cache([](auto, const auto&) -> sgre::render::SpriteTextureHandle {
		throw std::runtime_error("partial eye initialization must never upload");
	},
	    [](auto) {
	    });
	sgre::scene::MotionResourceManager manager(cache);
	sgre::scene::MotionPlayerCommands commands(manager);
	sgre::scene::EmoteVariableProgram program;
	sgre::scene::EmoteEyeDefinition eye;
	eye.Name = "eye";
	eye.LeftName = "eye-l";
	eye.RightName = "eye-r";
	eye.BlinkName = "blink-index";
	eye.InitialFrame = 4;
	eye.EndFrame = 10;
	program.Eyes.push_back(std::move(eye));
	sgre::scene::EmoteVariableControls controls(commands, [] {
	},
	    std::move(program));
	Require(controls.TransitionValue("eye") == 4 && controls.TransitionValue("eye-l") == 4 && controls.TransitionValue("eye-r") == 4 && controls.TransitionValue("blink-index") == -1, "eye constructor must copy all current frames and retain the actual absent-parameter index before publication");
	RequireFailure([&] {
		controls.Update(1);
	},
	    "actual entropy-word service");
	Require(commands.ParameterValues().LocalValueCount() == 0, "partial eye initialization falsely published a complete Emote pose");
}

static void TestRealEyePublication() {
	sgre::scene::TextureCache cache([](auto, const auto&) -> sgre::render::SpriteTextureHandle {
		throw std::runtime_error("eye controls have no texture inputs");
	},
	    [](auto) {
	    });
	sgre::scene::MotionResourceManager manager(cache);
	sgre::scene::MotionPlayerCommands commands(manager);
	sgre::scene::EmoteVariableProgram program;
	sgre::scene::EmoteEyeDefinition eye;
	eye.Name = "eye";
	eye.LeftName = "eye-l";
	eye.RightName = "eye-r";
	eye.BlinkName = "blink-index";
	eye.InitialFrame = 2;
	eye.EndFrame = 10;
	eye.Graph.Edges = {{0, 10}};
	eye.BlinkParameterIndex = 1;
	eye.Blink.IntervalMinimum = 2;
	eye.Blink.IntervalMaximum = 2;
	eye.Blink.CloseTicks = 2;
	eye.Blink.HoldTicks = 1;
	eye.Blink.OpenTicks = 2;
	eye.BlinkChoices = {{}, eye.Blink};
	program.Eyes.push_back(std::move(eye));
	std::size_t entropy_reads = 0;
	sgre::scene::EmoteVariableControls controls(commands, [] {
	},
	    std::move(program), [&] {
		    ++entropy_reads;
		    return std::uint32_t{42};
	    });
	Require(controls.MissingFamilies().empty() && entropy_reads == 1 && commands.ParameterValues().LocalValueCount() == 0 && controls.TransitionValue("eye-r") == 2 && controls.TransitionValue("blink-index") == 1, "genuine eye initialization/independent entropy service or staged current values differ");
	controls.Update(1);
	controls.Update(1);
	Require(controls.Read("eye") == 2 && controls.Read("blink-index") == 1, "eye blink interval must not advance directly to a closed frame");
	controls.Update(1);
	Require(controls.Read("eye") == 6 && controls.Read("eye-l") == 6 && controls.Read("eye-r") == 6, "actual eye close ramp was not applied to all real Motion channels");
	controls.Set("eye-l", 4);
	controls.Update(0);
	Require(controls.Read("eye-l") == 7 && controls.Read("eye-r") == 6 && controls.Read("blink-index") == 1, "independent eye setter must preserve active blink and publish parameter index rather than amount");
	controls.Set("eye", 0);
	controls.Update(0);
	Require(controls.Read("eye") == 5 && controls.Read("eye-l") == 5 && controls.Read("eye-r") == 5, "main eye setter did not update all three actual graph owners");
	for (std::size_t tick = 0; tick != 4; ++tick) {
		controls.Update(1);
	}
	controls.Set("blink-index", 0.9f, 3, 110);
	controls.Set("eye", 8, 2);
	controls.Update(1);
	Require(controls.Read("eye") == 4 && controls.Read("eye-l") == 4 && controls.Read("blink-index") == 0, "selected zero parameter index must disable modulation while eye graphs interpolate");
	controls.Update(1);
	Require(controls.Read("eye") == 8 && entropy_reads == 1, "eye update must finish real graph interpolation without reseeding or touching Motion random");
	controls.SetBlinkEnabled(false);
	Require(!controls.BlinkEnabled(), "actual containing blink-enabled setter/getter state differs");
	controls.SetBlinkEnabled(true);
	Require(controls.BlinkEnabled(), "actual containing blink-enabled state did not restore");
	RequireFailure([&] {
		controls.Set("blink-index", -1);
	},
	    "actual owned parameter vector");
}

static void TestEyeMetadataInitialization() {
	const auto parameter = [](std::string id, double close) {
		return Atom(sgre::resource::PsbObject{{"id", Atom(std::move(id))}, {"intervalMin", Atom(0.5)}, {"intervalMax", Atom(3.0)}, {"closeTime", Atom(close)}, {"holdTime", Atom(0.05)}, {"openTime", Atom(0.1)}});
	};
	const auto eye = Atom(sgre::resource::PsbObject{{"enabled", Atom(true)}, {"label", Atom(std::string("eye"))}, {"varL", Atom(std::string("left"))}, {"varR", Atom(std::string("right"))}, {"varB", Atom(std::string("blink"))}, {"parameter", Atom(std::string("normal"))}, {"blinkEnabled", Atom(false)}, {"beginFrame", Atom(std::int64_t{4})}, {"endFrame", Atom(std::int64_t{10})}, {"edge", Atom(sgre::resource::PsbArray{})}, {"node", Atom(sgre::resource::PsbArray{})}});
	sgre::resource::PsbDocument document;
	document.Root = Atom(sgre::resource::PsbObject{{"metadata", Atom(sgre::resource::PsbObject{{"immediateControl", Atom(sgre::resource::PsbArray{})}, {"velvetyCommonParams", Atom(sgre::resource::PsbObject{{"ppm", Atom(100.0)}})}, {"blinkParameter", Atom(sgre::resource::PsbArray{parameter("none", 0), parameter("normal", 0.1)})}, {"eyeControl", Atom(sgre::resource::PsbArray{eye})}})}});
	const auto program = sgre::scene::ReadEmoteVariableProgram(document);
	Require(program.Eyes.size() == 1 && program.Eyes[0].InitialFrame == 0 && program.Eyes[0].LowerFrame == 4 && program.Eyes[0].EndFrame == 10 && program.Eyes[0].BlinkParameterIndex == 1 && program.Eyes[0].Blink.IntervalMinimum == 30 && program.Eyes[0].Blink.IntervalMaximum == 180 && program.Eyes[0].Blink.CloseTicks == 6 && program.Eyes[0].BlinkChoices.size() == 2 && program.UnsupportedFamilies.empty(), "actual eye metadata must preserve original lower/effective current, matched parameter index, seconds-to-ticks and ignored blinkEnabled field");
	auto& metadata = std::get<sgre::resource::PsbObject>(std::get<sgre::resource::PsbObject>(document.Root.Data)[0].second.Data);
	metadata[1].second = Atom(false);
	Require(sgre::scene::ReadEmoteVariableProgram(document).Eyes[0].InitialFrame == 0, "native Velvety selection is field presence, even for a present false scalar");
	metadata.erase(metadata.begin() + 1);
	Require(sgre::scene::ReadEmoteVariableProgram(document).Eyes[0].InitialFrame == 4, "absent Velvety field must preserve actual beginFrame");
}

static void TestClampPublicationPhase() {
	sgre::scene::TextureCache cache([](auto, const auto&) -> sgre::render::SpriteTextureHandle {
		throw std::runtime_error("clamp publisher fixture has no texture inputs");
	},
	    [](auto) {
	    });
	sgre::scene::MotionResourceManager manager(cache);
	sgre::scene::MotionPlayerCommands commands(manager);
	sgre::scene::EmoteVariableProgram program;
	program.Immediate = {"x", "y"};
	sgre::scene::EmoteClampDefinition clamp;
	clamp.LeftRight = "x";
	clamp.UpDown = "y";
	clamp.Transform.Kind = sgre::scene::EmoteClampKind::Radial;
	clamp.Transform.Minimum = -1;
	clamp.Transform.Maximum = 1;
	program.Clamps.push_back(std::move(clamp));
	sgre::scene::EmoteVariableControls controls(commands, [] {
	},
	    std::move(program));
	controls.Set("x", 1);
	controls.Set("y", 1);
	controls.Update(0);
	Require(controls.Read("x") == 1 && controls.Read("y") == 1 && std::abs(commands.ParameterValues().Read("x") - std::sqrt(0.5f)) < 0.000002f && std::abs(commands.ParameterValues().Read("y") - std::sqrt(0.5f)) < 0.000002f, "native clamp must postprocess actual Motion after publication without rewriting the Emote current map");
	controls.Set("x", 2);
	controls.Set("y", 0);
	controls.Update(0);
	Require(commands.ParameterValues().Read("x") == 2 && commands.ParameterValues().Read("y") == 0, "native exact axis branch must not invent generic radial clipping");
}

static void TestClampMetadata() {
	const auto clamp = Atom(sgre::resource::PsbObject{{"enabled", Atom(true)}, {"type", Atom(std::int64_t{0})}, {"var_lr", Atom(std::string("x"))}, {"var_ud", Atom(std::string("y"))}, {"min", Atom(-10.0)}, {"max", Atom(50.0)}});
	sgre::resource::PsbDocument document;
	document.Root = Atom(sgre::resource::PsbObject{{"metadata", Atom(sgre::resource::PsbObject{{"immediateControl", Atom(sgre::resource::PsbArray{})}, {"clampControl", Atom(sgre::resource::PsbArray{clamp})}})}});
	const auto program = sgre::scene::ReadEmoteVariableProgram(document);
	Require(program.Clamps.size() == 1 && program.Clamps[0].LeftRight == "x" && program.Clamps[0].UpDown == "y" && program.Clamps[0].Transform.Minimum == -10 && program.Clamps[0].Transform.Maximum == 50 && program.UnsupportedFamilies.empty(), "native clamp metadata min/max and paired variable ownership differ");
}

static void TestInactiveCatalogAndInstantPolicy() {
	auto document = Metadata();
	auto& root_fields = std::get<sgre::resource::PsbObject>(document.Root.Data);
	auto& metadata = std::get<sgre::resource::PsbObject>(root_fields[0].second.Data);
	metadata.emplace_back("instantVariableList", Atom(sgre::resource::PsbArray{Atom(std::string("visible")), Atom(std::string("unrouted"))}));
	metadata.emplace_back("timelineControl", Atom(sgre::resource::PsbArray{Atom(sgre::resource::PsbObject{{"label", Atom(std::string("pose"))}, {"diff", Atom(false)}}), Atom(sgre::resource::PsbObject{{"label", Atom(std::string("overlay"))}, {"diff", Atom(true)}})}));
	auto program = sgre::scene::ReadEmoteVariableProgram(document);
	Require(program.UnsupportedFamilies.empty() && program.TimelineCatalog.size() == 2 && program.InstantVariables.size() == 2, "inactive timeline catalogue and literal instant policy must not be mistaken for active unsupported controllers");
	sgre::scene::TextureCache cache([](auto, const auto&) -> sgre::render::SpriteTextureHandle {
		throw std::runtime_error("initial variable publication has no texture inputs");
	},
	    [](auto) {
	    });
	sgre::scene::MotionResourceManager manager(cache);
	sgre::scene::MotionPlayerCommands commands(manager);
	sgre::scene::EmoteVariableControls controls(commands, [] {
	},
	    std::move(program));
	Require(controls.IsInstantVariable("visible") && controls.IsInstantVariable("unrouted") && controls.IsInstantVariable("selection") && !controls.IsInstantVariable("detail"), "native explicit/custom-selector instant membership differs");
	Require(controls.TimelineLabels(false).size() == 1 && controls.TimelineLabels(false)[0] == "pose" && controls.TimelineLabels(true).size() == 1 && controls.TimelineLabels(true)[0] == "overlay", "actual main/difference catalogue label order differs");
	controls.Update(0);
	Require(commands.ParameterValues().Read("visible") == 100 && commands.ParameterValues().Read("detail") == 0 && commands.ParameterValues().Read("unrouted") == 0, "declared inactive tracks or policy-only names must not override genuine selector initialization");
	controls.Set("pose", 9);
	controls.Update(0);
	Require(commands.ParameterValues().Read("pose") == 0, "variable Set must not invent timeline activation from a catalogue name");
}

static void TestNativeDefaultImmediateChannels() {
	sgre::resource::PsbDocument document;
	document.Root = Atom(sgre::resource::PsbObject{{"metadata", Atom(sgre::resource::PsbObject{})}});
	auto program = sgre::scene::ReadEmoteVariableProgram(document);
	Require(program.DefaultImmediate && program.UnsupportedFamilies.empty(), "absent immediate metadata must select actual three-channel native policy");
	program.Transitions = {"face_tears"};
	sgre::scene::TextureCache cache([](auto, const auto&) -> sgre::render::SpriteTextureHandle {
		throw std::runtime_error("default immediate policy has no texture inputs");
	},
	    [](auto) {
	    });
	sgre::scene::MotionResourceManager manager(cache);
	sgre::scene::MotionPlayerCommands commands(manager);
	sgre::scene::EmoteVariableControls controls(commands, [] {
	},
	    std::move(program));
	Require(controls.IsInstantVariable("face_cheek") && controls.IsInstantVariable("face_eye_hi") && !controls.IsInstantVariable("face_tears"), "native default immediate policy must preserve any existing route without making it instant");
	controls.Set("face_cheek", 0.5f);
	controls.Update(0);
	Require(commands.ParameterValues().Read("face_cheek") == 0.5f && commands.ParameterValues().Read("face_eye_hi") == 0, "native default immediate values must stay lazy until genuinely assigned");
}

static void TestActualStereoInitialization() {
	auto document = Metadata();
	auto& metadata = std::get<sgre::resource::PsbObject>(std::get<sgre::resource::PsbObject>(document.Root.Data)[0].second.Data);
	metadata.emplace_back("stereovisionControl", Atom(sgre::resource::PsbObject{{"variableMatchList", Atom(sgre::resource::PsbArray{Atom(std::string("visible")), Atom(std::string("detail"))})}}));
	auto program = sgre::scene::ReadEmoteVariableProgram(document);
	Require(program.StereoVariables.size() == 2 && program.UnsupportedFamilies.empty(), "stereovision metadata must read its actual object.variableMatchList");
	// Run the same selector as a primary control, before native stereo setup, and distinguish unresolved detail.
	program.PrimarySelectorCount = 1;
	program.Selectors.Selectors[0].Options[1].OnValue = 7;
	sgre::scene::TextureCache cache([](auto, const auto&) -> sgre::render::SpriteTextureHandle {
		throw std::runtime_error("stereo initialization fixture has no texture inputs");
	},
	    [](auto) {
	    });
	sgre::scene::MotionResourceManager manager(cache);
	auto model = std::make_shared<sgre::resource::MotionResource>();
	sgre::resource::MotionObject character;
	character.Name = "character";
	character.Timelines.push_back(Timeline("base"));
	auto child_timeline = Timeline("child");
	auto& child_definition = std::get<sgre::resource::PsbObject>(std::get<sgre::resource::PsbArray>(child_timeline.Parameters.Data)[0].Data);
	for (auto& [name, value] : child_definition) {
		if (name == "rangeBegin") {
			value = Atom(200.0);
		} else if (name == "rangeEnd") {
			value = Atom(-20.0);
		}
	}
	character.Timelines.push_back(std::move(child_timeline));
	model->Objects.push_back(std::move(character));
	sgre::scene::MotionProjectIdentity identity;
	identity.DocumentInstance = cache.AllocateDocumentIdentity();
	identity.RootOffset = 48;
	const auto project = manager.Register(identity, model, {});
	{
		sgre::scene::MotionPlayerCommands parent(manager);
		parent.SetCharacter("character");
		parent.Play("base");
		sgre::scene::MotionTimelineParameters parent_parameters(parent.ParameterValues(), *parent.CachedTimeline());
		{
			sgre::scene::MotionPlayerCommands child(manager, parent);
			child.SetCharacter("character");
			child.Play("child");
			sgre::scene::MotionTimelineParameters child_parameters(child.ParameterValues(), *child.CachedTimeline());
			const auto bounds = parent.ParameterValues().RegisteredBounds("visible");
			Require(bounds && (*bounds)[0] == -20 && (*bounds)[1] == 200 && !parent.ParameterValues().RegisteredBounds("detail"), "native parameter-bound query must include the real selected child's reversed range and exclude unmatched names");
			sgre::scene::EmoteVariableControls controls(parent, [] {
			},
			    std::move(program));
			Require(controls.TransitionValue("visible") == 0 && controls.TransitionValue("detail") == 7 && parent.ParameterValues().LocalValueCount() == 0, "stereo setup must reset only a matched initialized channel without publishing Motion before update");
			controls.Update(0);
			Require(parent.ParameterValues().Read("visible") == 0 && child.ParameterValues().Read("visible") == 0 && parent.ParameterValues().Read("detail") == 7, "stereo initialization must use genuine deferred Emote publication and parent/child Motion fanout");
		}
		const auto after_child = parent.ParameterValues().RegisteredBounds("visible");
		Require(after_child && (*after_child)[0] == 0 && (*after_child)[1] == 100, "actual child retirement must remove its borrowed parameter range");
	}
	Require(manager.Unregister(project), "stereo parameter fixture retained a registration");
}

static void TestConstructorZeroDeltaPublication() {
	sgre::scene::TextureCache cache([](auto, const auto&) -> sgre::render::SpriteTextureHandle {
		throw std::runtime_error("zero-delta controller initialization has no texture inputs");
	},
	    [](auto) {
	    });
	sgre::scene::MotionResourceManager manager(cache);
	sgre::scene::MotionPlayerCommands commands(manager);
	sgre::scene::EmoteVariableProgram program;
	program.Transitions = {"scalar"};
	program.Immediate = {"unassigned"};
	sgre::scene::EmoteGraphChannelDefinition eyebrow;
	eyebrow.Name = "brow";
	eyebrow.InitialFrame = 7;
	program.Eyebrows.push_back(std::move(eyebrow));
	program.Mouths.push_back({"mouth", "talk", 3});
	sgre::scene::EmoteEyeDefinition eye;
	eye.Name = "eye";
	eye.LeftName = "eye-left";
	eye.RightName = "eye-right";
	eye.BlinkName = "blink-index";
	eye.InitialFrame = 5;
	eye.LowerFrame = 0;
	eye.EndFrame = 10;
	eye.BlinkParameterIndex = 0;
	program.Eyes.push_back(std::move(eye));
	std::size_t entropy_calls = 0;
	sgre::scene::EmoteVariableControls controls(commands, [] {
	},
	    std::move(program), [&] {
		    ++entropy_calls;
		    return std::uint32_t{19};
	    });
	Require(commands.ParameterValues().LocalValueCount() == 0 && controls.TransitionValue("brow") == 7 && controls.TransitionValue("eye-left") == 5, "native constructor must own initialized controls without publishing Motion before its first update");
	controls.Update(0);
	Require(commands.ParameterValues().Read("brow") == 7 && commands.ParameterValues().Read("mouth") == 3 && commands.ParameterValues().Read("eye") == 5 && commands.ParameterValues().Read("eye-left") == 5 && commands.ParameterValues().Read("eye-right") == 5 && commands.ParameterValues().LocalValueCount() == 8 && controls.PublishedVariableCount() == 8, "constructor dirty state must publish all native initialized channels during the first zero-delta update even without selectors");
	controls.Update(0);
	Require(entropy_calls == 1 && commands.ParameterValues().LocalValueCount() == 8 && commands.ParameterValues().Read("unassigned") == 0, "repeated zero-delta update must not acquire entropy again or initialize an unassigned immediate channel");
}

static void TestActualGraphMetadata() {
	const auto pair = [](std::int64_t lower, std::int64_t upper) {
		return Atom(sgre::resource::PsbArray{Atom(lower), Atom(upper)});
	};
	const auto graph = Atom(sgre::resource::PsbObject{{"enabled", Atom(true)}, {"label", Atom(std::string("eyebrow"))}, {"beginFrame", Atom(std::int64_t{2})}, {"edge", Atom(sgre::resource::PsbArray{pair(0, 10), pair(20, 30)})}, {"node", Atom(sgre::resource::PsbArray{pair(10, 20)})}});
	const auto mouth = Atom(sgre::resource::PsbObject{{"enabled", Atom(true)}, {"label", Atom(std::string("mouth"))}, {"talkLabel", Atom(std::string("talk"))}, {"beginFrame", Atom(std::int64_t{3})}});
	const auto disabled = Atom(sgre::resource::PsbObject{{"enabled", Atom(false)}});
	sgre::resource::PsbDocument document;
	document.Root = Atom(sgre::resource::PsbObject{{"metadata", Atom(sgre::resource::PsbObject{{"immediateControl", Atom(sgre::resource::PsbArray{})}, {"eyebrowControl", Atom(sgre::resource::PsbArray{disabled, graph})}, {"mouthControl", Atom(sgre::resource::PsbArray{mouth})}, {"eyeControl", Atom(sgre::resource::PsbArray{disabled})}})}});
	const auto program = sgre::scene::ReadEmoteVariableProgram(document);
	Require(program.Eyebrows.size() == 1 && program.Eyebrows[0].InitialFrame == 2 && program.Eyebrows[0].Graph.Nodes[0][1] == 20 && program.Mouths.size() == 1 && program.Mouths[0].TalkName == "talk" && program.Mouths[0].InitialFrame == 3 && program.Eyes.empty() && program.UnsupportedFamilies.empty(), "actual graph node/edge schema, mouth labels or disabled declaration handling differ");
}

int main() {
	try {
		TestActualPublicationPhase();
		TestUnsupportedFamilyRejectsBeforeMutation();
		TestGraphAndMouthPublication();
		TestPartialEyeInitialization();
		TestActualGraphMetadata();
		TestRealEyePublication();
		TestEyeMetadataInitialization();
		TestClampPublicationPhase();
		TestClampMetadata();
		TestInactiveCatalogAndInstantPolicy();
		TestNativeDefaultImmediateChannels();
		TestActualStereoInitialization();
		TestConstructorZeroDeltaPublication();
		std::cout << "Emote transition/immediate/selector owner initialization, deferred actual Motion variable publication, parent/child fanout and explicit missing-family guards passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
