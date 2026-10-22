#include "sgre/scenario/scenario_navigation.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <utility>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static sgre::resource::PsbValue Value(auto data) {
	sgre::resource::PsbValue result;
	result.Data = std::move(data);

	return result;
}

static sgre::resource::PsbValue Exit(std::string storage, std::string label = {}, std::string condition = {}) {
	return Value(sgre::resource::PsbObject{{"type", Value(std::int64_t{0})}, {"storage", Value(std::move(storage))}, {"target", Value(std::move(label))}, {"eval", Value(std::move(condition))}});
}

static sgre::resource::PsbValue Scene(std::string label, sgre::resource::PsbArray exits = {}) {
	sgre::resource::PsbArray lines;
	for (std::int64_t index = 0; index < 3; ++index) {
		lines.push_back(Value(sgre::resource::PsbArray{Value(index + 4), Value(sgre::resource::PsbObject{}), Value(std::int64_t{0}), Value(std::int64_t{0}), Value(20 - index)}));
	}

	return Value(sgre::resource::PsbObject{{"label", Value(std::move(label))}, {"lines", Value(std::move(lines))}, {"nexts", Value(std::move(exits))}});
}

static sgre::resource::PsbDocument Document(std::string storage, sgre::resource::PsbArray scenes) {
	sgre::resource::PsbDocument result;
	result.Root = Value(sgre::resource::PsbObject{{"name", Value(std::move(storage))}, {"scenes", Value(std::move(scenes))}});

	return result;
}

static void Reject(const auto& operation) {
	try {
		operation();
	} catch (const std::runtime_error&) {
		return;
	}

	throw std::runtime_error("invalid or ambiguous navigation unexpectedly succeeded");
}

static void TestGraphAndPhonePolicy() {
	sgre::scenario::ScenarioNavigation graph;
	graph.AddDocument(Document("source.ks", {Scene("*entry", {Exit("source.ks", "*phone")}), Scene("*phone", {Exit("source.ks", "*loop", "phoneNumbers==\"99\""), Exit("destination.ks", {}, " phoneNumbers \t == \"37*4\" "), Exit("source.ks", "*loop")}), Scene("*loop", {Exit("source.ks", "*phone")})}));
	graph.AddDocument(Document("destination.ks", {Scene("*other_default", {Exit("destination.ks", "*end")}), Scene("*end")}));
	graph.AddDocument(Document("unrelated.ks", {Scene("*isolated")}));
	Require(graph.PhoneNumbers("source.ks", "*phone", "destination.ks", "*end") == "37*4", "phone policy followed the retry loop or invented a number");
	Require(!graph.PhoneNumbers("source.ks", "*phone", "unrelated.ks", "*isolated"), "unreachable target fabricated phone input");
	Require(!graph.PhoneNumbers("source.ks", "*phone", "source.ks", "*phone"), "current target advanced its required interaction");
	const auto candidates = graph.Predecessors({"destination.ks", "*end", 19, 5, 1});
	Require(candidates.size() == 13, "predecessor inventory lost actual snapshots or included an unrelated scene/future record");
	Require(candidates[0].Storage == "destination.ks" && candidates[0].Label == "*end" && candidates[0].Record == 0, "same-scene predecessor used source-line sorting instead of the original record cursor");
	Require(candidates[1].Label == "*other_default" && candidates[1].Record == 2, "predecessor selection lost original default-label behavior or reverse record order");
	Require(candidates[4].Label == "*phone", "graph distance did not outrank source/archive lexical order");
	Reject([&] {
		graph.AddDocument(Document("source.ks", {Scene("*new")}));
	});
	Reject([&] {
		static_cast<void>(graph.Predecessors({"missing.ks", "*end", 0, 0, 0}));
	});
	graph.AddDocument(Document("alternate.ks", {Scene("*choice", {Exit("destination.ks", {}, "phoneNumbers==\"1\""), Exit("destination.ks", "*end", "phoneNumbers==\"2\"")})}));
	Reject([&] {
		static_cast<void>(graph.PhoneNumbers("alternate.ks", "*choice", "destination.ks", "*end"));
	});
}

static void TestSeparatedPhoneDispatch() {
	sgre::scenario::ScenarioNavigation graph;
	graph.AddDocument(Document("entry.ks", {Scene("*input", {Exit("dispatch.ks", "*bridge")})}));
	graph.AddDocument(Document("dispatch.ks", {Scene("*bridge", {Exit("dispatch.ks", "*select")}), Scene("*select", {Exit("dispatch.ks", "*retry", "phoneNumbers==\"#120\""), Exit("output.ks", {}, "phoneNumbers==\"120#\""), Exit("dispatch.ks", "*retry")}), Scene("*retry", {Exit("dispatch.ks", "*select")})}));
	graph.AddDocument(Document("output.ks", {Scene("*start")}));
	Require(graph.PhoneNumbers("entry.ks", "*input", "output.ks", "*start") == "120#", "phone policy must follow the original unconditional staging links and exclude a retry through its dispatch");
	Require(!graph.PhoneNumbers("entry.ks", "*input", "dispatch.ks", "*select"), "a target before the number-dependent branch cannot choose an arbitrary number");
	graph.AddDocument(Document("unknown.ks", {Scene("*conditional", {Exit("dispatch.ks", "*select", "unknownFlag")}), Scene("*multiple", {Exit("dispatch.ks", "*select"), Exit("output.ks", "*start")}), Scene("*cycle", {Exit("unknown.ks", "*cycle")}), Scene("*missing", {Exit("missing.ks", "*start")})}));
	for (const auto label : {"*conditional", "*multiple", "*cycle", "*missing"}) {
		Require(!graph.PhoneNumbers("unknown.ks", label, "output.ks", "*start"), "phone staging followed unknown conditions, multiple defaults, a cycle or an absent scene");
	}
}

