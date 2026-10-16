#pragma once

#include "sgre/scene/affine_matrix.hpp"

namespace sgre::scene {

	// Native AffineLayer projects origin and unit endpoints before forming these deltas.
	[[nodiscard]] LayerAffineTransform PrepareAffinePlaneTransform(const AffineMatrix& matrix);
	// Returns native stored inverse coefficients, including their recovered offset selection.
	// Singular matrices publish identity; nonzero determinants use individual division.
	[[nodiscard]] LayerAffineTransform InvertAffinePlaneTransform(const LayerAffineTransform& transform);

} // namespace sgre::scene
