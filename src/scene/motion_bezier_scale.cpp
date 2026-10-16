#include "sgre/scene/motion_bezier_lattice.hpp"

#include "sgre/scene/motion_affine_decomposition.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace sgre::scene {
	float MeasureMotionBezierLatticeScale(const resource::MotionBezierLattice& lattice) {
		ValidateMotionBezierLattice(lattice);
		const auto horizontal = static_cast<std::uint32_t>(lattice.EncodedDegree & 15);
		const auto vertical = static_cast<std::uint32_t>(lattice.EncodedDegree >> 4);
		if ((horizontal < 1 || horizontal > 6) || (vertical < 1 || vertical > 6)) {
			throw std::runtime_error("motion Bezier scale requires recovered degree1..6 identity control knots");
		}

		float sum_x = 0;
		float sum_y = 0;
		for (const auto& point : lattice.Controls) {
			sum_x = sum_x + point.X;
			sum_y = sum_y + point.Y;
		}
		const float count = static_cast<float>(lattice.Controls.size());
		const float mean_x = sum_x / count;
		const float mean_y = sum_y / count;
		float xx = 0;
		float xy = 0;
		float yy = 0;
		float observed_xx = 0;
		float observed_xy = 0;
		float observed_yx = 0;
		float observed_yy = 0;
		for (std::uint32_t row = 0; row <= vertical; ++row) {
			for (std::uint32_t column = 0; column <= horizontal; ++column) {
				const float x = static_cast<float>(column) / static_cast<float>(horizontal) - 0.5f;
				const float y = static_cast<float>(row) / static_cast<float>(vertical) - 0.5f;
				const auto& point = lattice.Controls[static_cast<std::size_t>(row) * (horizontal + 1) + column];
				const float observed_x = point.X - mean_x;
				const float observed_y = point.Y - mean_y;
				xy = xy + y * x;
				xx = xx + x * x;
				observed_xx = observed_xx + observed_x * x;
				observed_xy = observed_xy + observed_x * y;
				yy = yy + y * y;
				observed_yx = observed_yx + observed_y * x;
				observed_yy = observed_yy + observed_y * y;
			}
		}
		const float determinant = yy * xx - xy * xy;
		constexpr float kMinimumCovariance = std::bit_cast<float>(std::uint32_t{0x322bcc77});
		if (std::abs(determinant) < kMinimumCovariance) {
			return 1;
		}

		const float inverse_off_diagonal = -xy / determinant;
		const float inverse_xx = yy / determinant;
		const float inverse_yy = xx / determinant;
		resource::MotionMeshMatrix matrix;
		matrix.XX = inverse_off_diagonal * observed_xy + inverse_xx * observed_xx;
		matrix.XY = inverse_yy * observed_xy + inverse_off_diagonal * observed_xx;
		matrix.YX = inverse_off_diagonal * observed_yy + inverse_xx * observed_yx;
		matrix.YY = inverse_yy * observed_yy + inverse_off_diagonal * observed_yx;
		const auto decomposition = DecomposeMotionAffine(matrix);
		if (!decomposition) {
			return 1;
		}
		const auto& scale = decomposition->Scale;
		const float result = std::sqrt((scale[1] * scale[1] + scale[0] * scale[0]) * 0.5f);
		if (!std::isfinite(result)) {
			throw std::runtime_error("motion Bezier fitted scale exceeds its finite domain");
		}

		return result;
	}
} // namespace sgre::scene
