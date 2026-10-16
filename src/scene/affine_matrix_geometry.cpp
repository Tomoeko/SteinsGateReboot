#include "sgre/scene/affine_matrix_geometry.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>

namespace sgre::scene {

	static float TruncateNativeAffineCoordinate(float value) noexcept {
		if (!(value >= -2147483648.0f && value < 2147483648.0f)) {
			return static_cast<float>(std::numeric_limits<std::int32_t>::min());
		}

		return static_cast<float>(static_cast<std::int32_t>(value));
	}

	bool IsAffineMatrixIdentity(const AffineMatrix& matrix) noexcept {
		return matrix.Rows == AffineMatrix{}.Rows;
	}

	void InitializeAffinePerspective(AffineMatrix& matrix) noexcept {
		matrix.Rows[14] = 1;
		matrix.Rows[15] = 0;
	}

	void TranslateAffineMatrixToInteger(AffineMatrix& matrix, float x, float y, float z) {
		for (std::size_t row = 0; row < 16; row += 4) {
			const auto xy = x * matrix.Rows[row] + y * matrix.Rows[row + 1];
			const auto xyz = xy + z * matrix.Rows[row + 2];
			matrix.Rows[row + 3] = TruncateNativeAffineCoordinate(xyz) + matrix.Rows[row + 3];
		}
	}

	static float TransformAffineCoordinate(const AffineMatrix& matrix, std::size_t row, float x, float y, float z) {
		const auto xy = x * matrix.Rows[row] + y * matrix.Rows[row + 1];
		const auto numerator = (xy + z * matrix.Rows[row + 2]) + matrix.Rows[row + 3];
		const auto denominator_xy = x * matrix.Rows[12] + y * matrix.Rows[13];
		const auto denominator = (denominator_xy + z * matrix.Rows[14]) + matrix.Rows[15];

		return numerator / denominator;
	}

	float TransformAffineX(const AffineMatrix& matrix, float x, float y, float z) {
		return TransformAffineCoordinate(matrix, 0, x, y, z);
	}

	float TransformAffineY(const AffineMatrix& matrix, float x, float y, float z) {
		return TransformAffineCoordinate(matrix, 4, x, y, z);
	}

	static std::array<float, 4> TransformAffineArea(const AffineMatrix& matrix, std::size_t row, float width, float height) {
		std::array<float, 4> result{};
		result[0] = matrix.Rows[row + 3] / matrix.Rows[15];
		result[1] = (width * matrix.Rows[row] + matrix.Rows[row + 3]) / (width * matrix.Rows[12] + matrix.Rows[15]);
		result[2] = (height * matrix.Rows[row + 1] + matrix.Rows[row + 3]) / (height * matrix.Rows[13] + matrix.Rows[15]);
		const auto numerator_xy = height * matrix.Rows[row + 1] + width * matrix.Rows[row];
		const auto denominator_xy = width * matrix.Rows[12] + height * matrix.Rows[13];
		result[3] = (numerator_xy + matrix.Rows[row + 3]) / (denominator_xy + matrix.Rows[15]);

		return result;
	}

	std::array<float, 4> TransformAffineAreaX(const AffineMatrix& matrix, float width, float height) {
		return TransformAffineArea(matrix, 0, width, height);
	}

	std::array<float, 4> TransformAffineAreaY(const AffineMatrix& matrix, float width, float height) {
		return TransformAffineArea(matrix, 4, width, height);
	}

} // namespace sgre::scene
