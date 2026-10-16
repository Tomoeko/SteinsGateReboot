#pragma once

#include "sgre/scene/motion_batch_flush.hpp"
#include "sgre/scene/motion_deformation.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <span>

namespace sgre::scene {

	struct MotionPixelUvRectangle {
		float X = 0;
		float Y = 0;
		float Width = 0;
		float Height = 0;
	};

	struct MotionQuadBatchInput {
		std::array<resource::MotionPoint, 4> Positions{};
		// Color words have already passed the renderer's color conversion, if required.
		std::array<std::uint32_t, 4> Colors{};
		MotionPixelUvRectangle PixelUvs;
		resource::MotionPoint InverseTextureSize;
		std::uint32_t Key = 0;
	};

	struct MotionGridBatchInput {
		std::span<const resource::MotionPoint> Positions;
		std::array<std::uint32_t, 4> Colors{};
		MotionPixelUvRectangle PixelUvs;
		resource::MotionPoint InverseTextureSize;
		MotionMeshSubdivision Subdivision;
		std::uint32_t Key = 0;
	};

	struct MotionQuadBatchBuffers {
		std::span<resource::MotionPoint> Positions;
		std::span<resource::MotionPoint> Uvs;
		std::span<std::uint32_t> Colors;
		std::span<std::uint32_t> Indices;
	};

	class MotionQuadBatch {
	public:
		using Submission = std::function<void(const MotionBatchSubmission&)>;
		// All preallocated streams belong to the render task and must outlive this exclusive borrow.
		MotionQuadBatch(MotionBatchMode mode, const MotionQuadBatchBuffers& buffers);
		MotionQuadBatch(const MotionQuadBatch&) = delete;
		MotionQuadBatch& operator=(const MotionQuadBatch&) = delete;
		MotionQuadBatch(MotionQuadBatch&&) = delete;
		MotionQuadBatch& operator=(MotionQuadBatch&&) = delete;
		void Append(const MotionQuadBatchInput& input, const Submission& submit);
		// Native556c20 mode2: row-major vertices with degenerate-connected indexed strips, using these same streams.
		void AppendGrid(const MotionGridBatchInput& input, const Submission& submit);
		void Flush(const Submission& submit);
		[[nodiscard]] MotionBatchCounts Counts() const noexcept;
		[[nodiscard]] std::uint32_t Key() const noexcept;

	private:
		MotionBatchMode m_Mode = MotionBatchMode::TriangleStrip;
		MotionQuadBatchBuffers m_Buffers;
		MotionBatchCounts m_Counts;
		std::uint32_t m_Key = 0;
		bool m_Submitting = false;
	};

} // namespace sgre::scene
