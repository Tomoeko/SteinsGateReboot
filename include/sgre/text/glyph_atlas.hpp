#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace sgre::text {

	struct GlyphAtlasPacking {
		std::uint32_t WidthPixels = 1024;
		std::uint32_t HeightPixels = 1024;
		std::uint32_t PenXPixels = 0;
		std::uint32_t PenYPixels = 0;
		std::uint32_t RowHeightPixels = 0;
	};

	// Pixels borrow caller-owned BGRA8 storage; the owner must outlive every allocation's use.
	struct GlyphAtlasCpuSurface {
		std::span<std::uint8_t> Pixels;
		std::uint32_t RowPitchBytes = 0;
		bool Enabled = true;
		bool Locked = false;
		bool UploadRequired = false;
	};

	struct GlyphAtlasAllocation {
		std::uint32_t LeftPixels = 0;
		std::uint32_t TopPixels = 0;
		std::uint32_t WidthPixels = 0;
		std::uint32_t HeightPixels = 0;
		std::uint32_t RowPitchBytes = 0;
		std::size_t DataOffsetBytes = 0;
	};

	// Applies the native row wrap and height gate without mapping or advancing a successful allocation.
	[[nodiscard]] bool PrepareGlyphAtlasRow(GlyphAtlasPacking& packing, std::uint32_t width_pixels, std::uint32_t height_pixels);

	// A row wrap survives a later height/mapping failure, matching the native packing owner.
	// A successful mapping clears the full one-pixel border and marks the CPU surface locked/dirty.
	[[nodiscard]] std::optional<GlyphAtlasAllocation> AllocateGlyphAtlasRegion(GlyphAtlasPacking& packing, GlyphAtlasCpuSurface& surface, std::uint32_t width_pixels, std::uint32_t height_pixels);
	void UnlockGlyphAtlasSurface(GlyphAtlasCpuSurface& surface) noexcept;

} // namespace sgre::text
