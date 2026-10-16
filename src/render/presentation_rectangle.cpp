#include "sgre/render/presentation_rectangle.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace sgre::render {

	PresentationRectangle FitPresentationRectangle(std::uint32_t source_width, std::uint32_t source_height, std::uint32_t destination_width, std::uint32_t destination_height) {
		if (source_width == 0 || source_height == 0 || destination_width == 0 || destination_height == 0) {
			throw std::runtime_error("presentation rectangle requires positive source and destination pixel extents");
		}

		const auto scale = std::min(static_cast<double>(destination_width) / source_width, static_cast<double>(destination_height) / source_height);
		PresentationRectangle result;
		result.Width = source_width * scale;
		result.Height = source_height * scale;
		result.Left = (destination_width - result.Width) * 0.5;
		result.Top = (destination_height - result.Height) * 0.5;

		return result;
	}

	PresentationInputPoint MapPresentationPoint(PresentationRectangle rectangle, std::uint32_t source_width, std::uint32_t source_height, double x, double y) {
		if (source_width == 0 || source_height == 0 || !std::isfinite(rectangle.Left) || !std::isfinite(rectangle.Top) || !std::isfinite(rectangle.Width) || !std::isfinite(rectangle.Height) || rectangle.Width <= 0 || rectangle.Height <= 0 || !std::isfinite(x) || !std::isfinite(y)) {
			throw std::runtime_error("presentation input projection requires finite positive geometry");
		}

		PresentationInputPoint result;
		result.X = (x - rectangle.Left) / rectangle.Width * source_width;
		result.Y = (y - rectangle.Top) / rectangle.Height * source_height;
		if (!std::isfinite(result.X) || !std::isfinite(result.Y)) {
			throw std::runtime_error("presentation input projection exceeds finite logical coordinates");
		}
		result.Inside = x >= rectangle.Left && y >= rectangle.Top && x < rectangle.Left + rectangle.Width && y < rectangle.Top + rectangle.Height;

		return result;
	}

} // namespace sgre::render
