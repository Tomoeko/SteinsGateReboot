#include "sgre/text/glyph_raster_cells.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace sgre::text {
	static constexpr std::size_t kMaximumCells = 1024 * 1024;
	static constexpr std::size_t kMaximumEdgePoints = 1024 * 1024;
	static constexpr std::size_t kMaximumAreaPieces = 4 * 1024 * 1024;

	static std::int32_t ProjectMagnitude(std::int32_t distance, std::int32_t extent, std::int32_t denominator) {
		// Native quotient/remainder carries start with floor(denominator / 2) - 1.
		const auto numerator = std::int64_t(distance) * extent;

		return static_cast<std::int32_t>((numerator + denominator - denominator / 2) / denominator);
	}

	static void AppendCellPiece(GlyphRasterPoint first, GlyphRasterPoint last, std::int32_t row, bool reverse, GlyphRasterAreaCells& output, std::size_t& remaining_work) {
		if (remaining_work == 0) {
			throw std::runtime_error("glyph raster area traversal exceeds bounded work limit");
		}

		--remaining_work;
		const auto height = last.Y24Dot8 - first.Y24Dot8;
		const auto column = std::min(first.X24Dot8, last.X24Dot8) / 256;
		const auto fractions = first.X24Dot8 + last.X24Dot8 - column * 512;
		if (fractions < 0 || fractions > 512 || row < output.FirstRowPixel || row - output.FirstRowPixel >= output.RowCount || column < 0 || column > output.WidthPixels) {
			throw std::runtime_error("glyph raster area piece lies outside bounded cell geometry");
		}

		const auto offset = std::size_t(row - output.FirstRowPixel) * (std::size_t(output.WidthPixels) + 1) + static_cast<std::size_t>(column);
		const auto group_stride = (std::size_t(output.WidthPixels) + 4) / 4;
		output.ActiveGroups[std::size_t(row - output.FirstRowPixel) * group_stride + static_cast<std::size_t>(column / 4)] = 1;
		const auto sign = reverse ? -1 : 1;
		output.Deltas[offset] += static_cast<std::uint32_t>(sign * height * (512 - fractions));
		if (fractions != 0) {
			if (column == output.WidthPixels) {
				throw std::runtime_error("glyph raster area contribution exceeds final cell");
			}

			output.Deltas[offset + 1] += static_cast<std::uint32_t>(sign * height * fractions);
		}
	}

	static void AccumulateLine(GlyphRasterPoint first, GlyphRasterPoint last, bool reverse, GlyphRasterAreaCells& output, std::size_t& remaining_work) {
		const auto dy = last.Y24Dot8 - first.Y24Dot8;
		if (dy == 0) {
			return;
		}

		const auto direction = last.X24Dot8 < first.X24Dot8 ? -1 : 1;
		const auto dx = (last.X24Dot8 - first.X24Dot8) * direction;
		auto row_start = first.Y24Dot8;
		while (row_start < last.Y24Dot8) {
			const auto row_end = std::min(last.Y24Dot8, (row_start / 256 + 1) * 256);
			const auto start_x = row_start == first.Y24Dot8 ? first.X24Dot8 : first.X24Dot8 + direction * ProjectMagnitude(row_start - first.Y24Dot8, dx, dy);
			const auto end_x = row_end == last.Y24Dot8 ? last.X24Dot8 : first.X24Dot8 + direction * ProjectMagnitude(row_end - first.Y24Dot8, dx, dy);
			GlyphRasterPoint previous{start_x, row_start};
			auto next_x = direction > 0 ? (start_x / 256 + 1) * 256 : ((start_x - 1) / 256) * 256;
			// A boundary coinciding with a rounded row endpoint is not visited twice.
			while (dx != 0 && (next_x - start_x) * direction > 0 && (end_x - next_x) * direction > 0) {
				const auto projected_y = first.Y24Dot8 + ProjectMagnitude((next_x - first.X24Dot8) * direction, dy, dx);
				const GlyphRasterPoint point{next_x, std::clamp(projected_y, row_start, row_end)};
				AppendCellPiece(previous, point, row_start / 256, reverse, output, remaining_work);
				previous = point;
				next_x += direction * 256;
			}

			AppendCellPiece(previous, {end_x, row_end}, row_start / 256, reverse, output, remaining_work);
			row_start = row_end;
		}
	}

	GlyphRasterAreaCells AccumulateGlyphRasterCells(std::span<const GlyphRasterEdge> edges, const GlyphRasterClip& clip) {
		if (clip.WidthPixels == 0 || clip.HeightPixels == 0 || clip.WidthPixels == 65535 || clip.HeightPixels == 65535 || (clip.BandHeightPixels != 8 && clip.BandHeightPixels != 16 && clip.BandHeightPixels != 32 && clip.BandHeightPixels != 64)) {
			throw std::runtime_error("glyph raster cells dimensions or band height are invalid");
		}

		auto first_row = std::uint32_t(clip.HeightPixels);
		std::uint32_t last_row = 0;
		std::size_t point_count = 0;
		for (const auto& edge : edges) {
			if (edge.Points.size() < 2 || edge.Points.size() > kMaximumEdgePoints - point_count) {
				throw std::runtime_error("glyph raster area edge point count is invalid");
			}

			point_count += edge.Points.size();
			for (std::size_t index = 0; index < edge.Points.size(); ++index) {
				const auto point = edge.Points[index];
				if (point.X24Dot8 < 0 || point.Y24Dot8 < 0 || point.X24Dot8 > std::int32_t(clip.WidthPixels) * 256 || point.Y24Dot8 > std::int32_t(clip.HeightPixels) * 256 || (index != 0 && point.Y24Dot8 < edge.Points[index - 1].Y24Dot8)) {
					throw std::runtime_error("glyph raster area edge coordinate/direction is invalid");
				}
			}

			if (edge.Points.front().Y24Dot8 >= edge.Points.back().Y24Dot8) {
				throw std::runtime_error("glyph raster area edge must have positive Y extent");
			}

			if (edge.BandIndex != static_cast<std::uint32_t>(edge.Points.front().Y24Dot8) / (std::uint32_t(clip.BandHeightPixels) * 256)) {
				throw std::runtime_error("glyph raster area edge band provenance is invalid");
			}

			first_row = std::min(first_row, static_cast<std::uint32_t>(edge.Points.front().Y24Dot8 / 256));
			last_row = std::max(last_row, static_cast<std::uint32_t>((edge.Points.back().Y24Dot8 + 255) / 256));
		}

		GlyphRasterAreaCells output;
		output.WidthPixels = clip.WidthPixels;
		if (edges.empty() || first_row == last_row) {
			return output;
		}

		const auto row_count = last_row - first_row;
		if (row_count > kMaximumCells / (std::size_t(clip.WidthPixels) + 1)) {
			throw std::runtime_error("glyph raster area cells exceed bounded allocation limit");
		}

		output.FirstRowPixel = static_cast<std::uint16_t>(first_row);
		output.RowCount = static_cast<std::uint16_t>(row_count);
		output.Deltas.resize(row_count * (std::size_t(clip.WidthPixels) + 1));
		output.ActiveGroups.resize(row_count * ((std::size_t(clip.WidthPixels) + 4) / 4));
		std::size_t remaining_work = kMaximumAreaPieces;
		for (const auto& edge : edges) {
			for (std::size_t index = 1; index < edge.Points.size(); ++index) {
				AccumulateLine(edge.Points[index - 1], edge.Points[index], edge.ReverseWinding, output, remaining_work);
			}
		}

		return output;
	}
} // namespace sgre::text
