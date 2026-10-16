#include "sgre/resource/image.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

namespace sgre::resource {

	static constexpr std::uint32_t kMaxImageDimension = 16384;
	static constexpr std::size_t kMaxImageEntries = 65536;
	static constexpr std::size_t kMaxImageTiles = 65536;

	static const PsbValue& RequireField(const PsbValue& value, std::string_view name) {
		const auto* field = value.Find(name);
		if (field == nullptr) {
			throw std::runtime_error("image field is missing: " + std::string(name));
		}

		return *field;
	}

	static std::string ReadText(const PsbValue& value, std::string_view name) {
		const auto* text = std::get_if<std::string>(&RequireField(value, name).Data);
		if (text == nullptr) {
			throw std::runtime_error("image field is not a string: " + std::string(name));
		}

		return *text;
	}

	static std::uint32_t ReadInteger(const PsbValue& value, std::string_view name, bool allow_zero = false) {
		const auto& data = RequireField(value, name).Data;
		const auto* integer = std::get_if<std::int64_t>(&data);
		const auto* real = std::get_if<double>(&data);
		const auto number = integer != nullptr ? static_cast<double>(*integer) : real != nullptr ? *real
		                                                                                         : -1.0;
		if (!std::isfinite(number) || std::trunc(number) != number || number < (allow_zero ? 0 : 1) || number > kMaxImageDimension) {
			throw std::runtime_error("image field exceeds its dimension limit: " + std::string(name));
		}

		return static_cast<std::uint32_t>(number);
	}

	static std::uint64_t TextureByteCount(const ImageTexture& texture) {
		if (texture.Width == 0 || texture.Height == 0 || texture.Width > kMaxImageDimension || texture.Height > kMaxImageDimension) {
			throw std::runtime_error("texture dimensions exceed the supported limit");
		}

		switch (texture.Format) {
		case PixelFormat::Alpha8:
		case PixelFormat::Luminance8:
			return static_cast<std::uint64_t>(texture.Width) * texture.Height;
		case PixelFormat::Bc7:
			return static_cast<std::uint64_t>((texture.Width + 3) / 4) * ((texture.Height + 3) / 4) * 16;
		}

		throw std::runtime_error("unsupported texture pixel format");
	}

	ImageTexture ReadImageTexture(const PsbDocument& document, const PsbValue& value) {
		ImageTexture texture;
		texture.SerializedValueOffset = value.SerializedOffset;
		const auto type = ReadText(value, "type");
		if (type == "A8") {
			texture.Format = PixelFormat::Alpha8;
		} else if (type == "L8") {
			texture.Format = PixelFormat::Luminance8;
		} else if (type == "BC7") {
			texture.Format = PixelFormat::Bc7;
		} else {
			throw std::runtime_error("unsupported image texture type: " + type);
		}

		texture.Width = ReadInteger(value, "width");
		texture.Height = ReadInteger(value, "height");
		texture.TruncatedWidth = ReadInteger(value, "truncated_width");
		texture.TruncatedHeight = ReadInteger(value, "truncated_height");
		if (texture.TruncatedWidth > texture.Width || texture.TruncatedHeight > texture.Height) {
			throw std::runtime_error("texture truncated dimensions exceed the stored dimensions");
		}

		const auto* compression = value.Find("compress");
		if (compression != nullptr) {
			const auto* number = std::get_if<std::int64_t>(&compression->Data);
			if (number == nullptr || *number != 0) {
				throw std::runtime_error("unsupported image texture compression");
			}
		}

		const auto* reference = std::get_if<PsbChunkReference>(&RequireField(value, "pixel").Data);
		if (reference == nullptr) {
			throw std::runtime_error("texture pixel field is not a chunk reference");
		}

		const auto& chunks = reference->Extra ? document.ExtraChunks : document.Chunks;
		if (reference->Index >= chunks.size()) {
			throw std::runtime_error("texture pixel chunk index exceeds the chunk table");
		}

		texture.PixelRange = chunks[reference->Index];
		if (texture.PixelRange.Size != TextureByteCount(texture)) {
			throw std::runtime_error("texture pixel chunk length does not match its format and dimensions");
		}

		return texture;
	}

