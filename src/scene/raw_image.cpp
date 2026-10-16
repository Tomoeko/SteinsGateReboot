#include "sgre/scene/raw_image.hpp"

#include "sgre/scene/raw_texture_restore.hpp"

#include <cstddef>
#include <stdexcept>

namespace sgre::scene {

	static constexpr std::size_t kMaxRawImageBytes = 64 * 1024 * 1024;

	static std::size_t RawImageByteCount(std::uint32_t width, std::uint32_t height) {
		if (width == 0 || height == 0 || width > kMaxRawImageBytes / 4 / height) {
			throw std::runtime_error("raw image dimensions require a positive bounded pixel count");
		}

		return static_cast<std::size_t>(width) * height * 4;
	}

	RawImage::RawImage(std::uint32_t width, std::uint32_t height) : m_Width(width), m_Height(height), m_Pixels(RawImageByteCount(width, height)) {
	}

	std::uint32_t RawImage::Width() const noexcept {
		return m_Width;
	}

	std::uint32_t RawImage::Height() const noexcept {
		return m_Height;
	}

	bool RawImage::IsUpdated() const noexcept {
		return m_Updated;
	}

	void RawImage::ClearUpdated() noexcept {
		m_Updated = false;
	}

	std::span<const std::uint8_t> RawImage::BgraPixels() const noexcept {
		return m_Pixels;
	}

	void RawImage::Fill(RawImageRegion region, std::uint32_t rrggbbaa, bool opaque_only) {
		if (region.Width == 0 || region.Height == 0 || region.X >= m_Width || region.Y >= m_Height || region.Width > m_Width - region.X || region.Height > m_Height - region.Y) {
			throw std::runtime_error("raw image fill requires a positive rectangle within its dimensions");
		}

		for (std::size_t row = region.Y; row < static_cast<std::size_t>(region.Y) + region.Height; ++row) {
			for (std::size_t column = region.X; column < static_cast<std::size_t>(region.X) + region.Width; ++column) {
				const auto offset = (row * m_Width + column) * 4;
				if (opaque_only && m_Pixels[offset + 3] == 0) {
					continue;
				}

				m_Pixels[offset] = static_cast<std::uint8_t>(rrggbbaa >> 8);
				m_Pixels[offset + 1] = static_cast<std::uint8_t>(rrggbbaa >> 16);
				m_Pixels[offset + 2] = static_cast<std::uint8_t>(rrggbbaa >> 24);
				if (!opaque_only) {
					m_Pixels[offset + 3] = static_cast<std::uint8_t>(rrggbbaa);
				}
			}
		}

		m_Updated = true;
	}

	void RawImage::RestorePixels(std::span<const std::uint8_t> rgb_or_rgba) {
		const auto rgb_size = m_Pixels.size() / 4 * 3;
		if (rgb_or_rgba.size() != rgb_size && rgb_or_rgba.size() != m_Pixels.size()) {
			throw std::runtime_error("raw image restore requires an exact RGB or RGBA byte count");
		}

		// The original native method snapshots its Binary view before modifying image storage.
		const std::vector<std::uint8_t> input(rgb_or_rgba.begin(), rgb_or_rgba.end());
		if (input.size() == rgb_size) {
			RestoreRawTextureRgb({m_Pixels, m_Width, m_Height, static_cast<std::size_t>(m_Width) * 4}, input);
		} else {
			for (std::size_t offset = 0; offset < input.size(); offset += 4) {
				m_Pixels[offset] = input[offset + 2];
				m_Pixels[offset + 1] = input[offset + 1];
				m_Pixels[offset + 2] = input[offset];
				m_Pixels[offset + 3] = input[offset + 3];
			}
		}

		m_Updated = true;
	}

	std::vector<std::uint8_t> RawImage::StorePixels(bool include_alpha) const {
		const auto channels = include_alpha ? std::size_t(4) : std::size_t(3);
		std::vector<std::uint8_t> output(m_Pixels.size() / 4 * channels);
		for (std::size_t pixel = 0; pixel < m_Pixels.size() / 4; ++pixel) {
			const auto source = pixel * 4;
			const auto destination = pixel * channels;
			output[destination] = m_Pixels[source + 2];
			output[destination + 1] = m_Pixels[source + 1];
			output[destination + 2] = m_Pixels[source];
			if (include_alpha) {
				output[destination + 3] = m_Pixels[source + 3];
			}
		}

		return output;
	}

} // namespace sgre::scene
