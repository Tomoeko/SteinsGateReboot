#pragma once

#include "sgre/resource/motion_mesh.hpp"
#include "sgre/scene/motion_deformation.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace sgre::scene {
	// Actual curve+8 cached Bernstein basis. Degrees1..6 retain native multiplication order; unused lanes are zero.
	[[nodiscard]] std::array<float, 16> MotionBezierBasisWeights(std::uint32_t degree, float coordinate);
	[[nodiscard]] std::size_t MotionBezierLatticeControlCount(std::uint8_t encoded_degree);
	void ValidateMotionBezierLattice(const resource::MotionBezierLattice& lattice);
	[[nodiscard]] resource::MotionBezierLattice IdentityMotionBezierLattice(std::uint8_t encoded_degree);
	[[nodiscard]] resource::MotionBezierLattice MotionBezierLatticeFromPatch(const resource::MotionBezierPatch& patch);
	[[nodiscard]] resource::MotionPoint EvaluateMotionBezierLattice(const resource::MotionBezierLattice& lattice, const resource::MotionPoint& coordinate);
	void EvaluateMotionBezierLatticeGrid(const resource::MotionBezierLattice& lattice, const MotionMeshSubdivision& subdivision, const MotionDeformationWindow& window, std::span<resource::MotionPoint> vertices);
	[[nodiscard]] resource::MotionBezierLattice ElevateMotionBezierLattice(const resource::MotionBezierLattice& lattice, std::uint8_t encoded_degree);
	// Native554400 elevates when both target axes grow; otherwise it samples target identity knots then inverts rows/columns.
	[[nodiscard]] resource::MotionBezierLattice ConvertMotionBezierLatticeDegree(const resource::MotionBezierLattice& lattice, std::uint8_t encoded_degree);
	[[nodiscard]] resource::MotionBezierLattice CombineMotionBezierLattices(const resource::MotionBezierLattice& first, const resource::MotionBezierLattice& second);
	// Native 5540a0 fits the centered control lattice and reports RMS decomposed axis scale.
	[[nodiscard]] float MeasureMotionBezierLatticeScale(const resource::MotionBezierLattice& lattice);
	// Native526df0 comparison uses squared component distance and may clear a retained identity lattice.
	[[nodiscard]] bool AssignMotionBezierLattice(resource::MotionBezierLattice& current, const resource::MotionBezierLattice* incoming, bool compare);
} // namespace sgre::scene
