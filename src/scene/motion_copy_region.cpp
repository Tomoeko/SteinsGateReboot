#include "sgre/scene/motion_copy_region.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace sgre::scene {

	static bool OrderedBounds(const MotionBounds& bounds) {
		return bounds.MaximumX >= bounds.MinimumX && bounds.MaximumY >= bounds.MinimumY;
	}

	static void RequireFiniteCopyBounds(const MotionBounds& bounds) {
		if (!std::isfinite(bounds.MinimumX) || !std::isfinite(bounds.MinimumY) || !std::isfinite(bounds.MaximumX) || !std::isfinite(bounds.MaximumY)) {
			throw std::runtime_error("motion copied-region bounds must be finite");
		}
	}

	static void IntersectCopyBounds(MotionBounds& bounds, const MotionBounds& clip) {
		if (!(clip.MaximumY > bounds.MaximumY)) {
			bounds.MaximumY = clip.MaximumY;
		}
		if (!(clip.MaximumX > bounds.MaximumX)) {
			bounds.MaximumX = clip.MaximumX;
		}
		if (!(bounds.MinimumY > clip.MinimumY)) {
			bounds.MinimumY = clip.MinimumY;
		}
		if (!(bounds.MinimumX > clip.MinimumX)) {
			bounds.MinimumX = clip.MinimumX;
		}
	}

	static void SnapPixelationBounds(MotionBounds& bounds, float width, float height, float amount) {
		if (!std::isfinite(amount) || amount == 0) {
			throw std::runtime_error("motion copy pixelation amount must be nonzero and finite");
		}

		float cell = -amount;
		if (amount > 0) {
			cell = std::ceil(std::max(height, width) / amount);
		}
		if (!std::isfinite(cell) || cell <= 0) {
			throw std::runtime_error("motion copy pixelation cell must be finite and positive");
		}
		bounds.MinimumX = std::floor(bounds.MinimumX / cell) * cell;
		bounds.MinimumY = std::floor(bounds.MinimumY / cell) * cell;
		bounds.MaximumX = std::ceil(bounds.MaximumX / cell) * cell;
		bounds.MaximumY = std::ceil(bounds.MaximumY / cell) * cell;
	}

	static void ExpandBlurBounds(MotionBounds& bounds, float parameter) {
		if (!std::isfinite(parameter) || parameter < 0 || parameter >= 10) {
			throw std::runtime_error("motion copy blur step must be within the finite [0,10) domain");
		}

		const auto step = static_cast<std::uint32_t>(parameter);
		static constexpr std::array<float, 5> offsets{1, 2, 2, 4, 6};
		const auto negative_offset = -offsets[step % offsets.size()];
		if (step >= 5) {
			bounds.MinimumX += 0.0f;
			bounds.MinimumY += negative_offset;
			bounds.MaximumX += 0.0f;
			bounds.MaximumY -= negative_offset;
		} else {
			bounds.MinimumX += negative_offset;
			bounds.MinimumY += 0.0f;
			bounds.MaximumX -= negative_offset;
			bounds.MaximumY += 0.0f;
		}
	}

	MotionCopyRegionPlan PrepareMotionCopyRegion(const MotionCopyRegionInput& input) {
		MotionCopyRegionPlan plan;
		const auto mode = input.BlendMode & 0xff0f;
		if ((mode & 0xf00) != 0x100) {
			return plan;
		}
		if (input.Target.Width <= 0 || input.Target.Height <= 0) {
			throw std::runtime_error("motion copy target dimensions must be positive");
		}
		RequireFiniteCopyBounds(input.Accumulated);
		RequireFiniteCopyBounds(input.Clip);

		plan.Action = MotionCopyRegionAction::Preserve;
		const float half_width = static_cast<float>(input.Target.Width) * 0.5f;
		const float half_height = static_cast<float>(input.Target.Height) * 0.5f;
		const float width = (0.0f - -half_width) + half_width;
		const float height = (0.0f - -half_height) + half_height;
		MotionBounds bounds{0, 0, width, height};
		if (OrderedBounds(input.Accumulated)) {
			bounds = TransformMotionScissorBounds(input.Accumulated, input.View, input.Model, input.Target);
			if (OrderedBounds(input.Clip)) {
				IntersectCopyBounds(bounds, input.Clip);
			}
		}
		if (mode == 0x10c) {
			SnapPixelationBounds(bounds, width, height, input.BlendParameter);
		} else if (mode == 0x10d) {
			ExpandBlurBounds(bounds, input.BlendParameter);
		}
		RequireFiniteCopyBounds(bounds);
		bounds.MinimumX = std::floor(bounds.MinimumX);
		bounds.MinimumY = std::floor(bounds.MinimumY);
		bounds.MaximumX = std::ceil(bounds.MaximumX);
		bounds.MaximumY = std::ceil(bounds.MaximumY);
		if (!(height > bounds.MaximumY)) {
			bounds.MaximumY = height;
		}
		if (!(width > bounds.MaximumX)) {
			bounds.MaximumX = width;
		}
		if (!(bounds.MinimumY > 0)) {
			bounds.MinimumY = 0;
		}
		if (!(bounds.MinimumX > 0)) {
			bounds.MinimumX = 0;
		}
		if (bounds.MaximumX - bounds.MinimumX <= 0 || bounds.MaximumY - bounds.MinimumY <= 0) {
			return plan;
		}

		plan.Action = MotionCopyRegionAction::Copy;
		plan.Rectangle = bounds;

		return plan;
	}

} // namespace sgre::scene