	static ImageEntry ReadImageEntry(const PsbDocument& document, const PsbValue& value) {
		ImageEntry image;
		image.Label = ReadText(value, "label");
		image.Width = ReadInteger(value, "width");
		image.Height = ReadInteger(value, "height");
		const auto* tiles = std::get_if<PsbArray>(&RequireField(value, "texture").Data);
		if (tiles == nullptr || tiles->size() > kMaxImageTiles) {
			throw std::runtime_error("image texture array is invalid or exceeds its entry limit");
		}

		image.Tiles.reserve(tiles->size());
		for (std::size_t index = 0; index < tiles->size(); ++index) {
			try {
				const auto& value = (*tiles)[index];
				ImageTile tile;
				tile.Left = ReadInteger(value, "left", true);
				tile.Top = ReadInteger(value, "top", true);
				tile.Width = ReadInteger(value, "width");
				tile.Height = ReadInteger(value, "height");
				tile.Texture = ReadImageTexture(document, RequireField(value, "image"));
				if (tile.Width > tile.Texture.Width || tile.Height > tile.Texture.Height || tile.Left >= image.Width || tile.Top >= image.Height) {
					throw std::runtime_error("image tile dimensions or origin are outside the texture or logical image");
				}

				image.Tiles.push_back(std::move(tile));
			} catch (const std::exception& error) {
				throw std::runtime_error("image " + image.Label + ", tile " + std::to_string(index) + ": " + error.what());
			}
		}

		return image;
	}

	ImageResource ReadImageResource(const PsbDocument& document) {
		if ((document.Header.Version != 3 && document.Header.Version != 4) || document.Header.Flags != 0) {
			throw std::runtime_error("image resource requires an unfiltered PSB version 3 or 4");
		}

		if (ReadText(document.Root, "id") != "image" || ReadText(document.Root, "spec") != "win") {
			throw std::runtime_error("image resource requires id=image and spec=win");
		}

		const auto& version = RequireField(document.Root, "version");
		const auto* integer = std::get_if<std::int64_t>(&version.Data);
		const auto* real = std::get_if<double>(&version.Data);
		if ((integer == nullptr || *integer != 1) && (real == nullptr || *real != 1.0)) {
			throw std::runtime_error("unsupported image schema version");
		}

		const auto* images = std::get_if<PsbArray>(&RequireField(document.Root, "imageList").Data);
		if (images == nullptr || images->size() > kMaxImageEntries) {
			throw std::runtime_error("imageList is invalid or exceeds its entry limit");
		}

		ImageResource resource;
		resource.Label = ReadText(document.Root, "label");
		resource.Images.reserve(images->size());
		for (const auto& image : *images) {
			resource.Images.push_back(ReadImageEntry(document, image));
		}

		return resource;
	}

	struct Bc7Partition {
		std::array<std::uint8_t, 16> Subsets;
		std::array<std::uint8_t, 3> Anchors;
	};

