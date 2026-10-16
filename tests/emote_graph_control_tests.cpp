#include "sgre/scene/emote_graph_control.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

static void RequireValue(float actual, float expected, std::string_view operation) {
	if (actual != expected) {
		throw std::runtime_error(std::string(operation) + ": expected " + std::to_string(expected) + ", actual " + std::to_string(actual));
	}
}

static void TestEquivalentFrames() {
	sgre::scene::EmoteGraphDefinition definition;
	definition.Edges = {{0, 10}, {20, 30}};
	definition.Nodes = {{10, 20}};
	sgre::scene::EmoteGraphControl graph(std::move(definition));
	graph.Prepare(2, 28);
	RequireValue(graph.Evaluate(0), 2, "actual graph start");
	RequireValue(graph.Evaluate(0.25f), 6, "approach the first equivalent endpoint");
	RequireValue(graph.Evaluate(0.5f), 10, "first equivalent endpoint is retained at the exact boundary");
	RequireValue(graph.Evaluate(0.75f), 24, "leave the other equivalent endpoint");
	RequireValue(graph.Evaluate(1), 28, "actual graph target");
	graph.Prepare(28, 2);
	RequireValue(graph.Evaluate(0.25f), 24, "reversed equivalent route first leg");
	RequireValue(graph.Evaluate(0.75f), 6, "reversed equivalent route last leg");
}

static void TestNativeCandidateTieOrder() {
	sgre::scene::EmoteGraphDefinition definition;
	definition.Edges = {{0, 10}, {20, 30}};
	definition.Nodes = {{0, 20}, {10, 30}};
	sgre::scene::EmoteGraphControl graph(std::move(definition));
	graph.Prepare(5, 25);
	RequireValue(graph.Evaluate(0.25f), 2.5f, "equal shortest candidates preserve lower/lower first");
	RequireValue(graph.Evaluate(0.75f), 22.5f, "tie route leaves lower target endpoint");
}

static void TestInteriorPointMergeAndSplit() {
	sgre::scene::EmoteGraphDefinition definition;
	definition.Edges = {{10, 0}, {20, 30}};
	definition.Nodes = {{2, 8, 20}};
	sgre::scene::EmoteGraphControl graph(std::move(definition));
	graph.Prepare(0, 30);
	RequireValue(graph.Evaluate(0.2f), 3, "merged interior average shortens actual route to fifteen frame units");
	RequireValue(graph.Evaluate(1.0f / 3.0f), 20, "native final-leg inclusive boundary selects the destination equivalent endpoint");
	RequireValue(graph.Evaluate(2.0f / 3.0f), 25, "split edge joins the original external equivalent endpoint");
}

static void TestDisconnectedAndSameEdge() {
	sgre::scene::EmoteGraphDefinition definition;
	definition.Edges = {{0, 10}, {20, 30}};
	sgre::scene::EmoteGraphControl graph(std::move(definition));
	graph.Prepare(2, 28);
	RequireValue(graph.Evaluate(0), 2, "disconnected route exact start");
	RequireValue(graph.Evaluate(0.01f), 28, "native no-route kind zero jumps to target");
	graph.Prepare(2, 8);
	RequireValue(graph.Evaluate(0.25f), 3.5f, "same edge uses direct interpolation");
	RequireValue(graph.Evaluate(-1), 2, "ratio lower clamp");
	RequireValue(graph.Evaluate(2), 8, "ratio upper clamp");
}

int main() {
	try {
		TestEquivalentFrames();
		TestNativeCandidateTieOrder();
		TestInteriorPointMergeAndSplit();
		TestDisconnectedAndSameEdge();
		std::cout << "Emote integer frame graph: endpoint equivalence, deterministic shortest-route tie order, merge/split, no-route jump and direct interpolation passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
