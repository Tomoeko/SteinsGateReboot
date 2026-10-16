#include "sgre/scene/motion_affine_decomposition.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace sgre::scene {
	std::optional<MotionAffineDecomposition> DecomposeMotionAffine(const resource::MotionMeshMatrix& matrix, const MotionRotationReference& reference) {
		for (const auto value : {matrix.XX, matrix.XY, matrix.YX, matrix.YY, reference.Radians, reference.Cosine, reference.Sine}) {
			if (!std::isfinite(value)) {
				throw std::runtime_error("motion affine decomposition requires finite linear coefficients and cached rotation");
			}
		}
		const float determinant = matrix.YY * matrix.XX - matrix.YX * matrix.XY;
		if (!std::isfinite(determinant)) {
			throw std::runtime_error("motion affine determinant exceeds its finite domain");
		}
		constexpr float kMinimumDeterminant = std::bit_cast<float>(std::uint32_t{0x358637bd});
		if (std::abs(determinant) < kMinimumDeterminant) {
			return std::nullopt;
		}

		MotionAffineDecomposition result;
		result.Reflected = determinant < 0;
		float a = matrix.XX;
		float b = matrix.XY;
		float c = matrix.YX;
		float d = matrix.YY;
		if (result.Reflected) {
			a = matrix.YX * 0.0f - matrix.XX;
			b = matrix.YY * 0.0f - matrix.XY;
			c = matrix.XX * 0.0f + matrix.YX;
			d = matrix.XY * 0.0f + matrix.YY;
		}
		const float rotated_a = reference.Sine * c + reference.Cosine * a;
		const float rotated_b = reference.Cosine * b + reference.Sine * d;
		const float rotated_c = reference.Cosine * c + -reference.Sine * a;
		const float rotated_d = reference.Cosine * d + -reference.Sine * b;
		const float first_angle = std::atan2(rotated_c, rotated_a);
		const float second_angle = std::atan2(-rotated_b, rotated_d);
		const float second_weight = std::abs(second_angle) <= std::abs(first_angle) ? 1.0f : 0.0f;
		const float angle = (1.0f - second_weight) * first_angle + second_weight * second_angle;
		const float sine = std::sin(-angle);
		const float cosine = std::cos(-angle);

		result.Scale[0] = cosine * rotated_a + -sine * rotated_c;
		result.Scale[1] = sine * rotated_b + cosine * rotated_d;
		const float horizontal_shear = cosine * rotated_b + -sine * rotated_d;
		const float vertical_shear = sine * rotated_a + cosine * rotated_c;
		result.Shear[0] = std::abs(result.Scale[0]) <= kMinimumDeterminant ? 0.0f : horizontal_shear / result.Scale[0];
		result.Shear[1] = std::abs(result.Scale[1]) <= kMinimumDeterminant ? 0.0f : vertical_shear / result.Scale[1];
		result.Rotation.Radians = angle + reference.Radians;
		result.Rotation.Sine = std::sin(result.Rotation.Radians);
		result.Rotation.Cosine = std::cos(result.Rotation.Radians);
		for (const auto value : {result.Scale[0], result.Scale[1], result.Shear[0], result.Shear[1], result.Rotation.Radians, result.Rotation.Cosine, result.Rotation.Sine}) {
			if (!std::isfinite(value)) {
				throw std::runtime_error("motion affine decomposition exceeds its finite output domain");
			}
		}

		return result;
	}
} // namespace sgre::scene
