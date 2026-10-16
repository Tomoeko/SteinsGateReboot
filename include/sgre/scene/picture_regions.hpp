#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace sgre::scene {

	struct PictureTileBounds {
		std::array<float, 4> Bounds{};
		std::uint32_t Source = 0;
	};

	struct PictureRegion {
		std::array<float, 4> Destination{};
		std::array<float, 4> SourceBounds{};
		std::uint32_t Source = 0;
	};

	class PictureRegionSet {
	public:
		// Tile bounds borrow the immutable Image model through preparation; previously prepared regions are owned.
		explicit PictureRegionSet(std::span<const PictureTileBounds> tiles, std::size_t max_regions = 4096);
		// Preparation appends cropped pieces, preserving earlier ranges and their cumulative extent.
		void Append(const std::array<float, 4>& rectangle, const std::array<float, 2>& destination = {});
		void Clear() noexcept;
		[[nodiscard]] std::uint32_t Width() const noexcept;
		[[nodiscard]] std::uint32_t Height() const noexcept;
		[[nodiscard]] std::span<const PictureRegion> Regions() const noexcept;
		[[nodiscard]] bool Contains(const std::array<float, 2>& point, const std::array<float, 2>& position = {}) const;

	private:
		std::span<const PictureTileBounds> m_Tiles;
		std::vector<PictureRegion> m_Regions;
		std::size_t m_MaxRegions = 0;
		std::uint32_t m_Width = 0;
		std::uint32_t m_Height = 0;
	};

} // namespace sgre::scene
