#include "sgre/scene/affine_layer_order.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static void CheckRecursivePositionDepth() {
	std::array<sgre::scene::AffineLayerOrderNode, 3> graph;
	graph[0].PositionZ = 16777216;
	graph[0].OffsetZ = 100;
	graph[0].OriginZ = 200;
	graph[1].LinkParent = 0;
	graph[1].PositionZ = -16777216;
	graph[2].LinkParent = 1;
	graph[2].PositionZ = 1;
	Require(sgre::scene::PrepareAffineLayerDepth(graph, 2) == 1, "depth adds each child after its completed parent, preserving native binary32 association");
	Require(sgre::scene::PrepareAffineLayerDepth(graph, 0) == 16777216, "recursive position depth excludes ancestor offset and origin values");
	graph[0].PositionZ = 0;
	graph[1].PositionZ = 0;
	graph[2].PositionZ = 0;
	Require(sgre::scene::CompareAffineLayerOrder(graph, 0, 2), "comparator adds the current ancestor's offset/origin after recursive position depth");
}

static void CheckParentAndSubtreeOrder() {
	std::array<sgre::scene::AffineLayerOrderNode, 5> graph;
	graph[1].LinkParent = 0;
	graph[1].Order = -1;
	graph[2].LinkParent = 0;
	graph[2].Order = 1;
	graph[3].Order = 5;
	graph[4].LinkParent = 3;
	graph[4].Order = -100;
	Require(sgre::scene::CompareAffineLayerOrder(graph, 1, 0) && !sgre::scene::CompareAffineLayerOrder(graph, 0, 1), "negative-order child precedes its direct parent when depth ties");
	Require(sgre::scene::CompareAffineLayerOrder(graph, 0, 2) && !sgre::scene::CompareAffineLayerOrder(graph, 2, 0), "nonnegative-order child follows its direct parent when depth ties");
	Require(sgre::scene::CompareAffineLayerOrder(graph, 1, 2), "siblings compare their own signed order");
	Require(sgre::scene::CompareAffineLayerOrder(graph, 2, 4) && !sgre::scene::CompareAffineLayerOrder(graph, 4, 2), "distinct subtrees eventually compare ancestors, preserving the original parent traversal choice");
	Require(!sgre::scene::CompareAffineLayerOrder(graph, 1, 1), "self comparison remains false");
	graph[4].OffsetZ = 1;
	Require(sgre::scene::CompareAffineLayerOrder(graph, 4, 2), "greater effective depth precedes hierarchy/order tie handling");
}

static void CheckBoundedInvalidGraph() {
	std::array<sgre::scene::AffineLayerOrderNode, 2> graph;
	graph[0].LinkParent = 1;
	graph[1].LinkParent = 0;
	try {
		static_cast<void>(sgre::scene::PrepareAffineLayerDepth(graph, 0));
	} catch (const std::runtime_error& error) {
		Require(std::string(error.what()).find("cycle") != std::string::npos, "cyclic native links have an explicit portable rejection");
		graph[0].LinkParent.reset();
		graph[1].LinkParent.reset();
		graph[1].OffsetZ = std::numeric_limits<float>::quiet_NaN();
		try {
			static_cast<void>(sgre::scene::CompareAffineLayerOrder(graph, 0, 1));
		} catch (const std::runtime_error& nonfinite) {
			Require(std::string(nonfinite.what()).find("finite") != std::string::npos, "unmeasured unordered comparator domain remains explicit");

			return;
		}
	}

	throw std::runtime_error("unsafe affine graph was incorrectly accepted");
}

static std::uint32_t ReadWord(std::ifstream& stream) {
	std::uint32_t word = 0;
	for (std::uint32_t shift = 0; shift < 32; shift += 8) {
		const auto byte = stream.get();
		if (byte == std::char_traits<char>::eof()) {
			throw std::runtime_error("truncated affine order vectors");
		}
		word |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(byte)) << shift;
	}

	return word;
}

static void CompareNative(const char* plan_path, const char* expected_path) {
	std::ifstream plan(plan_path, std::ios::binary);
	std::ifstream expected(expected_path, std::ios::binary);
	Require(plan && expected && ReadWord(plan) == 0x4f414753, "valid affine order vectors");
	const auto count = ReadWord(plan);
	Require(count <= 10000, "bounded affine order graph count");
	for (std::uint32_t index = 0; index < count; ++index) {
		std::array<sgre::scene::AffineLayerOrderNode, 8> graph;
		for (auto& node : graph) {
			const auto parent = ReadWord(plan);
			if (parent != 0xffffffff) {
				node.LinkParent = parent;
			}
			node.PositionZ = std::bit_cast<float>(ReadWord(plan));
			node.OffsetZ = std::bit_cast<float>(ReadWord(plan));
			node.OriginZ = std::bit_cast<float>(ReadWord(plan));
			node.Order = std::bit_cast<std::int32_t>(ReadWord(plan));
		}
		for (std::uint32_t node = 0; node < graph.size(); ++node) {
			Require(std::bit_cast<std::uint32_t>(sgre::scene::PrepareAffineLayerDepth(graph, node)) == ReadWord(expected), "original recursive depth word mismatch");
		}
		for (std::uint32_t left = 0; left < graph.size(); ++left) {
			for (std::uint32_t right = 0; right < graph.size(); ++right) {
				Require((sgre::scene::CompareAffineLayerOrder(graph, left, right) ? 1u : 0u) == ReadWord(expected), "original complete affine comparator word mismatch");
			}
		}
	}
	Require(plan.peek() == std::char_traits<char>::eof() && expected.peek() == std::char_traits<char>::eof(), "affine order vectors consumed exactly");
	std::cout << "Native " << count << " affine order graphs: " << count * 72 << " words, zero differences\n";
}

int main(int argc, char** argv) {
	try {
		Require(argc == 1 || argc == 3, "optional affine order plan and expected streams");
		CheckRecursivePositionDepth();
		CheckParentAndSubtreeOrder();
		CheckBoundedInvalidGraph();
		std::cout << "Three affine order fixture groups passed\n";
		if (argc == 3) {
			CompareNative(argv[1], argv[2]);
		}
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
