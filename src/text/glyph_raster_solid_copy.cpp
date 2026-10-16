#include "sgre/text/glyph_raster_solid_copy.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace sgre::text {
	static constexpr std::size_t kMaximumSurfacePixels = 1024 * 1024;

	static std::uint8_t CoverageWeight(std::uint32_t area, std::uint32_t mask, std::uint8_t opacity) {
		const auto shifted = std::bit_cast<std::uint32_t>(std::bit_cast<std::int32_t>(area) >> 8);
		auto difference = (shifted & mask) - 512;
		if (std::bit_cast<std::int32_t>(difference) < 0) {
			difference = std::uint32_t(0) - difference;
		}

		if (std::bit_cast<std::int32_t>(difference) > 512) {
			difference = 512;
		}

		const auto product = difference * (std::uint32_t(opacity) << 7);

		return static_cast<std::uint8_t>(product >> 16);
	}

	static std::uint32_t BlendPackedPixel(std::uint32_t source, std::uint32_t destination, std::uint8_t weight) {
		std::uint32_t result = 0;
		for (std::uint32_t shift = 0; shift < 32; shift += 8) {
			const auto weighted = ((source >> shift) & 255) * weight + ((destination >> shift) & 255) * (255 - std::uint32_t(weight));
			// The static native kernel omits a rounding constant and preserves its endpoint approximation.
			const auto channel = ((weighted + (weighted >> 8)) >> 8) & 255;
			result |= channel << shift;
		}

		return result;
	}

	static void ValidatePreparedCells(const GlyphRasterAreaCells& cells, std::uint16_t height_pixels, std::uint32_t source, GlyphRasterFillRule fill_rule) {

		if (std::uint32_t(cells.FirstRowPixel) + cells.RowCount > height_pixels || cells.Deltas.size() != std::size_t(cells.RowCount) * (std::size_t(cells.WidthPixels) + 1) || cells.ActiveGroups.size() != std::size_t(cells.RowCount) * ((std::size_t(cells.WidthPixels) + 4) / 4)) {
			throw std::runtime_error("glyph raster solid-copy prepared area storage is invalid");
		}

		if (fill_rule != GlyphRasterFillRule::NonZero && fill_rule != GlyphRasterFillRule::EvenOdd) {
			throw std::runtime_error("glyph raster solid-copy fill rule is invalid");
		}

		const auto alpha = source >> 24;
		if ((source & 255) > alpha || ((source >> 8) & 255) > alpha || ((source >> 16) & 255) > alpha) {
			throw std::runtime_error("glyph raster solid-copy source color is not premultiplied");
		}

		for (const auto flag : cells.ActiveGroups) {
			if (flag > 1) {
				throw std::runtime_error("glyph raster solid-copy active group flag is invalid");
			}
		}
	}

	template <typename PixelReader, typename PixelWriter>
	static void CompositeRows(const GlyphRasterAreaCells& cells, std::uint32_t premultiplied_bgra32, std::uint8_t opacity, GlyphRasterFillRule fill_rule, const PixelReader& read_pixel, const PixelWriter& write_pixel) {

		const auto group_stride = (std::size_t(cells.WidthPixels) + 4) / 4;
		const auto cell_stride = std::size_t(cells.WidthPixels) + 1;
		const auto mask = fill_rule == GlyphRasterFillRule::NonZero ? std::uint32_t(0xfffffffe) : std::uint32_t(0x3fe);
		for (std::size_t row = 0; row < cells.RowCount; ++row) {
			const auto groups = std::span(cells.ActiveGroups).subspan(row * group_stride, group_stride);
			const auto first = std::find(groups.begin(), groups.end(), std::uint8_t(1));
			if (first == groups.end()) {
				continue;
			}

			const auto last = std::find(groups.rbegin(), groups.rend(), std::uint8_t(1));
			const auto start_x = static_cast<std::size_t>(first - groups.begin()) * 4;
			const auto end_x = std::min((group_stride - static_cast<std::size_t>(last - groups.rbegin())) * 4, std::size_t(cells.WidthPixels));
			auto area = std::uint32_t(0x20000);
			const auto destination_row = std::size_t(cells.FirstRowPixel) + row;
			for (auto column = start_x; column < end_x; ++column) {
				area += cells.Deltas[row * cell_stride + column];
				const auto weight = CoverageWeight(area, mask, opacity);
				const auto destination = read_pixel(destination_row, column);
				if (groups[column / 4] != 0) {
					write_pixel(destination_row, column, BlendPackedPixel(premultiplied_bgra32, destination, weight));
				} else if (weight == 255) {
					write_pixel(destination_row, column, premultiplied_bgra32);
				} else if (weight != 0) {
					write_pixel(destination_row, column, BlendPackedPixel(premultiplied_bgra32, destination, weight));
				}
			}
		}
	}
	static void ValidateSurfaceDimensions(const GlyphRasterAreaCells& cells, std::uint16_t height_pixels) {
		if (cells.WidthPixels == 0 || cells.WidthPixels == 65535 || height_pixels == 0 || height_pixels == 65535 || std::size_t(cells.WidthPixels) > kMaximumSurfacePixels / height_pixels) {
			throw std::runtime_error("glyph raster solid-copy surface dimensions or allocation limit are invalid");
		}
	}

	void CompositeGlyphRasterSolidCopy(const GlyphRasterAreaCells& cells, std::span<std::uint32_t> surface, std::uint16_t height_pixels, std::uint32_t premultiplied_bgra32, std::uint8_t opacity, GlyphRasterFillRule fill_rule) {
		ValidateSurfaceDimensions(cells, height_pixels);
		if (surface.size() != std::size_t(cells.WidthPixels) * height_pixels) {
			throw std::runtime_error("glyph raster solid-copy surface dimensions or allocation limit are invalid");
		}

		ValidatePreparedCells(cells, height_pixels, premultiplied_bgra32, fill_rule);
		const auto read_pixel = [&](std::size_t row, std::size_t column) {
			return surface[row * cells.WidthPixels + column];
		};
		const auto write_pixel = [&](std::size_t row, std::size_t column, std::uint32_t pixel) {
			surface[row * cells.WidthPixels + column] = pixel;
		};
		CompositeRows(cells, premultiplied_bgra32, opacity, fill_rule, read_pixel, write_pixel);
	}

	void CompositeGlyphRasterSolidCopyBytes(const GlyphRasterAreaCells& cells, std::span<std::uint8_t> surface, std::uint16_t height_pixels, std::uint32_t row_pitch_bytes, std::uint32_t premultiplied_bgra32, std::uint8_t opacity, GlyphRasterFillRule fill_rule) {
		ValidateSurfaceDimensions(cells, height_pixels);
		const auto row_bytes = std::size_t(cells.WidthPixels) * 4;
		const auto preceding_rows = std::size_t(height_pixels) - 1;
		if (row_pitch_bytes < row_bytes || (preceding_rows != 0 && row_pitch_bytes > surface.size() / preceding_rows)) {
			throw std::runtime_error("glyph raster solid-copy byte surface pitch or storage is invalid");
		}

		const auto last_row_offset = preceding_rows * row_pitch_bytes;
		if (last_row_offset > surface.size() || row_bytes > surface.size() - last_row_offset) {
			throw std::runtime_error("glyph raster solid-copy byte surface pitch or storage is invalid");
		}

		ValidatePreparedCells(cells, height_pixels, premultiplied_bgra32, fill_rule);
		const auto read_pixel = [&](std::size_t row, std::size_t column) {
			const auto offset = row * row_pitch_bytes + column * 4;

			return std::uint32_t(surface[offset]) | (std::uint32_t(surface[offset + 1]) << 8) | (std::uint32_t(surface[offset + 2]) << 16) | (std::uint32_t(surface[offset + 3]) << 24);
		};
		const auto write_pixel = [&](std::size_t row, std::size_t column, std::uint32_t pixel) {
			const auto offset = row * row_pitch_bytes + column * 4;
			for (std::uint32_t channel = 0; channel < 4; ++channel) {
				surface[offset + channel] = static_cast<std::uint8_t>(pixel >> (channel * 8));
			}
		};
		CompositeRows(cells, premultiplied_bgra32, opacity, fill_rule, read_pixel, write_pixel);
	}

} // namespace sgre::text
