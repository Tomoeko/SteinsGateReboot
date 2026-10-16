#include "sgre/scene/motion_batch_flush.hpp"

#include <stdexcept>

namespace sgre::scene {

	static constexpr std::size_t kMaxBatchEntries = 1024 * 1024;

	MotionBatchFlushPlan PrepareMotionBatchFlush(MotionBatchMode mode, std::uint32_t key, std::span<const resource::MotionPoint> positions, std::span<const resource::MotionPoint> uvs, std::span<const std::uint32_t> colors, std::span<const std::uint32_t> indices) {
		if (positions.size() > kMaxBatchEntries || uvs.size() > kMaxBatchEntries || colors.size() > kMaxBatchEntries || indices.size() > kMaxBatchEntries) {
			throw std::runtime_error("motion batch exceeds its prepared stream limit");
		}

		MotionBatchFlushPlan plan;
		if (positions.empty()) {
			plan.RetainedCounts.Uvs = uvs.size();
			plan.RetainedCounts.Colors = colors.size();
			plan.RetainedCounts.Indices = indices.size();

			return plan;
		}

		if (uvs.size() < positions.size() || colors.size() < positions.size()) {
			throw std::runtime_error("motion batch has incomplete parallel vertex streams");
		}

		MotionBatchSubmission submission;
		submission.Mode = mode;
		submission.Key = key;
		submission.Positions = positions;
		submission.Uvs = uvs.first(positions.size());
		submission.Colors = colors.first(positions.size());
		switch (mode) {
		case MotionBatchMode::TriangleStrip:
			if (positions.size() < 2) {
				throw std::runtime_error("motion batch strip lacks its two outer duplicate entries");
			}

			submission.Positions = positions.subspan(1, positions.size() - 2);
			submission.Uvs = submission.Uvs.subspan(1, positions.size() - 2);
			submission.Colors = submission.Colors.subspan(1, positions.size() - 2);
			break;
		case MotionBatchMode::IndexedTriangleStrip:
			if (indices.size() < 2) {
				throw std::runtime_error("indexed motion batch strip lacks its two outer duplicate indices");
			}

			submission.Indices = indices.subspan(1, indices.size() - 2);
			break;
		case MotionBatchMode::TriangleList:
			break;
		case MotionBatchMode::IndexedTriangleList:
			submission.Indices = indices;
			break;
		default:
			throw std::runtime_error("motion batch mode is not recovered");
		}

		// Native555490 clears all staging ends only after its backend callback returns.
		plan.Submission = submission;

		return plan;
	}

} // namespace sgre::scene
