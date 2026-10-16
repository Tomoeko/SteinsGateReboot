#include "sgre/scene/affine_layer_order.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace sgre::scene {

	static constexpr std::size_t kMaxAffineOrderNodes = 65536;
	static constexpr std::size_t kMaxAffineOrderDepth = 256;

	static const AffineLayerOrderNode& AffineOrderNode(std::span<const AffineLayerOrderNode> graph, std::uint32_t index) {
		if (graph.size() > kMaxAffineOrderNodes || index >= graph.size()) {
			throw std::runtime_error("affine order graph exceeds its node limit or references an invalid node");
		}

		return graph[index];
	}

	float PrepareAffineLayerDepth(std::span<const AffineLayerOrderNode> graph, std::uint32_t index) {
		std::array<std::uint32_t, kMaxAffineOrderDepth> path{};
		std::size_t count = 0;
		while (true) {
			const auto& node = AffineOrderNode(graph, index);
			if (!std::isfinite(node.PositionZ)) {
				throw std::runtime_error("affine order depth requires finite position-Z values");
			}
			if (count == path.size()) {
				throw std::runtime_error("affine order link depth exceeds its traversal limit");
			}
			if (std::find(path.begin(), path.begin() + static_cast<std::ptrdiff_t>(count), index) != path.begin() + static_cast<std::ptrdiff_t>(count)) {
				throw std::runtime_error("affine order links contain a cycle");
			}
			path[count++] = index;
			if (!node.LinkParent) {
				break;
			}
			index = *node.LinkParent;
		}

		// Native recursive return adds each child to the completed parent value.
		auto result = graph[path[count - 1]].PositionZ;
		while (count > 1) {
			--count;
			result = result + graph[path[count - 1]].PositionZ;
			if (!std::isfinite(result)) {
				throw std::runtime_error("affine order depth overflows its finite domain");
			}
		}

		return result;
	}

	static float AffineOrderDepth(std::span<const AffineLayerOrderNode> graph, std::uint32_t index) {
		const auto& node = AffineOrderNode(graph, index);
		const auto result = (PrepareAffineLayerDepth(graph, index) + node.OffsetZ) + node.OriginZ;
		if (!std::isfinite(result)) {
			throw std::runtime_error("affine comparator requires finite offset/origin depth");
		}

		return result;
	}

	bool CompareAffineLayerOrder(std::span<const AffineLayerOrderNode> graph, std::uint32_t left, std::uint32_t right) {
		for (std::size_t step = 0; step < kMaxAffineOrderDepth * 2; ++step) {
			const auto left_depth = AffineOrderDepth(graph, left);
			const auto right_depth = AffineOrderDepth(graph, right);
			if (left_depth > right_depth) {
				return true;
			}
			if (left_depth != right_depth) {
				return false;
			}

			const auto& left_node = AffineOrderNode(graph, left);
			const auto& right_node = AffineOrderNode(graph, right);
			if (left_node.LinkParent == right) {
				return left_node.Order < 0;
			}
			if (right_node.LinkParent == left) {
				return right_node.Order >= 0;
			}
			if (left_node.LinkParent == right_node.LinkParent) {
				return left_node.Order < right_node.Order;
			}
			if (left_node.LinkParent) {
				left = *left_node.LinkParent;
			} else {
				right = *right_node.LinkParent;
			}
		}

		throw std::runtime_error("affine order ancestor comparison exceeds its traversal limit");
	}

} // namespace sgre::scene