	// Numeric format constants from Khronos DataFormat BPTC partition and anchor tables.
	// See docs/recovery/images.md for the specification and decoder validation.
	static constexpr std::array<Bc7Partition, 64> kBc7Partitions2 = {{
	    {{0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1}, {0, 15, 0}},
	    {{0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1}, {0, 15, 0}},
	    {{0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1}, {0, 15, 0}},
	    {{0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 1, 1, 1}, {0, 15, 0}},
	    {{0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 1}, {0, 15, 0}},
	    {{0, 0, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1}, {0, 15, 0}},
	    {{0, 0, 0, 1, 0, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1}, {0, 15, 0}},
	    {{0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 1, 0, 1, 1, 1}, {0, 15, 0}},
	    {{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 1}, {0, 15, 0}},
	    {{0, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}, {0, 15, 0}},
	    {{0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1}, {0, 15, 0}},
	    {{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 1}, {0, 15, 0}},
	    {{0, 0, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}, {0, 15, 0}},
	    {{0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1}, {0, 15, 0}},
	    {{0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}, {0, 15, 0}},
	    {{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1}, {0, 15, 0}},
	    {{0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 0, 1, 1, 1, 1}, {0, 15, 0}},
	    {{0, 1, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0}, {0, 2, 0}},
	    {{0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 0}, {0, 8, 0}},
	    {{0, 1, 1, 1, 0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0}, {0, 2, 0}},
	    {{0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0}, {0, 2, 0}},
	    {{0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0, 1, 1, 1, 0}, {0, 8, 0}},
	    {{0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0}, {0, 8, 0}},
	    {{0, 1, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 0, 1}, {0, 15, 0}},
	    {{0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0}, {0, 2, 0}},
	    {{0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0}, {0, 8, 0}},
	    {{0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0}, {0, 2, 0}},
	    {{0, 0, 1, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 1, 0, 0}, {0, 2, 0}},
	    {{0, 0, 0, 1, 0, 1, 1, 1, 1, 1, 1, 0, 1, 0, 0, 0}, {0, 8, 0}},
	    {{0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0}, {0, 8, 0}},
	    {{0, 1, 1, 1, 0, 0, 0, 1, 1, 0, 0, 0, 1, 1, 1, 0}, {0, 2, 0}},
	    {{0, 0, 1, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 1, 0, 0}, {0, 2, 0}},
	    {{0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1}, {0, 15, 0}},
	    {{0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1}, {0, 15, 0}},
	    {{0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0}, {0, 6, 0}},
	    {{0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 0, 0}, {0, 8, 0}},
	    {{0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0}, {0, 2, 0}},
	    {{0, 1, 0, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1, 0}, {0, 8, 0}},
	    {{0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1}, {0, 15, 0}},
	    {{0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 1}, {0, 15, 0}},
	    {{0, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 0}, {0, 2, 0}},
	    {{0, 0, 0, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 0, 0, 0}, {0, 8, 0}},
	    {{0, 0, 1, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 1, 0, 0}, {0, 2, 0}},
	    {{0, 0, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 0, 0}, {0, 2, 0}},
	    {{0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0}, {0, 2, 0}},
	    {{0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 0, 0, 0, 0, 1, 1}, {0, 15, 0}},
	    {{0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1}, {0, 15, 0}},
	    {{0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 0, 0, 0}, {0, 6, 0}},
	    {{0, 1, 0, 0, 1, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0}, {0, 6, 0}},
	    {{0, 0, 1, 0, 0, 1, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0}, {0, 2, 0}},
	    {{0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 1, 1, 0, 0, 1, 0}, {0, 6, 0}},
	    {{0, 0, 0, 0, 0, 1, 0, 0, 1, 1, 1, 0, 0, 1, 0, 0}, {0, 8, 0}},
	    {{0, 1, 1, 0, 1, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 1}, {0, 15, 0}},
	    {{0, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 0, 1, 0, 0, 1}, {0, 15, 0}},
	    {{0, 1, 1, 0, 0, 0, 1, 1, 1, 0, 0, 1, 1, 1, 0, 0}, {0, 2, 0}},
	    {{0, 0, 1, 1, 1, 0, 0, 1, 1, 1, 0, 0, 0, 1, 1, 0}, {0, 2, 0}},
	    {{0, 1, 1, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 0, 0, 1}, {0, 15, 0}},
	    {{0, 1, 1, 0, 0, 0, 1, 1, 0, 0, 1, 1, 1, 0, 0, 1}, {0, 15, 0}},
	    {{0, 1, 1, 1, 1, 1, 1, 0, 1, 0, 0, 0, 0, 0, 0, 1}, {0, 15, 0}},
	    {{0, 0, 0, 1, 1, 0, 0, 0, 1, 1, 1, 0, 0, 1, 1, 1}, {0, 15, 0}},
	    {{0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1}, {0, 15, 0}},
	    {{0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0}, {0, 2, 0}},
	    {{0, 0, 1, 0, 0, 0, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0}, {0, 2, 0}},
	    {{0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 0, 1, 1, 1}, {0, 15, 0}},
	}};