static void TestLiteralSafety() {
	for (const auto condition : {"phoneNumbers=\"5\"", "phoneNumbers==\"5\" || true", "phoneNumbersExtra==\"5\"", "phoneNumbers==\"a\"", "phoneNumbers==\"\"", "phoneNumbers==\"5\\x31\"", "unknown==\"5\""}) {
		sgre::scenario::ScenarioNavigation graph;
		graph.AddDocument(Document("input.ks", {Scene("*phone", {Exit("output.ks", {}, condition)})}));
		graph.AddDocument(Document("output.ks", {Scene("*start")}));
		Require(!graph.PhoneNumbers("input.ks", "*phone", "output.ks", "*start"), "phone policy accepted nonliteral, empty, escaped or unrelated executable text");
	}
	sgre::scenario::ScenarioNavigation graph;
	Reject([&] {
		graph.AddDocument(Document("bad.ks", {Scene("*same"), Scene("*same")}));
	});
	Require(graph.Scenes().empty(), "failed document published partial navigation state");
}

static sgre::resource::PsbValue Activation(std::string id) {
	return Value(sgre::resource::PsbArray{Value(std::string("resg_phone")), Value(std::string("cmd")), Value(std::string("trigger")), Value(std::string("action")), Value(std::string("start")), Value(std::string("id")), Value(std::move(id))});
}

static sgre::resource::PsbDocument PhoneMaster(sgre::resource::PsbArray definitions) {
	sgre::resource::PsbDocument result;
	result.Root = Value(sgre::resource::PsbObject{{"trigger_list", Value(std::move(definitions))}});

	return result;
}

static sgre::resource::PsbValue PhoneDefinition(std::string id, std::string target) {
	return Value(sgre::resource::PsbObject{{"id", Value(std::move(id))}, {"triggers", Value(sgre::resource::PsbArray{Value(sgre::resource::PsbObject{{"target", Value(std::move(target))}})})}});
}

static void TestPhoneEventCandidates() {
	sgre::scenario::ScenarioNavigation graph;
	auto activated = Scene("*activate", {Exit("event.ks", "*loop")});
	auto& fields = std::get<sgre::resource::PsbObject>(activated.Data);
	auto& lines = std::get<sgre::resource::PsbArray>(std::find_if(fields.begin(), fields.end(), [](const auto& field) {
		return field.first == "lines";
	})->second.Data);
	lines.push_back(Activation("original-trigger"));
	lines.push_back(Activation("original-trigger"));
	graph.AddDocument(Document("input.ks", {Scene("*input", {Exit("event.ks", "*activate", "phoneNumbers==\"12\""), Exit("input.ks", "*retry", "phoneNumbers==\"99\"")}), Scene("*retry", {Exit("input.ks", "*input")})}));
	graph.AddDocument(Document("event.ks", {std::move(activated), Scene("*loop", {Exit("event.ks", "*loop")}), Scene("*call", {Exit("end.ks", "*start")})}));
	graph.AddDocument(Document("end.ks", {Scene("*start")}));
	graph.AddDocument(Document("other.ks", {Scene("*call")}));
	Require(!graph.Reaches("input.ks", "*input", "end.ks", "*start"), "static graph invented a phone event before its definitions were supplied");
	Require(graph.Scenes()[2].PhoneTriggers == std::vector<std::string>{"original-trigger"}, "literal trigger inventory lost identity or retained duplicate activation hints");
	graph.AddPhoneMaster(PhoneMaster({PhoneDefinition("original-trigger", "*call")}));
	Require(graph.Reaches("input.ks", "*input", "end.ks", "*start") && !graph.Reaches("event.ks", "*activate", "other.ks", "*call"), "resource phone event links must be local to the activating storage");
	Require(graph.PhoneNumbers("input.ks", "*input", "end.ks", "*start") == "12", "an explicit resource event must permit earlier literal phone dispatch without admitting its wrong-number cycle");
	Require(!graph.Reaches("event.ks", "*loop", "end.ks", "*start"), "activation hints must not fabricate inherited live phone state in later scenes");
	Require(graph.Scenes()[2].Exits.size() == 1 && graph.Scenes()[2].Exits[0].Label == "*loop", "phone candidate reachability replaced original executable exits");
	const auto predecessors = graph.Predecessors({"end.ks", "*start", 19, 5, 1});
	Require(std::any_of(predecessors.begin(), predecessors.end(), [](const auto& point) {
		return point.Storage == "input.ks";
	}),
	    "event reachability must include genuine earlier checkpoint candidates");
	Reject([&] {
		graph.AddPhoneMaster(PhoneMaster({}));
	});

	for (const auto target : {"", "other.ks*call"}) {
		sgre::scenario::ScenarioNavigation malformed;
		Reject([&] {
			malformed.AddPhoneMaster(PhoneMaster({PhoneDefinition("id", target)}));
		});
		malformed.AddPhoneMaster(PhoneMaster({PhoneDefinition("id", "*valid")}));
	}
	sgre::scenario::ScenarioNavigation duplicate;
	Reject([&] {
		duplicate.AddPhoneMaster(PhoneMaster({PhoneDefinition("id", "*one"), PhoneDefinition("id", "*two")}));
	});
	duplicate.AddPhoneMaster(PhoneMaster({}));
	Reject([&] {
		duplicate.AddPhoneMaster(PhoneMaster({}));
	});
}

int main() {
	try {
		TestGraphAndPhonePolicy();
		TestLiteralSafety();
		TestSeparatedPhoneDispatch();
		TestPhoneEventCandidates();
		std::cout << "scenario graph, record ordering, phone retry cycles, ambiguity and literal safety passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
