#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace sgre::scene {

	struct RawTextureSurface {
		std::span<std::byte> Bytes;
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
		std::size_t RowBytes = 0;

		RawTextureSurface() = default;
		RawTextureSurface(std::span<std::byte> bytes, std::uint32_t width, std::uint32_t height, std::size_t row_bytes) : Bytes(bytes), Width(width), Height(height), RowBytes(row_bytes) {
		}
		RawTextureSurface(std::span<std::uint8_t> bytes, std::uint32_t width, std::uint32_t height, std::size_t row_bytes) : RawTextureSurface(std::as_writable_bytes(bytes), width, height, row_bytes) {
		}
	};

	// Both inputs are packed rows and borrow immutable storage separate from the mapped destination.
	// Mapping, upload-dirty publication and unlock belong to the texture owner.
	void RestoreRawTextureBgra(const RawTextureSurface& destination, std::span<const std::uint8_t> bgra, std::uint32_t width, std::uint32_t height);
	void RestoreRawTextureRgb(const RawTextureSurface& destination, std::span<const std::uint8_t> rgb);

} // namespace sgre::scene
