#include "sgre/scene/affine_layer_order_workspace.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static void CheckNativeLinkedMergeAndStability() {
	std::array<sgre::scene::AffineLayerOrderNode, 4> graph;
	graph[1].LinkParent = 0;
	graph[1].Order = 1;
	graph[2].LinkParent = 0;
	graph[2].Order = -1;
	std::array<std::uint32_t, 4> order{3, 1, 0, 2};
	sgre::scene::AffineLayerOrderWorkspace workspace(4);
	workspace.Sort(graph, order);
	Require(order == std::array<std::uint32_t, 4>{3, 2, 0, 1}, "native merge keeps equal root subtrees stable while inserting negative/positive children around their parent");
	workspace.Sort(graph, order);
	Require(order == std::array<std::uint32_t, 4>{3, 2, 0, 1}, "repeated native sort retains existing equivalent-element order");
	std::array<std::uint32_t, 0> empty;
	workspace.Sort({}, empty);
	std::array<std::uint32_t, 1> single{2};
	workspace.Sort(graph, single);
	Require(single[0] == 2, "empty and single native ranges do not invoke the hierarchy predicate");
}

static void CheckPriorityAndDepth() {
	std::array<sgre::scene::AffineLayerOrderNode, 4> graph;
	graph[0].PositionZ = 1;
	graph[1].PositionZ = 2;
	graph[2].PositionZ = 3;
	graph[3].PositionZ = 4;
	std::array<std::uint32_t, 4> order{0, 1, 2, 3};
	sgre::scene::AffineLayerOrderWorkspace workspace(4);
	workspace.Sort(graph, order);
	Require(order == std::array<std::uint32_t, 4>{3, 2, 1, 0}, "greater native effective depth receives an earlier priority rank");
	Require(sgre::scene::PrepareAffineLayerPriority(3, -0.5f) == 2.5f, "priority adds binary32 rank to the current owner absolute base");
	Require(sgre::scene::PrepareAffineLayerPriority(1, 16777216.0f) == 16777216.0f && sgre::scene::PrepareAffineLayerPriority(2, 16777216.0f) == 16777218.0f, "priority preserves native binary32 rounding after rank conversion");
}

static void CheckInvalidMembershipAndReuse() {
	std::array<sgre::scene::AffineLayerOrderNode, 3> graph;
	graph[0].PositionZ = 5;
	graph[1].PositionZ = 3;
	graph[2].PositionZ = -1;
	sgre::scene::AffineLayerOrderWorkspace workspace(3);
	std::array<std::uint32_t, 3> order{0, 0, 2};
	try {
		workspace.Sort(graph, order);
	} catch (const std::runtime_error& error) {
		Require(std::string(error.what()).find("duplicate") != std::string::npos && order == std::array<std::uint32_t, 3>{0, 0, 2}, "duplicate membership rejects before changing caller order");
		order = {2, 1, 0};
		workspace.Sort(graph, order);
		Require(order == std::array<std::uint32_t, 3>{0, 1, 2} && workspace.Capacity() == 3, "a rejected traversal leaves preallocated workspace reusable");
		graph[0].LinkParent = 1;
		graph[1].LinkParent = 0;
		try {
			workspace.Sort(graph, order);
		} catch (const std::runtime_error& cyclic) {
			Require(std::string(cyclic.what()).find("cycle") != std::string::npos, "unsafe cyclic depth fails through the bounded native predicate");

			return;
		}
	}

	throw std::runtime_error("invalid native list membership or cyclic graph was accepted");
}

static std::uint32_t ReadWord(std::ifstream& input) {
	std::uint32_t result = 0;
	for (std::uint32_t shift = 0; shift < 32; shift += 8) {
		const auto byte = input.get();
		if (byte == std::char_traits<char>::eof()) {
			throw std::runtime_error("truncated affine manager vector");
		}

		result |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(byte)) << shift;
	}

	return result;
}

static void CompareNative(const char* plan_path, const char* expected_path) {
	std::ifstream plan(plan_path, std::ios::binary);
	std::ifstream expected(expected_path, std::ios::binary);
	Require(ReadWord(plan) == 0x4d414753u, "invalid native affine manager corpus magic");
	const auto cases = ReadWord(plan);
	Require(cases <= 10000, "affine manager corpus exceeds its case limit");
	sgre::scene::AffineLayerOrderWorkspace workspace(32);
	std::size_t words = 0;
	for (std::uint32_t case_id = 0; case_id < cases; ++case_id) {
		const auto count = ReadWord(plan);
		Require(count <= 32, "affine manager vector exceeds its owner limit");
		std::vector<sgre::scene::AffineLayerOrderNode> graph(count);
		std::vector<float> absolute_base(count);
		std::vector<std::uint32_t> order(count);
		for (std::size_t index = 0; index < count; ++index) {
			const auto parent = ReadWord(plan);
			if (parent != 0xffffffffu) {
				graph[index].LinkParent = parent;
			}
			graph[index].PositionZ = std::bit_cast<float>(ReadWord(plan));
			graph[index].OffsetZ = std::bit_cast<float>(ReadWord(plan));
			graph[index].OriginZ = std::bit_cast<float>(ReadWord(plan));
			graph[index].Order = std::bit_cast<std::int32_t>(ReadWord(plan));
			absolute_base[index] = std::bit_cast<float>(ReadWord(plan));
		}
		for (auto& member : order) {
			member = ReadWord(plan);
		}

		workspace.Sort(graph, order);
		for (const auto member : order) {
			Require(member == ReadWord(expected), "native affine manager membership order differs");
			++words;
		}
		for (std::uint32_t rank = 0; rank < count; ++rank) {
			const auto value = sgre::scene::PrepareAffineLayerPriority(rank, absolute_base[order[rank]]);
			Require(std::bit_cast<std::uint32_t>(value) == ReadWord(expected), "native affine manager priority word differs");
			++words;
		}
	}

	Require(plan.get() == std::char_traits<char>::eof() && expected.get() == std::char_traits<char>::eof(), "affine manager corpus has trailing bytes");
	std::cout << "Native affine manager: " << cases << " cases, " << words << " words, zero differences\n";
}

int main(int argc, char** argv) {
	try {
		CheckNativeLinkedMergeAndStability();
		CheckPriorityAndDepth();
		CheckInvalidMembershipAndReuse();
		std::cout << "Three affine manager ordering groups passed\n";
		if (argc == 3) {
			CompareNative(argv[1], argv[2]);
		} else if (argc != 1) {
			throw std::runtime_error("affine_layer_order_workspace_tests [NATIVE_PLAN NATIVE_EXPECTED]");
		}
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