	static constexpr std::array<Bc7Partition, 64> kBc7Partitions3 = {{
	    {{0, 0, 1, 1, 0, 0, 1, 1, 0, 2, 2, 1, 2, 2, 2, 2}, {0, 3, 15}},
	    {{0, 0, 0, 1, 0, 0, 1, 1, 2, 2, 1, 1, 2, 2, 2, 1}, {0, 3, 8}},
	    {{0, 0, 0, 0, 2, 0, 0, 1, 2, 2, 1, 1, 2, 2, 1, 1}, {0, 15, 8}},
	    {{0, 2, 2, 2, 0, 0, 2, 2, 0, 0, 1, 1, 0, 1, 1, 1}, {0, 15, 3}},
	    {{0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 2, 1, 1, 2, 2}, {0, 8, 15}},
	    {{0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 2, 2, 0, 0, 2, 2}, {0, 3, 15}},
	    {{0, 0, 2, 2, 0, 0, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1}, {0, 15, 3}},
	    {{0, 0, 1, 1, 0, 0, 1, 1, 2, 2, 1, 1, 2, 2, 1, 1}, {0, 15, 8}},
	    {{0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2}, {0, 8, 15}},
	    {{0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2}, {0, 8, 15}},
	    {{0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2}, {0, 6, 15}},
	    {{0, 0, 1, 2, 0, 0, 1, 2, 0, 0, 1, 2, 0, 0, 1, 2}, {0, 6, 15}},
	    {{0, 1, 1, 2, 0, 1, 1, 2, 0, 1, 1, 2, 0, 1, 1, 2}, {0, 6, 15}},
	    {{0, 1, 2, 2, 0, 1, 2, 2, 0, 1, 2, 2, 0, 1, 2, 2}, {0, 5, 15}},
	    {{0, 0, 1, 1, 0, 1, 1, 2, 1, 1, 2, 2, 1, 2, 2, 2}, {0, 3, 15}},
	    {{0, 0, 1, 1, 2, 0, 0, 1, 2, 2, 0, 0, 2, 2, 2, 0}, {0, 3, 8}},
	    {{0, 0, 0, 1, 0, 0, 1, 1, 0, 1, 1, 2, 1, 1, 2, 2}, {0, 3, 15}},
	    {{0, 1, 1, 1, 0, 0, 1, 1, 2, 0, 0, 1, 2, 2, 0, 0}, {0, 3, 8}},
	    {{0, 0, 0, 0, 1, 1, 2, 2, 1, 1, 2, 2, 1, 1, 2, 2}, {0, 8, 15}},
	    {{0, 0, 2, 2, 0, 0, 2, 2, 0, 0, 2, 2, 1, 1, 1, 1}, {0, 15, 3}},
	    {{0, 1, 1, 1, 0, 1, 1, 1, 0, 2, 2, 2, 0, 2, 2, 2}, {0, 3, 15}},
	    {{0, 0, 0, 1, 0, 0, 0, 1, 2, 2, 2, 1, 2, 2, 2, 1}, {0, 3, 8}},
	    {{0, 0, 0, 0, 0, 0, 1, 1, 0, 1, 2, 2, 0, 1, 2, 2}, {0, 6, 15}},
	    {{0, 0, 0, 0, 1, 1, 0, 0, 2, 2, 1, 0, 2, 2, 1, 0}, {0, 10, 8}},
	    {{0, 1, 2, 2, 0, 1, 2, 2, 0, 0, 1, 1, 0, 0, 0, 0}, {0, 5, 3}},
	    {{0, 0, 1, 2, 0, 0, 1, 2, 1, 1, 2, 2, 2, 2, 2, 2}, {0, 8, 15}},
	    {{0, 1, 1, 0, 1, 2, 2, 1, 1, 2, 2, 1, 0, 1, 1, 0}, {0, 8, 6}},
	    {{0, 0, 0, 0, 0, 1, 1, 0, 1, 2, 2, 1, 1, 2, 2, 1}, {0, 6, 10}},
	    {{0, 0, 2, 2, 1, 1, 0, 2, 1, 1, 0, 2, 0, 0, 2, 2}, {0, 8, 15}},
	    {{0, 1, 1, 0, 0, 1, 1, 0, 2, 0, 0, 2, 2, 2, 2, 2}, {0, 5, 15}},
	    {{0, 0, 1, 1, 0, 1, 2, 2, 0, 1, 2, 2, 0, 0, 1, 1}, {0, 15, 10}},
	    {{0, 0, 0, 0, 2, 0, 0, 0, 2, 2, 1, 1, 2, 2, 2, 1}, {0, 15, 8}},
	    {{0, 0, 0, 0, 0, 0, 0, 2, 1, 1, 2, 2, 1, 2, 2, 2}, {0, 8, 15}},
	    {{0, 2, 2, 2, 0, 0, 2, 2, 0, 0, 1, 2, 0, 0, 1, 1}, {0, 15, 3}},
	    {{0, 0, 1, 1, 0, 0, 1, 2, 0, 0, 2, 2, 0, 2, 2, 2}, {0, 3, 15}},
	    {{0, 1, 2, 0, 0, 1, 2, 0, 0, 1, 2, 0, 0, 1, 2, 0}, {0, 5, 10}},
	    {{0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 0, 0, 0, 0}, {0, 6, 10}},
	    {{0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0}, {0, 10, 8}},
	    {{0, 1, 2, 0, 2, 0, 1, 2, 1, 2, 0, 1, 0, 1, 2, 0}, {0, 8, 9}},
	    {{0, 0, 1, 1, 2, 2, 0, 0, 1, 1, 2, 2, 0, 0, 1, 1}, {0, 15, 10}},
	    {{0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 0, 0, 0, 0, 1, 1}, {0, 15, 6}},
	    {{0, 1, 0, 1, 0, 1, 0, 1, 2, 2, 2, 2, 2, 2, 2, 2}, {0, 3, 15}},
	    {{0, 0, 0, 0, 0, 0, 0, 0, 2, 1, 2, 1, 2, 1, 2, 1}, {0, 15, 8}},
	    {{0, 0, 2, 2, 1, 1, 2, 2, 0, 0, 2, 2, 1, 1, 2, 2}, {0, 5, 15}},
	    {{0, 0, 2, 2, 0, 0, 1, 1, 0, 0, 2, 2, 0, 0, 1, 1}, {0, 15, 3}},
	    {{0, 2, 2, 0, 1, 2, 2, 1, 0, 2, 2, 0, 1, 2, 2, 1}, {0, 15, 6}},
	    {{0, 1, 0, 1, 2, 2, 2, 2, 2, 2, 2, 2, 0, 1, 0, 1}, {0, 15, 6}},
	    {{0, 0, 0, 0, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1}, {0, 15, 8}},
	    {{0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 2, 2, 2, 2}, {0, 3, 15}},
	    {{0, 2, 2, 2, 0, 1, 1, 1, 0, 2, 2, 2, 0, 1, 1, 1}, {0, 15, 3}},
	    {{0, 0, 0, 2, 1, 1, 1, 2, 0, 0, 0, 2, 1, 1, 1, 2}, {0, 5, 15}},
	    {{0, 0, 0, 0, 2, 1, 1, 2, 2, 1, 1, 2, 2, 1, 1, 2}, {0, 5, 15}},
	    {{0, 2, 2, 2, 0, 1, 1, 1, 0, 1, 1, 1, 0, 2, 2, 2}, {0, 5, 15}},
	    {{0, 0, 0, 2, 1, 1, 1, 2, 1, 1, 1, 2, 0, 0, 0, 2}, {0, 8, 15}},
	    {{0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 2, 2, 2, 2}, {0, 5, 15}},
	    {{0, 0, 0, 0, 0, 0, 0, 0, 2, 1, 1, 2, 2, 1, 1, 2}, {0, 10, 15}},
	    {{0, 1, 1, 0, 0, 1, 1, 0, 2, 2, 2, 2, 2, 2, 2, 2}, {0, 5, 15}},
	    {{0, 0, 2, 2, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 2, 2}, {0, 10, 15}},
	    {{0, 0, 2, 2, 1, 1, 2, 2, 1, 1, 2, 2, 0, 0, 2, 2}, {0, 8, 15}},
	    {{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 1, 1, 2}, {0, 13, 15}},
	    {{0, 0, 0, 2, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0, 1}, {0, 15, 3}},
	    {{0, 2, 2, 2, 1, 2, 2, 2, 0, 2, 2, 2, 1, 2, 2, 2}, {0, 12, 15}},
	    {{0, 1, 0, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2}, {0, 3, 15}},
	    {{0, 1, 1, 1, 2, 0, 1, 1, 2, 2, 0, 1, 2, 2, 2, 0}, {0, 3, 8}},
	}};
	struct Bc7Mode {
		std::uint32_t Subsets;
		std::uint32_t PartitionBits;
		std::uint32_t RotationBits;
		std::uint32_t SelectionBits;
		std::uint32_t ColorBits;
		std::uint32_t AlphaBits;
		std::uint32_t EndpointBits;
		std::uint32_t SharedBits;
		std::uint32_t IndexBits;
		std::uint32_t SecondaryIndexBits;
	};

