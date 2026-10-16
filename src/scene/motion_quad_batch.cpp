#include "sgre/scene/motion_quad_batch.hpp"

#include "sgre/scene/motion_grid_draw.hpp"

#include <cmath>
#include <stdexcept>

namespace sgre::scene {

	static constexpr std::size_t kMaxBatchEntries = 1024 * 1024;
	static constexpr std::array<std::size_t, 6> kStripOrder{0, 0, 2, 1, 3, 3};
	static constexpr std::array<std::size_t, 6> kListOrder{0, 1, 2, 2, 1, 3};

	static bool IndexedMode(MotionBatchMode mode) noexcept {
		return mode == MotionBatchMode::IndexedTriangleStrip || mode == MotionBatchMode::IndexedTriangleList;
	}

	static bool StripMode(MotionBatchMode mode) noexcept {
		return mode == MotionBatchMode::TriangleStrip || mode == MotionBatchMode::IndexedTriangleStrip;
	}

	static void ValidateMode(MotionBatchMode mode) {
		if (mode != MotionBatchMode::TriangleStrip && mode != MotionBatchMode::IndexedTriangleStrip && mode != MotionBatchMode::TriangleList && mode != MotionBatchMode::IndexedTriangleList) {
			throw std::runtime_error("motion quad batching requires a recovered batch mode");
		}
	}

	static void ValidateInput(const MotionQuadBatchInput& input) {
		if (!std::isfinite(input.PixelUvs.X) || !std::isfinite(input.PixelUvs.Y) || !std::isfinite(input.PixelUvs.Width) || !std::isfinite(input.PixelUvs.Height) || !std::isfinite(input.InverseTextureSize.X) || !std::isfinite(input.InverseTextureSize.Y) || input.InverseTextureSize.X <= 0 || input.InverseTextureSize.Y <= 0) {
			throw std::runtime_error("motion quad batching requires finite pixel UVs and positive finite inverse texture dimensions");
		}

		for (const auto& point : input.Positions) {
			if (!std::isfinite(point.X) || !std::isfinite(point.Y)) {
				throw std::runtime_error("motion quad batching requires finite evaluated positions");
			}
		}
	}

	static std::array<resource::MotionPoint, 4> QuadUvs(const MotionQuadBatchInput& input) {
		const auto left = input.PixelUvs.X * input.InverseTextureSize.X;
		const auto top = input.PixelUvs.Y * input.InverseTextureSize.Y;
		const auto right = left + input.PixelUvs.Width * input.InverseTextureSize.X;
		const auto bottom = top + input.PixelUvs.Height * input.InverseTextureSize.Y;
		if (!std::isfinite(left) || !std::isfinite(top) || !std::isfinite(right) || !std::isfinite(bottom)) {
			throw std::runtime_error("motion quad UV arithmetic exceeded its finite recovered scope");
		}

		std::array<resource::MotionPoint, 4> uvs{};
		uvs[0].X = left;
		uvs[0].Y = top;
		uvs[1].X = right;
		uvs[1].Y = top;
		uvs[2].X = left;
		uvs[2].Y = bottom;
		uvs[3].X = right;
		uvs[3].Y = bottom;

		return uvs;
	}

	MotionQuadBatch::MotionQuadBatch(MotionBatchMode mode, const MotionQuadBatchBuffers& buffers) : m_Mode(mode), m_Buffers(buffers) {
		ValidateMode(mode);
		const auto vertex_count = IndexedMode(mode) ? std::size_t{4} : std::size_t{6};
		const auto index_count = IndexedMode(mode) ? std::size_t{6} : std::size_t{0};
		if (buffers.Positions.size() < vertex_count || buffers.Uvs.size() < vertex_count || buffers.Colors.size() < vertex_count || buffers.Indices.size() < index_count || buffers.Positions.size() > kMaxBatchEntries || buffers.Uvs.size() > kMaxBatchEntries || buffers.Colors.size() > kMaxBatchEntries || buffers.Indices.size() > kMaxBatchEntries) {
			throw std::runtime_error("motion quad batching requires bounded prepared streams that fit one quad");
		}
	}

