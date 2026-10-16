#include "sgre/resource/emote_selectors.hpp"
#include "sgre/scene/emote_selector_control.hpp"

#include "sgre/scene/motion_player_commands.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
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
static void RequireFailure(Operation operation, std::string_view fragment) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(fragment) != std::string_view::npos, error.what());
		return;
	}
	throw std::runtime_error("Emote selector accepted an invalid input or active callback");
}

template <typename Value>
static sgre::resource::PsbValue Atom(Value value) {
	sgre::resource::PsbValue result;
	result.Data = std::move(value);
	return result;
}

static sgre::resource::PsbValue Option(std::string label, std::int64_t lower, std::int64_t upper, double on, double off, bool immediate) {
	return Atom(sgre::resource::PsbObject{{"label", Atom(std::move(label))}, {"lower", Atom(lower)}, {"upper", Atom(upper)}, {"onValue", Atom(on)}, {"offValue", Atom(off)}, {"imm", Atom(immediate)}});
}

static sgre::resource::PsbValue Selector(std::string label, sgre::resource::PsbArray options) {
	return Atom(sgre::resource::PsbObject{{"enabled", Atom(std::int64_t{1})}, {"label", Atom(std::move(label))}, {"optionList", Atom(std::move(options))}});
}

static sgre::resource::PsbDocument Metadata() {
	sgre::resource::PsbArray selectors;
	selectors.push_back(Selector("choice", {Option("visible", 0, 0, 100, 0, false), Option("detail", 0, 9, 0, 1, true), Option("", 1, 1, 0, 1, false), Option("visible", 2, 3, 999, -999, true)}));
	selectors.push_back(Atom(sgre::resource::PsbObject{{"enabled", Atom(false)}}));
	const auto default_range = Atom(sgre::resource::PsbObject{{"label", Atom(std::string("ordinal"))}, {"onValue", Atom(7.0)}, {"offValue", Atom(-2.0)}});
	sgre::resource::PsbDocument document;
	document.Root = Atom(sgre::resource::PsbObject{{"metadata", Atom(sgre::resource::PsbObject{{"base", Atom(sgre::resource::PsbObject{{"chara", Atom(std::string("character"))}, {"motion", Atom(std::string("base"))}})}, {"selectorControl", Atom(selectors)}, {"customSelectorList", Atom(sgre::resource::PsbArray{Selector("ordinal-choice", {Option("", 0, 0, 0, 0, false), default_range})})}, {"variableMetaInfoList", Atom(sgre::resource::PsbArray{})}})}});
	return document;
}

struct IoEvent {
	bool Write = false;
	std::string Identifier;
	float Value = 0;
	float Duration = 0;
	float Easing = 0;
};

class RecordingVariableIo final : public sgre::scene::EmoteVariableIo {
public:
	mutable std::vector<IoEvent> Events;
	std::map<std::string, float, std::less<>> Values;
	sgre::scene::EmoteSelectorControl* Reenter = nullptr;

	float Read(std::string_view identifier) const override {
		const auto found = Values.find(identifier);
		const float value = found == Values.end() ? 0 : found->second;
		Events.push_back({false, std::string(identifier), value, 0, 0});
		return value;
	}

	void Set(std::string_view identifier, float value, float duration, float easing) override {
		if (Reenter != nullptr) {
			RequireFailure([&] {
				Reenter->Apply(1, 0, 0);
			},
			    "reenter");
		}
		Events.push_back({true, std::string(identifier), value, duration, easing});
		Values.insert_or_assign(std::string(identifier), value);
	}
};

static void TestMetadataAndSelectorProtocol() {
	auto document = Metadata();
	auto metadata = sgre::resource::ReadEmoteSelectorMetadata(document);
	document = {};
	Require(metadata.Base && metadata.Base->Character == "character" && metadata.Base->Motion == "base" && metadata.Selectors.size() == 2, "selector parsing lost its own base/definition strings or enabled/order semantics");
	Require(metadata.Selectors[0].Options.size() == 3 && metadata.Selectors[0].Options[0].Ranges.size() == 2 && metadata.Selectors[0].Options[0].OnValue == 100 && !metadata.Selectors[0].Options[0].Immediate, "duplicate native labels must retain the first on/off/imm and append ranges only");
	Require(metadata.Selectors[1].Options[1].Ranges[0].Lower == 1 && metadata.Selectors[1].Options[1].Ranges[0].Upper == 1, "missing bounds must use original option index including empty labels");

	RecordingVariableIo variables;
	variables.Values["visible"] = 17;
	sgre::scene::EmoteSelectorControl selector(std::move(metadata.Selectors[0]), variables);
	Require(selector.Selection() == 0 && variables.Values.at("visible") == 100 && variables.Values.at("detail") == 0 && variables.Events.size() == 4, "native constructor must select zero and write exact on/off values, skipping empty labels");
	Require(!variables.Events[0].Write && variables.Events[0].Value == 17 && variables.Events[1].Write && variables.Events[1].Value == 100 && variables.Events[1].Duration == 0 && variables.Events[1].Easing == 0, "native variable read must precede set without resetting or replacing the initial value");
	selector.Apply(1, 12, 0.25f);
	Require(variables.Values.at("visible") == 0 && variables.Values.at("detail") == 0 && variables.Events[5].Duration == 12 && variables.Events[5].Easing == 0.25f && variables.Events[7].Duration == 0 && variables.Events[7].Easing == 0.25f, "inclusive ranges or native per-option immediate duration differs");
	selector.Apply(2, 0, 0);
	Require(variables.Values.at("visible") == 100, "later grouped ranges were discarded or their ignored on/off values replaced the first values");
	selector.Apply(-1, 0, 0);
	Require(variables.Values.at("visible") == 0 && variables.Values.at("detail") == 1, "out-of-range selector input must choose off values without clamping");
	variables.Reenter = &selector;
	selector.Apply(3, 0, 0);
	variables.Reenter = nullptr;
	sgre::scene::EmoteSelectorControl ordinal(std::move(metadata.Selectors[1]), variables);
	Require(variables.Values.at("ordinal") == -2, "ordinal-default range must not match initial selection zero");
	ordinal.Apply(1, 0, 0);
	Require(variables.Values.at("ordinal") == 7, "ordinal-default range did not match exact authored index");
	const auto events_before = variables.Events.size();
	RequireFailure([&] {
		ordinal.Apply(0, -1, 0);
	},
	    "nonnegative");
	RequireFailure([&] {
		ordinal.Apply(0, 0, std::numeric_limits<float>::infinity());
	},
	    "finite");
	Require(variables.Events.size() == events_before && ordinal.Selection() == 1, "invalid application reached variable callbacks or changed selector state");
}