	static constexpr std::array<Bc7Mode, 8> kBc7Modes = {{{3, 4, 0, 0, 4, 0, 1, 0, 3, 0},
	    {2, 6, 0, 0, 6, 0, 0, 1, 3, 0},
	    {3, 6, 0, 0, 5, 0, 0, 0, 2, 0},
	    {2, 6, 0, 0, 7, 0, 1, 0, 2, 0},
	    {1, 0, 2, 1, 5, 6, 0, 0, 2, 3},
	    {1, 0, 2, 0, 7, 8, 0, 0, 2, 2},
	    {1, 0, 0, 0, 7, 7, 1, 0, 4, 0},
	    {2, 6, 0, 0, 5, 5, 1, 0, 2, 0}}};

	class Bc7Bits {
	public:
		explicit Bc7Bits(std::span<const std::uint8_t, 16> bytes) : m_Bytes(bytes) {
		}

		[[nodiscard]] std::uint32_t Read(std::uint32_t count) {
			if (count > 8 || count > 128 - m_Position) {
				throw std::runtime_error("BC7 field exceeds its 128-bit block");
			}

			std::uint32_t value = 0;
			for (std::uint32_t bit = 0; bit < count; ++bit) {
				value |= static_cast<std::uint32_t>((m_Bytes[m_Position / 8] >> (m_Position % 8)) & 1) << bit;
				++m_Position;
			}

			return value;
		}

