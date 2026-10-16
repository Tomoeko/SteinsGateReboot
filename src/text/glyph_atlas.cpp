#include "sgre/text/glyph_atlas.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace sgre::text {

	static constexpr std::uint32_t kGlyphBorderPixels = 1;
	static constexpr std::uint32_t kGlyphPaddingPixels = 2 * kGlyphBorderPixels;
	static constexpr std::uint32_t kAtlasBytesPerPixel = 4;
	static constexpr std::uint32_t kMaximumImageExtentPixels = 65534;

	static void ValidatePacking(const GlyphAtlasPacking& packing) {
		if (packing.WidthPixels == 0 || packing.HeightPixels == 0 || packing.WidthPixels > kMaximumImageExtentPixels || packing.HeightPixels > kMaximumImageExtentPixels) {
			throw std::runtime_error("glyph atlas dimensions exceed the supported native image range");
		}
		if (packing.PenXPixels > packing.WidthPixels || packing.PenYPixels > packing.HeightPixels || packing.RowHeightPixels > packing.HeightPixels - packing.PenYPixels) {
			throw std::runtime_error("glyph atlas row state exceeds its dimensions");
		}
	}

	static void ValidateSurface(const GlyphAtlasPacking& packing, const GlyphAtlasCpuSurface& surface) {
		const auto minimum_pitch = packing.WidthPixels * kAtlasBytesPerPixel;
		if (surface.RowPitchBytes < minimum_pitch || surface.RowPitchBytes > std::numeric_limits<std::size_t>::max() / packing.HeightPixels) {
			throw std::runtime_error("glyph atlas CPU row pitch is shorter than its BGRA extent or overflows storage");
		}
		if (surface.Pixels.size() < std::size_t(surface.RowPitchBytes) * packing.HeightPixels) {
			throw std::runtime_error("glyph atlas CPU storage is shorter than its rows");
		}
	}

	static void ClearPaddedRegion(const GlyphAtlasPacking& packing, GlyphAtlasCpuSurface& surface, std::uint32_t padded_width, std::uint32_t padded_height) {
		const auto clear_bytes = std::size_t(padded_width) * kAtlasBytesPerPixel;
		for (std::size_t row = 0; row < padded_height; ++row) {
			const auto offset = (std::size_t(packing.PenYPixels) + row) * surface.RowPitchBytes + std::size_t(packing.PenXPixels) * kAtlasBytesPerPixel;
			std::fill_n(surface.Pixels.data() + offset, clear_bytes, std::uint8_t(0));
		}
	}

	bool PrepareGlyphAtlasRow(GlyphAtlasPacking& packing, std::uint32_t width_pixels, std::uint32_t height_pixels) {
		ValidatePacking(packing);
		if (width_pixels == 0 || height_pixels == 0 || width_pixels > kMaximumImageExtentPixels || height_pixels > kMaximumImageExtentPixels) {
			throw std::runtime_error("glyph atlas requires nonempty bounded glyph dimensions");
		}

		const auto padded_width = width_pixels + kGlyphPaddingPixels;
		const auto padded_height = height_pixels + kGlyphPaddingPixels;
		if (padded_width > packing.WidthPixels) {
			throw std::runtime_error("glyph atlas padded glyph width exceeds its row storage");
		}
		if (packing.PenXPixels + padded_width >= packing.WidthPixels) {
			packing.PenYPixels += packing.RowHeightPixels;
			packing.PenXPixels = 0;
			packing.RowHeightPixels = 0;
		}
		return padded_height < packing.HeightPixels - packing.PenYPixels;
	}

	std::optional<GlyphAtlasAllocation> AllocateGlyphAtlasRegion(GlyphAtlasPacking& packing, GlyphAtlasCpuSurface& surface, std::uint32_t width_pixels, std::uint32_t height_pixels) {
		if (!PrepareGlyphAtlasRow(packing, width_pixels, height_pixels) || !surface.Enabled || surface.Locked || surface.Pixels.empty()) {
			return std::nullopt;
		}

		const auto padded_width = width_pixels + kGlyphPaddingPixels;
		const auto padded_height = height_pixels + kGlyphPaddingPixels;
		ValidateSurface(packing, surface);
		ClearPaddedRegion(packing, surface, padded_width, padded_height);
		GlyphAtlasAllocation result;
		result.LeftPixels = packing.PenXPixels + kGlyphBorderPixels;
		result.TopPixels = packing.PenYPixels + kGlyphBorderPixels;
		result.WidthPixels = width_pixels;
		result.HeightPixels = height_pixels;
		result.RowPitchBytes = surface.RowPitchBytes;
		result.DataOffsetBytes = std::size_t(result.TopPixels) * surface.RowPitchBytes + std::size_t(result.LeftPixels) * kAtlasBytesPerPixel;
		packing.PenXPixels += padded_width;
		packing.RowHeightPixels = std::max(packing.RowHeightPixels, padded_height);
		surface.Locked = true;
		surface.UploadRequired = true;

		return result;
	}

	void UnlockGlyphAtlasSurface(GlyphAtlasCpuSurface& surface) noexcept {
		surface.Locked = false;
	}

} // namespace sgre::text
