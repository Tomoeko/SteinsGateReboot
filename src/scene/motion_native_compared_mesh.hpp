#pragma once

#include "sgre/resource/motion_mesh.hpp"

#include <cstdint>
#include <span>

namespace sgre::scene {
	// Native zero-time physical owners may produce unordered transient controls. Only the final comparison may publish a finite result.
	void ConvertUnorderedMotionBezierSamplesToControls(std::span<resource::MotionPoint> samples, std::uint8_t encoded_degree);
	void ConvertUnorderedMotionBezierSamplesToControls(std::span<resource::MotionPoint> samples, std::uint8_t encoded_degree, std::span<const float> horizontal_knots, std::span<const float> vertical_knots);
	[[nodiscard]] resource::MotionPoint EvaluateUnorderedMotionBezierLattice(const resource::MotionBezierLattice& lattice, const resource::MotionPoint& coordinate);
	bool AssignNativeComparedMotionBezierLattice(resource::MotionBezierLattice& current, const resource::MotionBezierLattice& incoming);
} // namespace sgre::scene