	private:
		std::span<const std::uint8_t, 16> m_Bytes;
		std::uint32_t m_Position = 0;
	};

	static std::uint32_t ExpandEndpoint(std::uint32_t value, std::uint32_t bits) {
		const auto shifted = value << (8 - bits);

		return shifted | (shifted >> bits);
	}

	static std::uint32_t InterpolationWeight(std::uint32_t index, std::uint32_t bits) {
		if (bits == 2) {
			return index * 21 + (index >> 1);
		}

		if (bits == 3) {
			return index * 9 + (index >> 2);
		}

		return (index << 2) + (index >> 2) + ((index >> 1) & 1);
	}

	static std::array<std::uint8_t, 16> ReadBc7Indices(Bc7Bits& bits, const Bc7Partition& partition, std::uint32_t index_bits) {
		std::array<std::uint8_t, 16> indices{};
		for (std::size_t pixel = 0; pixel < indices.size(); ++pixel) {
			const auto subset = partition.Subsets[pixel];
			const auto fixup = pixel == partition.Anchors[subset] ? 1u : 0u;
			indices[pixel] = static_cast<std::uint8_t>(bits.Read(index_bits - fixup));
		}

		return indices;
	}

	static std::array<std::uint8_t, 64> DecodeBc7Block(std::span<const std::uint8_t, 16> bytes) {
		std::array<std::uint8_t, 64> pixels{};
		if (bytes[0] == 0) {
			return pixels;
		}

		Bc7Bits bits(bytes);
		std::size_t mode_index = 0;
		while (bits.Read(1) == 0) {
			++mode_index;
		}

		const auto& mode = kBc7Modes[mode_index];
		const auto partition_index = bits.Read(mode.PartitionBits);
		const auto rotation = bits.Read(mode.RotationBits);
		const auto selection = bits.Read(mode.SelectionBits);
		Bc7Partition partition{};
		if (mode.Subsets == 2) {
			partition = kBc7Partitions2[partition_index];
		} else if (mode.Subsets == 3) {
			partition = kBc7Partitions3[partition_index];
		}

		std::array<std::array<std::uint32_t, 4>, 6> endpoints{};
		const auto endpoint_count = mode.Subsets * 2;
		for (std::size_t channel = 0; channel < 3; ++channel) {
			for (std::size_t endpoint = 0; endpoint < endpoint_count; ++endpoint) {
				endpoints[endpoint][channel] = bits.Read(mode.ColorBits);
			}
		}

		for (std::size_t endpoint = 0; endpoint < endpoint_count; ++endpoint) {
			endpoints[endpoint][3] = mode.AlphaBits == 0 ? 255u : bits.Read(mode.AlphaBits);
		}

		std::array<std::uint32_t, 6> p_bits{};
		if (mode.EndpointBits != 0) {
			for (std::size_t endpoint = 0; endpoint < endpoint_count; ++endpoint) {
				p_bits[endpoint] = bits.Read(1);
			}
		} else if (mode.SharedBits != 0) {
			for (std::size_t subset = 0; subset < mode.Subsets; ++subset) {
				p_bits[subset * 2] = bits.Read(1);
				p_bits[subset * 2 + 1] = p_bits[subset * 2];
			}
		}

		const auto p_bit_count = mode.EndpointBits | mode.SharedBits;
		for (std::size_t endpoint = 0; endpoint < endpoint_count; ++endpoint) {
			for (std::size_t channel = 0; channel < 4; ++channel) {
				const auto stored_bits = channel == 3 ? mode.AlphaBits : mode.ColorBits;
				if (stored_bits != 0) {
					const auto value = (endpoints[endpoint][channel] << p_bit_count) | p_bits[endpoint];
					endpoints[endpoint][channel] = ExpandEndpoint(value, stored_bits + p_bit_count);
				}
			}
		}

		const auto primary = ReadBc7Indices(bits, partition, mode.IndexBits);
		std::array<std::uint8_t, 16> secondary{};
		if (mode.SecondaryIndexBits != 0) {
			secondary = ReadBc7Indices(bits, partition, mode.SecondaryIndexBits);
		}

		for (std::size_t pixel = 0; pixel < 16; ++pixel) {
			const auto subset = partition.Subsets[pixel];
			for (std::size_t channel = 0; channel < 4; ++channel) {
				const bool use_secondary = mode.SecondaryIndexBits != 0 && (channel == 3 ? selection == 0 : selection != 0);
				const auto index = use_secondary ? secondary[pixel] : primary[pixel];
				const auto index_bits = use_secondary ? mode.SecondaryIndexBits : mode.IndexBits;
				const auto weight = InterpolationWeight(index, index_bits);
				const auto value = (64 - weight) * endpoints[subset * 2][channel] + weight * endpoints[subset * 2 + 1][channel];
				pixels[pixel * 4 + channel] = static_cast<std::uint8_t>((value + 32) >> 6);
			}

			if (rotation != 0) {
				std::swap(pixels[pixel * 4 + 3], pixels[pixel * 4 + rotation - 1]);
			}
		}

		return pixels;
	}