	void MotionQuadBatch::Append(const MotionQuadBatchInput& input, const Submission& submit) {
		if (m_Submitting) {
			throw std::runtime_error("motion quad batch cannot append during submission");
		}

		ValidateInput(input);
		const auto uvs = QuadUvs(input);
		const auto vertex_count = IndexedMode(m_Mode) ? std::size_t{4} : std::size_t{6};
		const auto index_count = IndexedMode(m_Mode) ? std::size_t{6} : std::size_t{0};
		const auto used = input.Key == m_Key ? m_Counts : MotionBatchCounts{};
		if (vertex_count > m_Buffers.Positions.size() - used.Positions || vertex_count > m_Buffers.Uvs.size() - used.Uvs || vertex_count > m_Buffers.Colors.size() - used.Colors || index_count > m_Buffers.Indices.size() - used.Indices) {
			throw std::runtime_error("motion quad batch exhausted its prepared stream capacity");
		}

		if (input.Key != m_Key) {
			Flush(submit);
			m_Key = input.Key;
		}

		const auto& order = StripMode(m_Mode) ? kStripOrder : kListOrder;
		const auto base = m_Counts.Positions;
		for (std::size_t vertex = 0; vertex < vertex_count; ++vertex) {
			const auto corner = IndexedMode(m_Mode) ? vertex : order[vertex];
			m_Buffers.Positions[m_Counts.Positions++] = input.Positions[corner];
			m_Buffers.Uvs[m_Counts.Uvs++] = uvs[corner];
			m_Buffers.Colors[m_Counts.Colors++] = input.Colors[corner];
		}

		for (std::size_t index = 0; index < index_count; ++index) {
			m_Buffers.Indices[m_Counts.Indices++] = static_cast<std::uint32_t>(base + order[index]);
		}
	}

