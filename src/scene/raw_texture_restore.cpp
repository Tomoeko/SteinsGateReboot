#include "sgre/scene/raw_texture_restore.hpp"

#include <cstring>
#include <functional>
#include <stdexcept>

namespace sgre::scene {

	static constexpr std::size_t kMaxRawTextureBytes = 64 * 1024 * 1024;

	static void ValidateRestore(const RawTextureSurface& destination, std::span<const std::uint8_t> source, std::size_t channels) {
		if (destination.Width == 0 || destination.Height == 0 || destination.Width > kMaxRawTextureBytes / 4 / destination.Height) {
			throw std::runtime_error("raw texture restore dimensions exceed the positive bounded pixel domain");
		}

		const auto row_bytes = static_cast<std::size_t>(destination.Width) * 4;
		if (destination.RowBytes < row_bytes || destination.RowBytes > kMaxRawTextureBytes / destination.Height || destination.Bytes.size() < destination.RowBytes * destination.Height) {
			throw std::runtime_error("raw texture restore destination row pitch or storage is invalid");
		}

		const auto required = static_cast<std::size_t>(destination.Width) * destination.Height * channels;
		if (source.size() != required) {
			throw std::runtime_error("raw texture restore requires an exact packed input byte count");
		}

		// std::less supplies a total pointer order without subtracting unrelated allocations.
		const std::less<const void*> less;
		if (less(source.data(), destination.Bytes.data() + destination.Bytes.size()) && less(destination.Bytes.data(), source.data() + source.size())) {
			throw std::runtime_error("raw texture restore source must not overlap mapped destination storage");
		}
	}

	void RestoreRawTextureBgra(const RawTextureSurface& destination, std::span<const std::uint8_t> bgra, std::uint32_t width, std::uint32_t height) {
		if (width != destination.Width || height != destination.Height) {
			throw std::runtime_error("raw texture restore image size mismatch");
		}

		ValidateRestore(destination, bgra, 4);
		const auto packed_row = static_cast<std::size_t>(width) * 4;
		for (std::size_t row = 0; row < height; ++row) {
			std::memcpy(destination.Bytes.data() + row * destination.RowBytes, bgra.data() + row * packed_row, packed_row);
		}
	}

	void RestoreRawTextureRgb(const RawTextureSurface& destination, std::span<const std::uint8_t> rgb) {
		ValidateRestore(destination, rgb, 3);
		for (std::size_t row = 0; row < destination.Height; ++row) {
			const auto source_row = row * destination.Width * 3;
			const auto destination_row = row * destination.RowBytes;
			for (std::size_t column = 0; column < destination.Width; ++column) {
				const auto source_offset = source_row + column * 3;
				const auto destination_offset = destination_row + column * 4;
				destination.Bytes[destination_offset] = std::byte(rgb[source_offset + 2]);
				destination.Bytes[destination_offset + 1] = std::byte(rgb[source_offset + 1]);
				destination.Bytes[destination_offset + 2] = std::byte(rgb[source_offset]);
				destination.Bytes[destination_offset + 3] = std::byte{255};
			}
		}
	}

} // namespace sgre::scene