	static RgbaImage CreateRgbaImage(std::uint32_t width, std::uint32_t height, std::size_t max_decoded_bytes) {
		if (width == 0 || height == 0 || width > kMaxImageDimension || height > kMaxImageDimension || width > max_decoded_bytes / 4 / height) {
			throw std::runtime_error("decoded image exceeds its dimensions or allocation limit");
		}

		RgbaImage image;
		image.Width = width;
		image.Height = height;
		image.Pixels.resize(static_cast<std::size_t>(width) * height * 4);

		return image;
	}

	static void DecodeBc7Texture(RgbaImage& image, std::span<const std::uint8_t> bytes) {
		const auto blocks_wide = (image.Width + 3) / 4;
		const auto blocks_high = (image.Height + 3) / 4;
		for (std::uint32_t block_y = 0; block_y < blocks_high; ++block_y) {
			for (std::uint32_t block_x = 0; block_x < blocks_wide; ++block_x) {
				const auto block_offset = (static_cast<std::size_t>(block_y) * blocks_wide + block_x) * 16;
				const auto block = DecodeBc7Block(std::span<const std::uint8_t, 16>(bytes.subspan(block_offset, 16)));
				for (std::uint32_t y = 0; y < 4 && block_y * 4 + y < image.Height; ++y) {
					for (std::uint32_t x = 0; x < 4 && block_x * 4 + x < image.Width; ++x) {
						const auto source = (static_cast<std::size_t>(y) * 4 + x) * 4;
						const auto destination = (static_cast<std::size_t>(block_y * 4 + y) * image.Width + block_x * 4 + x) * 4;
						std::copy_n(block.begin() + static_cast<std::ptrdiff_t>(source), 4, image.Pixels.begin() + static_cast<std::ptrdiff_t>(destination));
					}
				}
			}
		}
	}

