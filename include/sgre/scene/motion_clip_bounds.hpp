#pragma once

#include "sgre/resource/motion.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace sgre::scene {

	struct MotionBounds {
		float MinimumX = std::numeric_limits<float>::max();
		float MinimumY = std::numeric_limits<float>::max();
		float MaximumX = -std::numeric_limits<float>::max();
		float MaximumY = -std::numeric_limits<float>::max();
	};

	struct MotionClipGeometry {
		// Position storage belongs to the scene owner and outlives this cache/view.
		std::span<const resource::MotionPoint> Positions;
		MotionBounds CachedBounds;
		bool Calculated = false;
	};

	struct MotionClipCommand {
		MotionClipGeometry* Geometry = nullptr;
		std::uint64_t ClipGroup = 0;
		std::uint32_t GeometryCount = 0;
		bool Hidden = false;
	};

	struct MotionClipNode {
		MotionClipCommand Command;
		std::span<const MotionClipCommand> Members;
		std::optional<std::uint32_t> Parent;
		std::uint8_t ModeFlags = 0;
	};

	struct MotionClipBoundsResult {
		MotionBounds Bounds;
		bool HasBounds = false;
	};

	[[nodiscard]] MotionBounds MeasureMotionGeometryBounds(MotionClipGeometry& geometry);
	[[nodiscard]] MotionClipBoundsResult MeasureMotionClipRun(std::span<const MotionClipCommand> commands, std::size_t start, std::uint64_t clip_group);
	[[nodiscard]] MotionClipBoundsResult MeasureMotionClipChain(std::span<const MotionClipNode> nodes, std::optional<std::uint32_t> first);

} // namespace sgre::scene
