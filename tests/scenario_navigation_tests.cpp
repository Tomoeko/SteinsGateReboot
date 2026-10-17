#include "sgre/scenario/scenario_navigation.hpp"

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

int main() {
	try {
		TestGraphAndPhonePolicy();
		TestLiteralSafety();
		std::cout << "scenario graph, record ordering, phone retry cycles, ambiguity and literal safety passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