	Bc7TextureView BorrowBc7Texture(const ImageTexture& texture, std::span<const std::uint8_t> psb_bytes, std::size_t max_decoded_bytes) {
		if (texture.Format != PixelFormat::Bc7) {
			throw std::runtime_error("stored BC7 upload requires the actual BC7 texture format");
		}
		const auto byte_count = TextureByteCount(texture);
		if (texture.Width > max_decoded_bytes / 4 / texture.Height) {
			throw std::runtime_error("stored BC7 texture exceeds its decoded extent limit");
		}
		const auto& range = texture.PixelRange;
		if (range.Size != byte_count || range.Offset > psb_bytes.size() || range.Size > psb_bytes.size() - range.Offset) {
			throw std::runtime_error("stored BC7 pixel range exceeds input or disagrees with its dimensions");
		}

		Bc7TextureView view;
		view.Width = texture.Width;
		view.Height = texture.Height;
		view.BlockRowBytes = static_cast<std::size_t>((texture.Width + 3) / 4) * 16;
		view.Blocks = psb_bytes.subspan(static_cast<std::size_t>(range.Offset), static_cast<std::size_t>(range.Size));

		return view;
	}

	RgbaImage DecodeTexture(const ImageTexture& texture, std::span<const std::uint8_t> psb_bytes, std::size_t max_decoded_bytes) {
		const auto byte_count = TextureByteCount(texture);
		const auto& range = texture.PixelRange;
		if (range.Size != byte_count || range.Offset > psb_bytes.size() || range.Size > psb_bytes.size() - range.Offset) {
			throw std::runtime_error("texture pixel range exceeds input or disagrees with its format");
		}

		const auto bytes = psb_bytes.subspan(static_cast<std::size_t>(range.Offset), static_cast<std::size_t>(range.Size));
		auto image = CreateRgbaImage(texture.Width, texture.Height, max_decoded_bytes);
		if (texture.Format == PixelFormat::Bc7) {
			DecodeBc7Texture(image, bytes);
		} else {
			for (std::size_t pixel = 0; pixel < bytes.size(); ++pixel) {
				// Native DXGI A8 supplies only alpha; native L8 maps to R8_UNORM.
				image.Pixels[pixel * 4] = texture.Format == PixelFormat::Luminance8 ? bytes[pixel] : 0;
				image.Pixels[pixel * 4 + 3] = texture.Format == PixelFormat::Alpha8 ? bytes[pixel] : 255;
			}
		}

		return image;
	}

	RgbaImage DecodeImage(const ImageEntry& image, std::span<const std::uint8_t> psb_bytes, std::size_t max_decoded_bytes) {
		auto result = CreateRgbaImage(image.Width, image.Height, max_decoded_bytes);
		for (const auto& tile : image.Tiles) {
			if (tile.Left >= image.Width || tile.Top >= image.Height || tile.Width > tile.Texture.Width || tile.Height > tile.Texture.Height || tile.Width == 0 || tile.Height == 0) {
				throw std::runtime_error("image tile dimensions or origin are outside the texture or logical image");
			}

			if (result.Pixels.size() >= max_decoded_bytes) {
				throw std::runtime_error("image assembly has no remaining texture allocation budget");
			}

			const auto decoded = DecodeTexture(tile.Texture, psb_bytes, max_decoded_bytes - result.Pixels.size());
			const auto width = std::min(tile.Width, image.Width - tile.Left);
			const auto height = std::min(tile.Height, image.Height - tile.Top);
			for (std::uint32_t row = 0; row < height; ++row) {
				const auto source = static_cast<std::size_t>(row) * decoded.Width * 4;
				const auto destination = (static_cast<std::size_t>(tile.Top + row) * result.Width + tile.Left) * 4;
				std::copy_n(decoded.Pixels.begin() + static_cast<std::ptrdiff_t>(source), static_cast<std::size_t>(width) * 4, result.Pixels.begin() + static_cast<std::ptrdiff_t>(destination));
			}
		}

		return result;
	}

} // namespace sgre::resource
