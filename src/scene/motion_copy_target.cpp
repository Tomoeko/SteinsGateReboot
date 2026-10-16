#include "sgre/scene/motion_copy_target.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace sgre::scene {

	static std::int32_t QuantizeCopiedExtent(float extent) {
		const auto quantized = std::ceil(extent);
		if (static_cast<double>(quantized) > std::numeric_limits<std::int32_t>::max()) {
			throw std::runtime_error("motion copied-target extent exceeds signed dimensions");
		}

		return static_cast<std::int32_t>(quantized);
	}

	MotionCopiedTargetUpdate PrepareMotionCopiedTargetUpdate(MotionCopiedTargetSize current, const MotionBounds& requested) {
		if (current.Width < 0 || current.Height < 0) {
			throw std::runtime_error("motion copied-target current dimensions must be nonnegative");
		}
		if (!std::isfinite(requested.MinimumX) || !std::isfinite(requested.MinimumY) || !std::isfinite(requested.MaximumX) || !std::isfinite(requested.MaximumY)) {
			throw std::runtime_error("motion copied-target rectangle must be finite");
		}

		const float width = requested.MaximumX - requested.MinimumX;
		const float height = requested.MaximumY - requested.MinimumY;
		if (!std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0) {
			throw std::runtime_error("motion copied-target rectangle requires positive finite extents");
		}

		const float current_width = static_cast<float>(current.Width);
		const float current_height = static_cast<float>(current.Height);
		const bool smaller = current_width * 0.5f > width || current_height * 0.5f > height;
		const bool larger = width > current_width || height > current_height;
		MotionCopiedTargetUpdate update;
		update.Size = current;
		update.Replace = smaller || larger;
		if (update.Replace) {
			update.Size.Width = QuantizeCopiedExtent(width);
			update.Size.Height = QuantizeCopiedExtent(height);
		}

		return update;
	}

} // namespace sgre::scene
