#include "sgre/scene/affine_matrix.hpp"

#include "sgre/scene/affine_trigonometry.hpp"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace sgre::scene {

	static constexpr float kNativePi = 3.14159274f;

	static float NegateFloatBits(float value) noexcept {
		return std::bit_cast<float>(std::bit_cast<std::uint32_t>(value) ^ 0x80000000u);
	}

	void TranslateAffineMatrix(AffineMatrix& matrix, float x, float y, float z) {
		for (std::size_t row = 0; row < 16; row += 4) {
			const auto xy = x * matrix.Rows[row] + y * matrix.Rows[row + 1];
			const auto xyz = xy + z * matrix.Rows[row + 2];
			matrix.Rows[row + 3] = xyz + matrix.Rows[row + 3];
		}
	}

	void ScaleAffineMatrix(AffineMatrix& matrix, float x, float y, float z) {
		// Original Matrix scaling changes the first three rows; the fourth row is retained.
		for (std::size_t row = 0; row < 12; row += 4) {
			matrix.Rows[row] *= x;
			matrix.Rows[row + 1] *= y;
			matrix.Rows[row + 2] *= z;
		}
	}

	void SlantAffineMatrix(AffineMatrix& matrix, float x, float y) {
		for (std::size_t row = 0; row < 12; row += 4) {
			const auto first = matrix.Rows[row];
			const auto second = matrix.Rows[row + 1];
			matrix.Rows[row] = second * y + first;
			matrix.Rows[row + 1] = first * x + second;
		}
	}

	void FlipAffineMatrix(AffineMatrix& matrix, AffineAxis axis) {
		if (axis != AffineAxis::X && axis != AffineAxis::Y) {
			throw std::runtime_error("unknown affine matrix flip axis");
		}

		const std::size_t column = axis == AffineAxis::X ? 0 : 1;
		for (std::size_t row = 0; row < 16; row += 4) {
			matrix.Rows[row + column] = NegateFloatBits(matrix.Rows[row + column]);
		}
	}

	void RotateAffineMatrix(AffineMatrix& matrix, float degrees) {
		const auto half_turn = degrees * kNativePi;
		const auto radians = (half_turn + half_turn) / 360.0f;
		if (!std::isfinite(radians)) {
			throw std::runtime_error("affine matrix nonfinite rotation parity is not recovered");
		}

		const auto trigonometry = PrepareAffineTrigonometry(radians);
		const auto sine = trigonometry.Sine;
		const auto cosine = trigonometry.Cosine;
		for (std::size_t row = 0; row < 12; row += 4) {
			const auto first = matrix.Rows[row];
			const auto second = matrix.Rows[row + 1];
			matrix.Rows[row] = second * sine + first * cosine;
			matrix.Rows[row + 1] = second * cosine - first * sine;
		}
	}

	AffineMatrix MultiplyAffineMatrices(const AffineMatrix& left, const AffineMatrix& right) {
		AffineMatrix result;
		for (std::size_t row = 0; row < 16; row += 4) {
			for (std::size_t column = 0; column < 4; ++column) {
				const auto first_pair = left.Rows[row + 1] * right.Rows[column + 4] + left.Rows[row] * right.Rows[column];
				const auto third = first_pair + left.Rows[row + 2] * right.Rows[column + 8];
				result.Rows[row + column] = third + left.Rows[row + 3] * right.Rows[column + 12];
			}
		}

		return result;
	}

	static float PivotCoordinate(const AffineOptionalCoordinate& override_coordinate, float center) noexcept {
		return override_coordinate.Enabled ? override_coordinate.Value : center;
	}

	void ApplyAffineLayerMatrix(AffineMatrix& matrix, const AffineLayerParameterState& state, std::optional<LayerPoint> linked_position) {
		if (linked_position) {
			TranslateAffineMatrix(matrix, linked_position->X, linked_position->Y);
		}
		TranslateAffineMatrix(matrix, state.PositionX, state.PositionY);

		if (state.Rotation != 0) {
			const auto x = PivotCoordinate(state.RotationCenterX, state.CenterX);
			const auto y = PivotCoordinate(state.RotationCenterY, state.CenterY);
			TranslateAffineMatrix(matrix, x, y);
			RotateAffineMatrix(matrix, state.Rotation);
			TranslateAffineMatrix(matrix, NegateFloatBits(x), NegateFloatBits(y));
		}

		if (state.ZoomX != 1 || state.ZoomY != 1) {
			const auto x = PivotCoordinate(state.ZoomCenterX, state.CenterX);
			const auto y = PivotCoordinate(state.ZoomCenterY, state.CenterY);
			TranslateAffineMatrix(matrix, x, y);
			ScaleAffineMatrix(matrix, state.ZoomX, state.ZoomY);
			TranslateAffineMatrix(matrix, NegateFloatBits(x), NegateFloatBits(y));
		}

		if (state.SlantX != 0 || state.SlantY != 0) {
			SlantAffineMatrix(matrix, state.SlantX, state.SlantY);
		}
		if (state.FlipY) {
			FlipAffineMatrix(matrix, AffineAxis::Y);
		}
		if (state.FlipX) {
			FlipAffineMatrix(matrix, AffineAxis::X);
		}
		TranslateAffineMatrix(matrix, state.OffsetX, state.OffsetY);
	}

	void ApplyAffineImageMatrix(AffineMatrix& matrix, const AffineLayerParameterState& state) {
		if (state.ImageX != 0 || state.ImageY != 0) {
			TranslateAffineMatrix(matrix, state.ImageX, state.ImageY);
		}
		if (state.ImageRotation != 0) {
			RotateAffineMatrix(matrix, state.ImageRotation);
		}
		if (state.ImageZoom != 1) {
			ScaleAffineMatrix(matrix, state.ImageZoom, state.ImageZoom);
		}
	}

} // namespace sgre::scene
