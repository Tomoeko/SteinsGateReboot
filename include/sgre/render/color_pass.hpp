#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

namespace sgre::render {

	struct ColorPassVertex {
		std::array<float, 3> Position{};
		std::array<float, 4> Color{};
		std::array<float, 2> TextureCoordinates{};
	};

	struct ColorPassDescription {
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
		std::size_t MaxVertices = 4096;
		std::size_t MaxIndices = 8192;
		std::size_t MaxPixelBytes = 64 * 1024 * 1024;
	};

	struct ColorPassPixels {
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
		// Tight top-to-bottom RGBA8 UNORM rows. Capture owns its pixels.
		std::vector<std::uint8_t> Rgba;
	};

	class ColorPassRenderer {
	public:
		ColorPassRenderer(ColorPassDescription description, const std::filesystem::path& shader_source);
		~ColorPassRenderer();
		ColorPassRenderer(const ColorPassRenderer&) = delete;
		ColorPassRenderer& operator=(const ColorPassRenderer&) = delete;
		ColorPassRenderer(ColorPassRenderer&&) = delete;
		ColorPassRenderer& operator=(ColorPassRenderer&&) = delete;

		// Bounded asynchronous submission uses preallocated upload buffers and a three-frame queue.
		[[nodiscard]] bool CanSubmit() const;
		[[nodiscard]] std::uint64_t Submit(std::span<const ColorPassVertex> vertices, std::span<const std::uint16_t> strip_indices, const std::array<float, 16>& column_major_matrix, const std::array<float, 4>& clear_color = {});
		// Explicit verification operation: waits and reads back the latest submission, never an ordinary frame.
		[[nodiscard]] ColorPassPixels Capture(std::uint64_t submission);
		[[nodiscard]] ColorPassDescription Description() const;

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::render
