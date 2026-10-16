#include "sgre/scene/picture_layer_task.hpp"

#include <stdexcept>
#include <utility>

namespace sgre::scene {

	static const LoadedImageAsset& RequirePictureModel(const std::shared_ptr<const LoadedImageAsset>& image) {
		if (!image) {
			throw std::runtime_error("Picture task requires its genuine prepared Image model");
		}

		return *image;
	}

	class PictureLayerTask::Implementation {
	public:
		Implementation(const std::shared_ptr<const LoadedImageAsset>& image, const std::optional<std::array<float, 4>>& initial_range, std::size_t maximum) : Image(image), Ranges(RequirePictureModel(image).TileBounds, maximum) {
			const auto range = initial_range.value_or(std::array<float, 4>{0, 0, static_cast<float>(image->Model.Width), static_cast<float>(image->Model.Height)});
			Ranges.Append(range);
		}

		std::weak_ptr<const LoadedImageAsset> Image;
		PictureRegionSet Ranges;
		PictureLayerTaskState Value;

		void RequireOpen() const {
			if (Value.Closed) {
				throw std::runtime_error("Picture native task has retired its prepared geometry");
			}
		}
	};

	PictureLayerTask::PictureLayerTask(std::shared_ptr<const LoadedImageAsset> image, std::optional<std::array<float, 4>> initial_range, std::size_t max_regions) : m_Implementation(std::make_unique<Implementation>(image, initial_range, max_regions)) {
	}

	PictureLayerTask::~PictureLayerTask() = default;

	PictureLayerTaskState PictureLayerTask::State() const {
		auto value = m_Implementation->Value;
		value.Width = m_Implementation->Ranges.Width();
		value.Height = m_Implementation->Ranges.Height();

		return value;
	}

	std::span<const PictureRegion> PictureLayerTask::Regions() const {
		m_Implementation->RequireOpen();

		return m_Implementation->Ranges.Regions();
	}

	void PictureLayerTask::SetCoordinate(resource::MotionPoint coordinate) {
		m_Implementation->RequireOpen();
		m_Implementation->Value.Coordinate = coordinate;
	}

	void PictureLayerTask::SetLeft(float left) {
		SetCoordinate({left, m_Implementation->Value.Coordinate.Y});
	}

	void PictureLayerTask::SetTop(float top) {
		SetCoordinate({m_Implementation->Value.Coordinate.X, top});
	}

	void PictureLayerTask::SetOpacity(std::uint32_t opacity) {
		m_Implementation->RequireOpen();
		m_Implementation->Value.Opacity = opacity;
	}

	void PictureLayerTask::SetBlendMode(std::uint32_t blend_mode) {
		m_Implementation->RequireOpen();
		m_Implementation->Value.BlendMode = blend_mode;
	}

	void PictureLayerTask::SetMeshSize(float mesh_size) {
		m_Implementation->RequireOpen();
		m_Implementation->Value.MeshSize = mesh_size;
	}

	void PictureLayerTask::SetFiltering(std::int32_t minification, std::int32_t magnification) {
		m_Implementation->RequireOpen();
		m_Implementation->Value.Minification = minification;
		m_Implementation->Value.Magnification = magnification;
	}

	void PictureLayerTask::AppendImageRange(const std::array<float, 4>& rectangle, const std::array<float, 2>& destination) {
		m_Implementation->RequireOpen();
		const auto image = m_Implementation->Image.lock();
		if (!image) {
			throw std::runtime_error("Picture source model expired before image range preparation");
		}

		// This scoped owner keeps the borrowed tile bounds live for the complete Append operation.
		m_Implementation->Ranges.Append(rectangle, destination);
	}

	void PictureLayerTask::ClearImageRanges() {
		m_Implementation->RequireOpen();
		m_Implementation->Ranges.Clear();
	}

	bool PictureLayerTask::Contains(const std::array<float, 2>& point) const {
		m_Implementation->RequireOpen();
		const auto coordinate = m_Implementation->Value.Coordinate;

		return m_Implementation->Ranges.Contains(point, {coordinate.X, coordinate.Y});
	}

	void PictureLayerTask::Close() noexcept {
		m_Implementation->Ranges.Clear();
		m_Implementation->Image.reset();
		m_Implementation->Value.Closed = true;
	}

} // namespace sgre::scene
