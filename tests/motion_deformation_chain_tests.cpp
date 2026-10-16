#include "sgre/scene/motion_deformation_chain.hpp"

#include <array>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static void RequireFailure(const std::function<void()>& operation, std::string_view context) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, "deformation chain failure lost its context");

		return;
	}

	throw std::runtime_error("invalid deformation hierarchy was accepted");
}

static void TestOriginalAncestorSelection() {
	// Original 0x140520d80 selected these ancestors and active/export/stopping flags.
	const std::array<std::array<std::uint32_t, 8>, 10> inputs = {{
	    {0xffffffff, 0x02000000, 1, 10, 0, 0, 1, 1}, // Parent, inheritance, mesh mode, sync mask, combine, hidden, source ready, patch
	    {0xffffffff, 0, 0, 7, 1, 1, 0, 1},
	    {0, 0x02000000, 0, 5, 0, 0, 1, 0},
	    {2, 0, 1, 12, 1, 0, 0, 0},
	    {0xffffffff, 0, 0, 2, 1, 1, 0, 1},
	    {0, 0, 0, 14, 1, 1, 0, 1},
	    {4, 0x02000000, 0, 9, 0, 0, 0, 1},
	    {4, 0x02000000, 1, 6, 1, 0, 0, 0},
	    {1, 0, 1, 4, 1, 0, 1, 1},
	    {0, 0, 0, 15, 1, 0, 0, 0},
	}};
	const std::array<std::array<std::uint32_t, 4>, 10> expected = {{
	    {0xffffffff, 1, 1, 0}, // Ancestor, active, export, stop
	    {0xffffffff, 0, 0, 0},
	    {0, 0, 0, 0},
	    {0, 0, 0, 1},
	    {0xffffffff, 0, 0, 0},
	    {0, 1, 0, 1},
	    {0xffffffff, 0, 0, 0},
	    {0xffffffff, 0, 0, 0},
	    {0xffffffff, 1, 0, 0},
	    {0, 1, 0, 1},
	}};
	std::array<sgre::scene::MotionDeformationNode, 10> nodes;
	std::array<sgre::scene::MotionDeformationLink, 10> links;
	for (std::size_t index = 0; index < nodes.size(); ++index) {
		const auto& input = inputs[index];
		auto& node = nodes[index];
		if (input[0] != 0xffffffff) {
			node.Parent = input[0];
		}

		node.InheritMask = input[1];
		node.MeshTransform = input[2];
		node.MeshSyncChildMask = input[3];
		node.MeshCombine = input[4] != 0;
		node.Hidden = input[5] != 0;
		node.SourceReady = input[6] != 0;
		node.HasPatch = input[7] != 0;
	}

	sgre::scene::ResolveMotionDeformationLinks(nodes, links);
	for (std::size_t index = 0; index < links.size(); ++index) {
		const auto& link = links[index];
		const std::array<std::uint32_t, 4> actual = {link.Ancestor.value_or(0xffffffff), link.Active, link.ExportToChildren, link.StopsInheritance};
		Require(actual == expected[index], "native ancestor and mesh boundary vector changed");
	}
}

static void TestHierarchyValidation() {
	std::array<sgre::scene::MotionDeformationNode, 2> nodes;
	std::array<sgre::scene::MotionDeformationLink, 2> links;
	nodes[1].Parent = 1;
	RequireFailure([&]() {
		sgre::scene::ResolveMotionDeformationLinks(nodes, links);
	},
	    "precede");
	nodes[1].Parent = 0;
	nodes[0].Parent = 0xffffffff;
	RequireFailure([&]() {
		sgre::scene::ResolveMotionDeformationLinks(nodes, links);
	},
	    "precede");
	nodes[0].Parent.reset();
	RequireFailure([&]() {
		sgre::scene::ResolveMotionDeformationLinks(nodes, std::span(links).first(1));
	},
	    "count");
	std::array<sgre::scene::MotionDeformationNode, 130> deep_nodes;
	std::array<sgre::scene::MotionDeformationLink, 130> deep_links;
	for (std::uint32_t index = 1; index < deep_nodes.size(); ++index) {
		deep_nodes[index].Parent = index - 1;
	}

	RequireFailure([&]() {
		sgre::scene::ResolveMotionDeformationLinks(deep_nodes, deep_links);
	},
	    "depth");
	sgre::scene::ResolveMotionDeformationLinks({}, {});
}

int main() {
	try {
		TestOriginalAncestorSelection();
		TestHierarchyValidation();
		std::cout << "Native deformation ancestors, patch export, combine boundaries, and hierarchy guards passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
