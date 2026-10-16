#pragma once

#include <cstdint>

namespace sgre::render {

	struct PresentationRectangle {
		double Left = 0;
		double Top = 0;
		double Width = 0;
		double Height = 0;
	};

	struct PresentationInputPoint {
		double X = 0;
		double Y = 0;
		bool Inside = false;
	};

	// Top-left pixel coordinates are shared by Metal's viewport and the inverse input projection.
	[[nodiscard]] PresentationRectangle FitPresentationRectangle(std::uint32_t source_width, std::uint32_t source_height, std::uint32_t destination_width, std::uint32_t destination_height);
	[[nodiscard]] PresentationInputPoint MapPresentationPoint(PresentationRectangle rectangle, std::uint32_t source_width, std::uint32_t source_height, double x, double y);

} // namespace sgre::render
