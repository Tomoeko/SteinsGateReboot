#pragma once

#include "sgre/scene/affine_matrix.hpp"

#include <array>

namespace sgre::scene {

	[[nodiscard]] bool IsAffineMatrixIdentity(const AffineMatrix& matrix) noexcept;
	// Native initPerspective writes only fourth-row Z/W; unrelated matrix values remain intact.
	void InitializeAffinePerspective(AffineMatrix& matrix) noexcept;
	void TranslateAffineMatrixToInteger(AffineMatrix& matrix, float x, float y, float z = 0);
	[[nodiscard]] float TransformAffineX(const AffineMatrix& matrix, float x, float y, float z = 0);
	[[nodiscard]] float TransformAffineY(const AffineMatrix& matrix, float x, float y, float z = 0);
	// Native arrays retain corner order: origin, width-only, height-only, width-and-height.
	[[nodiscard]] std::array<float, 4> TransformAffineAreaX(const AffineMatrix& matrix, float width, float height);
	[[nodiscard]] std::array<float, 4> TransformAffineAreaY(const AffineMatrix& matrix, float width, float height);

} // namespace sgre::scene
