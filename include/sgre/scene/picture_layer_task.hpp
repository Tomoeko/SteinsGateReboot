#pragma once

#include "sgre/resource/motion.hpp"
#include "sgre/scene/image_assets.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace sgre::scene {

	struct PictureLayerTaskState {
		resource::MotionPoint Coordinate;
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
		std::int32_t Minification = 0;
		std::int32_t Magnification = 0;
		std::uint32_t Opacity = 255;
		std::uint32_t BlendMode = 0;
		float MeshSize = 16;
		bool Closed = false;
	};

	class PictureLayerTask {
	public:
		// Geometry borrows the live Image model; its script wrapper separately retains the actual Image root.
		PictureLayerTask(std::shared_ptr<const LoadedImageAsset> image, std::optional<std::array<float, 4>> initial_range = {}, std::size_t max_regions = 4096);
		~PictureLayerTask();
		PictureLayerTask(const PictureLayerTask&) = delete;
		PictureLayerTask& operator=(const PictureLayerTask&) = delete;
		[[nodiscard]] PictureLayerTaskState State() const;
		[[nodiscard]] std::span<const PictureRegion> Regions() const;
		void SetCoordinate(resource::MotionPoint coordinate);
		void SetLeft(float left);
		void SetTop(float top);
		void SetOpacity(std::uint32_t opacity);
		void SetBlendMode(std::uint32_t blend_mode);
		void SetMeshSize(float mesh_size);
		void SetFiltering(std::int32_t minification, std::int32_t magnification);
		void AppendImageRange(const std::array<float, 4>& rectangle, const std::array<float, 2>& destination = {});
		void ClearImageRanges();
		[[nodiscard]] bool Contains(const std::array<float, 2>& point) const;
		// Native deletion owns only prepared regions/modulator storage; it never dereferences the borrowed Image.
		void Close() noexcept;

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::scene
