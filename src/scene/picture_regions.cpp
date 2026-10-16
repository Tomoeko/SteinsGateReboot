#include "sgre/scene/picture_regions.hpp"

#include <bit>
#include <cmath>
#include <stdexcept>

namespace sgre::scene {

	static float NativePictureMin(float first, float second) {
		return first < second ? first : second;
	}

	static float NativePictureMax(float first, float second) {
		return first > second ? first : second;
	}

	static void RequireFinitePicture(std::span<const float> values) {
		for (const auto value : values) {
			if (!std::isfinite(value)) {
				throw std::runtime_error("picture region geometry requires finite measured coordinates");
			}
		}
	}

	static std::array<float, 4> PictureIntersection(const std::array<float, 4>& rectangle, const PictureTileBounds& tile) {
		return {NativePictureMax(rectangle[0], tile.Bounds[0]), NativePictureMax(rectangle[1], tile.Bounds[1]), NativePictureMin(rectangle[2], tile.Bounds[2]), NativePictureMin(rectangle[3], tile.Bounds[3])};
	}

	static bool HasPictureArea(const std::array<float, 4>& intersection) {
		return intersection[2] - intersection[0] > 0 && intersection[3] - intersection[1] > 0;
	}

	static std::uint32_t PictureExtent(float first, float last, float destination, std::uint32_t previous) {
		const auto extent = NativePictureMax((last - first) + destination, static_cast<float>(previous));
		if (!std::isfinite(extent) || extent < 0 || extent >= 4294967296.0f) {
			throw std::runtime_error("picture logical extent exceeds its measured unsigned dimension domain");
		}

		return static_cast<std::uint32_t>(extent);
	}

	static float NegatePictureCoordinate(float value) {
		return std::bit_cast<float>(std::bit_cast<std::uint32_t>(value) ^ 0x80000000u);
	}

	static PictureRegion PreparePictureRegion(const std::array<float, 4>& rectangle, const std::array<float, 2>& destination, const PictureTileBounds& tile, const std::array<float, 4>& intersection) {
		PictureRegion region;
		const auto offset_x = destination[0] - rectangle[0];
		const auto offset_y = destination[1] - rectangle[1];
		region.Destination = {offset_x + intersection[0], offset_y + intersection[1], offset_x + intersection[2], offset_y + intersection[3]};
		const auto source_x = NegatePictureCoordinate(tile.Bounds[0]);
		const auto source_y = NegatePictureCoordinate(tile.Bounds[1]);
		region.SourceBounds = {source_x + intersection[0], source_y + intersection[1], source_x + intersection[2], source_y + intersection[3]};
		region.Source = tile.Source;
		RequireFinitePicture(region.Destination);
		RequireFinitePicture(region.SourceBounds);

		return region;
	}

	PictureRegionSet::PictureRegionSet(std::span<const PictureTileBounds> tiles, std::size_t max_regions) : m_Tiles(tiles), m_MaxRegions(max_regions) {
		if (tiles.size() > 65536 || max_regions == 0 || max_regions > 65536) {
			throw std::runtime_error("picture source or region count exceeds its bounded preparation capacity");
		}

		for (const auto& tile : tiles) {
			RequireFinitePicture(tile.Bounds);
		}
		m_Regions.reserve(max_regions);
	}

	void PictureRegionSet::Append(const std::array<float, 4>& rectangle, const std::array<float, 2>& destination) {
		RequireFinitePicture(rectangle);
		RequireFinitePicture(destination);

		const auto width = PictureExtent(rectangle[0], rectangle[2], destination[0], m_Width);
		const auto height = PictureExtent(rectangle[1], rectangle[3], destination[1], m_Height);
		std::size_t added = 0;
		for (const auto& tile : m_Tiles) {
			const auto intersection = PictureIntersection(rectangle, tile);
			if (HasPictureArea(intersection)) {
				static_cast<void>(PreparePictureRegion(rectangle, destination, tile, intersection));
				++added;
			}
		}
		if (added > m_MaxRegions - m_Regions.size()) {
			throw std::runtime_error("picture image range exceeds its prepared region capacity");
		}

		for (const auto& tile : m_Tiles) {
			const auto intersection = PictureIntersection(rectangle, tile);
			if (!HasPictureArea(intersection)) {
				continue;
			}

			m_Regions.push_back(PreparePictureRegion(rectangle, destination, tile, intersection));
		}

		m_Width = width;
		m_Height = height;
	}

	void PictureRegionSet::Clear() noexcept {
		m_Regions.clear();
		m_Width = 0;
		m_Height = 0;
	}

	std::uint32_t PictureRegionSet::Width() const noexcept {
		return m_Width;
	}

	std::uint32_t PictureRegionSet::Height() const noexcept {
		return m_Height;
	}

	std::span<const PictureRegion> PictureRegionSet::Regions() const noexcept {
		return m_Regions;
	}

	bool PictureRegionSet::Contains(const std::array<float, 2>& point, const std::array<float, 2>& position) const {
		RequireFinitePicture(point);
		RequireFinitePicture(position);

		for (const auto& region : m_Regions) {
			if (point[0] >= position[0] + region.Destination[0] && point[0] < position[0] + region.Destination[2] && point[1] >= position[1] + region.Destination[1] && point[1] < position[1] + region.Destination[3]) {
				return true;
			}
		}

		return false;
	}

} // namespace sgre::scene
