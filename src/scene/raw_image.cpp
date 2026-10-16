#include "sgre/scene/raw_image.hpp"

#include "sgre/scene/raw_texture_restore.hpp"

#include <algorithm>
#include <array>
#include <bit>
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

	void RawImage::FillAlpha() {
		for (std::size_t offset = 3; offset < m_Pixels.size(); offset += 4) {
			m_Pixels[offset] = 255;
		}
		m_Updated = true;
	}

	void RawImage::BoxBlur(std::uint32_t horizontal_radius, std::uint32_t vertical_radius, std::uint32_t iterations) {
		static constexpr std::size_t kMaximumBlurPixelVisits = 256 * 1024 * 1024;
		if (horizontal_radius > m_Width / 2 || vertical_radius > m_Height / 2 || iterations == 0 || iterations > kMaximumBlurPixelVisits / (m_Pixels.size() / 4)) {
			throw std::runtime_error("raw image blur requires in-bounds radii and a bounded positive iteration count");
		}
		std::vector<std::uint8_t> source(m_Pixels.size());
		std::vector<std::array<std::uint32_t, 4>> columns(m_Width);
		for (std::uint32_t iteration = 0; iteration < iterations; ++iteration) {
			std::copy(m_Pixels.begin(), m_Pixels.end(), source.begin());
			std::fill(columns.begin(), columns.end(), std::array<std::uint32_t, 4>{});
			const auto add_row = [&](std::uint32_t row, bool subtract) {
				for (std::size_t column = 0; column < m_Width; ++column) {
					const auto offset = (static_cast<std::size_t>(row) * m_Width + column) * 4;
					for (std::size_t channel = 0; channel < 4; ++channel) {
						if (subtract) {
							columns[column][channel] -= source[offset + channel];
						} else {
							columns[column][channel] += source[offset + channel];
						}
					}
				}
			};
			for (std::uint32_t row = 0; row <= vertical_radius; ++row) {
				add_row(row, false);
			}
			for (std::uint32_t row = 0; row < m_Height; ++row) {
				std::array<std::uint32_t, 4> sums{};
				for (std::uint32_t column = 0; column <= horizontal_radius; ++column) {
					for (std::size_t channel = 0; channel < 4; ++channel) {
						sums[channel] += columns[column][channel];
					}
				}
				const auto first_row = row > vertical_radius ? row - vertical_radius : 0;
				const auto last_row = std::min(m_Height, row + vertical_radius + 1);
				for (std::uint32_t column = 0; column < m_Width; ++column) {
					const auto first_column = column > horizontal_radius ? column - horizontal_radius : 0;
					const auto last_column = std::min(m_Width, column + horizontal_radius + 1);
					const auto count = static_cast<std::uint64_t>(last_column - first_column) * (last_row - first_row);
					const auto reciprocal = (std::uint64_t{1} << 32) / count;
					const auto offset = (static_cast<std::size_t>(row) * m_Width + column) * 4;
					for (std::size_t channel = 0; channel < 4; ++channel) {
						m_Pixels[offset + channel] = static_cast<std::uint8_t>((static_cast<std::uint64_t>(sums[channel]) * reciprocal) >> 32);
						if (column >= horizontal_radius) {
							sums[channel] -= columns[column - horizontal_radius][channel];
						}
						if (column + horizontal_radius + 1 < m_Width) {
							sums[channel] += columns[column + horizontal_radius + 1][channel];
						}
					}
				}
				if (row >= vertical_radius) {
					add_row(row - vertical_radius, true);
				}
				if (row + vertical_radius + 1 < m_Height) {
					add_row(row + vertical_radius + 1, false);
				}
			}
		}
		m_Updated = true;
	}

	void RawImage::AdjustLevel(const std::array<std::int32_t, 6>& endpoints) {
		std::array<std::array<std::uint8_t, 256>, 3> tables{};
		for (std::size_t channel = 0; channel < 3; ++channel) {
			const auto first = std::bit_cast<std::uint32_t>(endpoints[channel * 2]);
			const auto delta = std::bit_cast<std::uint32_t>(endpoints[channel * 2 + 1]) - first;
			std::uint32_t accumulator = 0;
			for (auto& entry : tables[channel]) {
				// Native236250 uses unsigned MUL/division, then signed clipping after the wrapping add.
				const auto value = std::bit_cast<std::int32_t>(accumulator / 255u + first);
				entry = static_cast<std::uint8_t>(std::clamp(value, 0, 255));
				accumulator += delta;
			}
		}
		for (std::size_t offset = 0; offset < m_Pixels.size(); offset += 4) {
			m_Pixels[offset] = tables[2][m_Pixels[offset]];
			m_Pixels[offset + 1] = tables[1][m_Pixels[offset + 1]];
			m_Pixels[offset + 2] = tables[0][m_Pixels[offset + 2]];
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