	void MotionQuadBatch::AppendGrid(const MotionGridBatchInput& input, const Submission& submit) {
		if (m_Submitting) {
			throw std::runtime_error("motion grid batch cannot append during submission");
		}
		const auto vertex_count = MotionMeshGridVertexCount(input.Subdivision);
		if (input.Positions.size() != vertex_count) {
			throw std::runtime_error("motion grid batch positions differ from the actual subdivision product");
		}

		MotionQuadBatchInput material;
		material.Colors = input.Colors;
		material.PixelUvs = input.PixelUvs;
		material.InverseTextureSize = input.InverseTextureSize;
		material.Key = input.Key;
		if (input.Subdivision.X == 1 && input.Subdivision.Y == 1) {
			for (std::size_t index = 0; index < material.Positions.size(); ++index) {
				material.Positions[index] = input.Positions[index];
			}
			Append(material, submit);
			return;
		}
		if (m_Mode != MotionBatchMode::IndexedTriangleStrip) {
			throw std::runtime_error("motion grid batching requires the recovered full-frame indexed strip mode2");
		}

		ValidateInput(material);
		static_cast<void>(QuadUvs(material));
		for (const auto& position : input.Positions) {
			if (!std::isfinite(position.X) || !std::isfinite(position.Y)) {
				throw std::runtime_error("motion grid batching requires finite evaluated positions");
			}
		}
		const auto columns = static_cast<std::size_t>(input.Subdivision.X) + 1;
		const auto indices_per_row = columns * 2 + 2;
		if (input.Subdivision.Y > kMaxBatchEntries / indices_per_row) {
			throw std::runtime_error("motion grid batching exceeds its bounded index work");
		}
		const auto index_count = static_cast<std::size_t>(input.Subdivision.Y) * indices_per_row;
		const auto normalized_width = input.PixelUvs.Width * input.InverseTextureSize.X;
		const auto normalized_height = input.PixelUvs.Height * input.InverseTextureSize.Y;
		if (!std::isfinite(static_cast<float>(input.Subdivision.X) * normalized_width) || !std::isfinite(static_cast<float>(input.Subdivision.Y) * normalized_height)) {
			throw std::runtime_error("motion grid UV intermediate arithmetic exceeds its finite scope");
		}
		const auto used = input.Key == m_Key ? m_Counts : MotionBatchCounts{};
		if (vertex_count > m_Buffers.Positions.size() - used.Positions || vertex_count > m_Buffers.Uvs.size() - used.Uvs || vertex_count > m_Buffers.Colors.size() - used.Colors || index_count > m_Buffers.Indices.size() - used.Indices) {
			throw std::runtime_error("motion grid batch exhausted its prepared stream capacity");
		}

		if (input.Key != m_Key) {
			Flush(submit);
			m_Key = input.Key;
		}
		const auto base = m_Counts.Positions;
		const auto left = input.PixelUvs.X * input.InverseTextureSize.X;
		const auto top = input.PixelUvs.Y * input.InverseTextureSize.Y;
		const auto width = input.PixelUvs.Width * input.InverseTextureSize.X;
		const auto height = input.PixelUvs.Height * input.InverseTextureSize.Y;
		const auto horizontal_step = 1.0f / static_cast<float>(input.Subdivision.X);
		const auto vertical_step = 1.0f / static_cast<float>(input.Subdivision.Y);
		for (std::uint32_t row = 0; row <= input.Subdivision.Y; ++row) {
			const auto first = InterpolateMotionGridColor(input.Colors[0], input.Colors[2], row, input.Subdivision.Y);
			const auto last = InterpolateMotionGridColor(input.Colors[1], input.Colors[3], row, input.Subdivision.Y);
			const auto v = (static_cast<float>(row) * height) * vertical_step + top;
			for (std::uint32_t column = 0; column <= input.Subdivision.X; ++column) {
				const auto index = static_cast<std::size_t>(row) * columns + column;
				m_Buffers.Positions[m_Counts.Positions++] = input.Positions[index];
				auto& uv = m_Buffers.Uvs[m_Counts.Uvs++];
				uv.X = (static_cast<float>(column) * width) * horizontal_step + left;
				uv.Y = v;
				m_Buffers.Colors[m_Counts.Colors++] = InterpolateMotionGridColor(first, last, column, input.Subdivision.X);
			}
		}

		for (std::uint32_t row = 0; row < input.Subdivision.Y; ++row) {
			const auto first = base + static_cast<std::size_t>(row) * columns;
			const auto bottom = first + columns;
			m_Buffers.Indices[m_Counts.Indices++] = static_cast<std::uint32_t>(first);
			for (std::size_t column = 0; column < columns; ++column) {
				m_Buffers.Indices[m_Counts.Indices++] = static_cast<std::uint32_t>(first + column);
				m_Buffers.Indices[m_Counts.Indices++] = static_cast<std::uint32_t>(bottom + column);
			}
			m_Buffers.Indices[m_Counts.Indices++] = static_cast<std::uint32_t>(bottom + columns - 1);
		}
	}

	void MotionQuadBatch::Flush(const Submission& submit) {
		if (m_Submitting) {
			throw std::runtime_error("motion quad batch cannot flush recursively");
		}

		const auto plan = PrepareMotionBatchFlush(m_Mode, m_Key, m_Buffers.Positions.first(m_Counts.Positions), m_Buffers.Uvs.first(m_Counts.Uvs), m_Buffers.Colors.first(m_Counts.Colors), m_Buffers.Indices.first(m_Counts.Indices));
		if (!plan.Submission) {
			return;
		}

		if (!submit) {
			throw std::runtime_error("motion quad batch requires a submission callback");
		}

		m_Submitting = true;
		try {
			submit(*plan.Submission);
		} catch (...) {
			m_Submitting = false;

			throw;
		}

		m_Submitting = false;
		m_Counts = plan.RetainedCounts;
	}

	MotionBatchCounts MotionQuadBatch::Counts() const noexcept {
		return m_Counts;
	}

	std::uint32_t MotionQuadBatch::Key() const noexcept {
		return m_Key;
	}

} // namespace sgre::scene
