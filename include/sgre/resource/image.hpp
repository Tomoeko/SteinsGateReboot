#pragma once

#include "sgre/resource/psb.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace sgre::resource {

	enum class PixelFormat {
		Alpha8,
		Luminance8,
		Bc7
	};

	struct ImageTexture {
		std::optional<std::uint32_t> SerializedValueOffset;
		PixelFormat Format = PixelFormat::Alpha8;
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
		std::uint32_t TruncatedWidth = 0;
		std::uint32_t TruncatedHeight = 0;
		PsbChunkRange PixelRange;
	};

	struct ImageTile {
		std::uint32_t Left = 0;
		std::uint32_t Top = 0;
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
		ImageTexture Texture;
	};

	struct ImageEntry {
		std::string Label;
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
		std::vector<ImageTile> Tiles;
	};

	struct ImageResource {
		std::string Label;
		std::vector<ImageEntry> Images;
	};

	struct Bc7TextureView {
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
		std::size_t BlockRowBytes = 0;
		// Borrows the original chunk only through the synchronous upload; never retain this span in a cache record.
		std::span<const std::uint8_t> Blocks;
	};

	struct RgbaImage {
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
		// Top-to-bottom rows. Channels match the native texture's sampled RGBA values.
		std::vector<std::uint8_t> Pixels;
	};

	[[nodiscard]] ImageTexture ReadImageTexture(const PsbDocument& document, const PsbValue& value);
	[[nodiscard]] ImageResource ReadImageResource(const PsbDocument& document);
	[[nodiscard]] Bc7TextureView BorrowBc7Texture(const ImageTexture& texture, std::span<const std::uint8_t> psb_bytes, std::size_t max_decoded_bytes = 64 * 1024 * 1024);
	[[nodiscard]] RgbaImage DecodeTexture(const ImageTexture& texture, std::span<const std::uint8_t> psb_bytes, std::size_t max_decoded_bytes = 64 * 1024 * 1024);
	[[nodiscard]] RgbaImage DecodeImage(const ImageEntry& image, std::span<const std::uint8_t> psb_bytes, std::size_t max_decoded_bytes = 64 * 1024 * 1024);

} // namespace sgre::resource
