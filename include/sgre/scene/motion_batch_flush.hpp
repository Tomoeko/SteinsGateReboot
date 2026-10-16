#pragma once

#include "sgre/resource/motion.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace sgre::scene {

	enum class MotionBatchMode : std::uint32_t {
		TriangleStrip = 1,
		IndexedTriangleStrip = 2,
		TriangleList = 3,
		IndexedTriangleList = 4
	};

	struct MotionBatchCounts {
		std::size_t Positions = 0;
		std::size_t Uvs = 0;
		std::size_t Colors = 0;
		std::size_t Indices = 0;
		bool operator==(const MotionBatchCounts&) const = default;
	};

	struct MotionBatchSubmission {
		MotionBatchMode Mode = MotionBatchMode::TriangleStrip;
		std::uint32_t Key = 0;
		// Borrow the render task's already accumulated buffers through submission completion.
		std::span<const resource::MotionPoint> Positions;
		std::span<const resource::MotionPoint> Uvs;
		std::span<const std::uint32_t> Colors;
		std::span<const std::uint32_t> Indices;
	};

	struct MotionBatchFlushPlan {
		std::optional<MotionBatchSubmission> Submission;
		// Apply after a successful submission; empty positions leave all four stream ends unchanged.
		MotionBatchCounts RetainedCounts;
	};

	[[nodiscard]] MotionBatchFlushPlan PrepareMotionBatchFlush(MotionBatchMode mode, std::uint32_t key, std::span<const resource::MotionPoint> positions, std::span<const resource::MotionPoint> uvs, std::span<const std::uint32_t> colors, std::span<const std::uint32_t> indices);

} // namespace sgre::scene
