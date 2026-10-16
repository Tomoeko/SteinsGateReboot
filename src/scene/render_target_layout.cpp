#include "sgre/scene/render_target_layout.hpp"

#include <bit>
#include <stdexcept>

namespace sgre::scene {

	static constexpr std::uint32_t kMaximumRenderTargetDimension = 16384;

	RenderTargetLayout PrepareRenderTargetLayout(std::uint32_t width, std::uint32_t height, std::uint32_t native_target_type, std::uint32_t native_eye) {
		if (width == 0 || height == 0 || width > kMaximumRenderTargetDimension || height > kMaximumRenderTargetDimension) {
			throw std::runtime_error("render-target layout requires actual bounded nonempty allocated dimensions");
		}

		const auto physical_width = static_cast<float>(width);
		const auto physical_height = static_cast<float>(height);
		float scale_x = 1;
		float scale_y = 1;
		float offset_x = 0;
		float offset_y = 0;
		if (native_target_type == 2) {
			scale_x = 0.5f;
			if (native_eye == 1) {
				offset_x = physical_width * 0.5f;
			}
		} else if (native_target_type == 3) {
			scale_y = 0.5f;
			if (native_eye == 1) {
				offset_y = physical_height * 0.5f;
			}
		}

		RenderTargetLayout layout;
		layout.Viewport.X = scale_x * 0.0f + offset_x;
		layout.Viewport.Y = scale_y * 0.0f + offset_y;
		layout.ViewportBounds.MinimumX = layout.Viewport.X;
		layout.ViewportBounds.MinimumY = layout.Viewport.Y;
		layout.ViewportBounds.MaximumX = physical_width * scale_x + offset_x;
		layout.ViewportBounds.MaximumY = physical_height * scale_y + offset_y;
		layout.Viewport.Width = layout.ViewportBounds.MaximumX - layout.Viewport.X;
		layout.Viewport.Height = layout.ViewportBounds.MaximumY - layout.Viewport.Y;
		layout.FullScissor = MotionBounds{0, 0, physical_width, physical_height};

		// Native4c5510 uses the complete physical extent even for a split viewport.
		const auto right = physical_width * 0.5f;
		const auto left = -right;
		const auto bottom = physical_height * -0.5f;
		const auto top = -bottom;
		const auto horizontal = right - left;
		const auto vertical = top - bottom;
		layout.Projection.Coefficients[0] = 2.0f / horizontal;
		layout.Projection.Coefficients[3] = -(left + right) / horizontal;
		layout.Projection.Coefficients[5] = 2.0f / vertical;
		layout.Projection.Coefficients[7] = -(top + bottom) / vertical;
		layout.Projection.Coefficients[10] = -1.0f / 10.0f;
		layout.Projection.Coefficients[11] = -10.0f / 10.0f;

		return layout;
	}

	std::array<float, 4> PrepareRenderTargetClearColor(const std::array<std::int32_t, 4>& actual_words) {
		constexpr float scale = std::bit_cast<float>(std::uint32_t{0x3b808081});
		std::array<float, 4> colors{};
		for (std::size_t index = 0; index < colors.size(); ++index) {
			colors[index] = static_cast<float>(std::bit_cast<std::uint32_t>(actual_words[index])) * scale;
		}

		return colors;
	}

} // namespace sgre::scene