class MotionVariableIo final : public sgre::scene::EmoteVariableIo {
public:
	explicit MotionVariableIo(sgre::scene::MotionPlayerCommands& commands) : Commands(commands) {
	}
	float Read(std::string_view identifier) const override {
		return Commands.ParameterValues().Read(identifier);
	}
	void Set(std::string_view identifier, float value, float duration, float easing) override {
		Require(duration == 0 && easing == 0, "component Motion IO supports only genuine immediate local writes");
		Commands.ParameterValues().WriteLocal(identifier, value, 0);
	}
	sgre::scene::MotionPlayerCommands& Commands;
};

static sgre::resource::MotionTimeline Timeline(std::string name) {
	sgre::resource::MotionTimeline timeline;
	timeline.Name = std::move(name);
	timeline.LastTime = 100;
	timeline.Parameters = Atom(sgre::resource::PsbArray{Atom(sgre::resource::PsbObject{{"id", Atom(std::string("visible"))}, {"discretization", Atom(false)}, {"rangeBegin", Atom(0.0)}, {"rangeEnd", Atom(100.0)}, {"division", Atom(100.0)}}), Atom(sgre::resource::PsbObject{{"id", Atom(std::string("detail"))}, {"discretization", Atom(false)}, {"rangeBegin", Atom(0.0)}, {"rangeEnd", Atom(1.0)}, {"division", Atom(20.0)}})});
	return timeline;
}

static void TestActualMotionParameterFanout() {
	sgre::scene::TextureCache cache([](auto, const auto&) -> sgre::render::SpriteTextureHandle {
		throw std::runtime_error("selector parameter fixture owns no texture sources");
	},
	    [](auto) {
		    throw std::runtime_error("selector parameter fixture owns no uploaded textures");
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
	Require(parent.ParameterValues().LocalValueCount() == 0 && parent_parameters.Entries()[0].Position == 0 && child_parameters.Entries()[0].Position == 0, "plain Motion must retain native empty-map/zero defaults before any selector IO");
	MotionVariableIo io(parent);
	auto metadata = sgre::resource::ReadEmoteSelectorMetadata(Metadata());
	sgre::scene::EmoteSelectorControl selector(std::move(metadata.Selectors[0]), io);
	Require(parent.ParameterValues().Read("visible") == 100 && child.ParameterValues().Read("visible") == 100 && parent_parameters.Entries()[0].Position == 100 && child_parameters.Entries()[0].Position == 100 && child_parameters.Entries()[0].ControlFlags == 0, "actual local setter must fan native selector value into genuine parent/child parameter records");
	Require(parent_parameters.Entries()[1].Position == 0 && child_parameters.Entries()[1].Position == 0, "selector detail onValue zero must not be replaced with generic full visibility");
	selector.Apply(-1, 0, 0);
	Require(parent_parameters.Entries()[0].Position == 0 && child_parameters.Entries()[0].Position == 0 && parent_parameters.Entries()[1].Position == 20 && child_parameters.Entries()[1].Position == 20, "actual parameter normalization or independent on/off value domains differ");
	Require(manager.Contains(project) && manager.ReferenceCount(project) == 1 && cache.Count() == 0, "selector IO must borrow the original registration without another resource or texture owner");
}

static void TestMalformedMetadata() {
	auto document = Metadata();
	sgre::resource::EmoteSelectorLimits limits;
	limits.MaxRanges = 1;
	RequireFailure([&] {
		static_cast<void>(sgre::resource::ReadEmoteSelectorMetadata(document, limits));
	},
	    "ranges");
	limits = {};
	limits.MaxStringBytes = 2;
	RequireFailure([&] {
		static_cast<void>(sgre::resource::ReadEmoteSelectorMetadata(document, limits));
	},
	    "strings");
	RequireFailure([&] {
		static_cast<void>(sgre::resource::ReadEmoteSelectorMetadata({}));
	},
	    "metadata object");
	RecordingVariableIo io;
	sgre::resource::EmoteSelectorDefinition bad;
	bad.Options.resize(1);
	bad.Options[0].OnValue = std::numeric_limits<float>::quiet_NaN();
	RequireFailure([&] {
		sgre::scene::EmoteSelectorControl rejected(std::move(bad), io);
	},
	    "finite");
	Require(io.Events.empty(), "invalid typed definition reached actual variable IO");
}

int main() {
	try {
		TestMetadataAndSelectorProtocol();
		TestActualMotionParameterFanout();
		TestMalformedMetadata();
		std::cout << "Emote selector metadata/defaults/grouped ranges/IO order/immediate timing, actual Motion parent-child parameter fanout and bounds passed; containing Emote controller/update owner remains separate\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
