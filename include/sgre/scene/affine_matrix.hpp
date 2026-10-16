#pragma once

#include "sgre/scene/affine_layer_parameters.hpp"
#include "sgre/scene/layers.hpp"

#include <array>
#include <optional>

namespace sgre::scene {

	struct AffineMatrix {
		std::array<float, 16> Rows{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
	};

	void TranslateAffineMatrix(AffineMatrix& matrix, float x, float y, float z = 0);
	void ScaleAffineMatrix(AffineMatrix& matrix, float x, float y, float z = 1);
	void SlantAffineMatrix(AffineMatrix& matrix, float x, float y);
	void FlipAffineMatrix(AffineMatrix& matrix, AffineAxis axis);
	void RotateAffineMatrix(AffineMatrix& matrix, float degrees);
	[[nodiscard]] AffineMatrix MultiplyAffineMatrices(const AffineMatrix& left, const AffineMatrix& right);
	// Native calcMatrix reads only its linked parent's position; outer task composition is separate.
	void ApplyAffineLayerMatrix(AffineMatrix& matrix, const AffineLayerParameterState& state, std::optional<LayerPoint> linked_position = {});
	void ApplyAffineImageMatrix(AffineMatrix& matrix, const AffineLayerParameterState& state);

} // namespace sgre::scene
