#pragma once

#include "sgre/resource/motion_mesh.hpp"

#include <cstdint>
#include <span>

namespace sgre::scene {
	// Samples use identity knots and row-major axis degrees. Both axes must have a recovered degree1..6.
	// Native30c7b0 converts contiguous rows before strided columns; String orientation follows afterward.
	void ConvertMotionBezierSamplesToControls(std::span<resource::MotionPoint> samples, std::uint8_t encoded_degree);
	// Empty axis knots select uniform identity knots. Nonempty arrays retain the real basis input, including endpoints0/1.
	void ConvertMotionBezierSamplesToControls(std::span<resource::MotionPoint> samples, std::uint8_t encoded_degree, std::span<const float> horizontal_knots, std::span<const float> vertical_knots);
} // namespace sgre::scene
