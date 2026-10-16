#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace sgre::scene {

	struct RawImageRegion {
		std::uint32_t X = 0;
		std::uint32_t Y = 0;
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
	};

	class RawImage {
	public:
		RawImage(std::uint32_t width, std::uint32_t height);
		RawImage(const RawImage&) = delete;
		RawImage& operator=(const RawImage&) = delete;
		RawImage(RawImage&&) = delete;
		RawImage& operator=(RawImage&&) = delete;

		[[nodiscard]] std::uint32_t Width() const noexcept;
		[[nodiscard]] std::uint32_t Height() const noexcept;
		[[nodiscard]] bool IsUpdated() const noexcept;
		void ClearUpdated() noexcept;
		// Borrowed packed BGRA rows remain valid while this exclusive mutable image exists.
		[[nodiscard]] std::span<const std::uint8_t> BgraPixels() const noexcept;
		// Packed color is RRGGBBAA. Opaque-only fill replaces RGB where existing alpha is nonzero.
		void Fill(RawImageRegion region, std::uint32_t rrggbbaa, bool opaque_only = false);
		// Native restore accepts exactly packed RGB or RGBA bytes and copies input before conversion.
		void RestorePixels(std::span<const std::uint8_t> rgb_or_rgba);
		[[nodiscard]] std::vector<std::uint8_t> StorePixels(bool include_alpha) const;

	private:
		std::uint32_t m_Width;
		std::uint32_t m_Height;
		std::vector<std::uint8_t> m_Pixels;
		bool m_Updated = true;
	};

} // namespace sgre::scene
