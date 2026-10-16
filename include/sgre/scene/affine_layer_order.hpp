#pragma once

#include <cstdint>
#include <optional>
#include <span>

namespace sgre::scene {

	struct AffineLayerOrderNode {
		float PositionZ = 0;
		float OffsetZ = 0;
		float OriginZ = 0;
		std::int32_t Order = 0;
		std::optional<std::uint32_t> LinkParent;
	};

	// The span borrows live resolved links; link roots and sort traversal belong to the actual owner.
	[[nodiscard]] float PrepareAffineLayerDepth(std::span<const AffineLayerOrderNode> graph, std::uint32_t index);
	[[nodiscard]] bool CompareAffineLayerOrder(std::span<const AffineLayerOrderNode> graph, std::uint32_t left, std::uint32_t right);

} // namespace sgre::scene
