#include "sgre/scene/affine_plane_transform.hpp"

#include <bit>
#include <cstdint>

namespace sgre::scene {

	LayerAffineTransform PrepareAffinePlaneTransform(const AffineMatrix& matrix) {
		const auto translation_x = matrix.Rows[3] / matrix.Rows[15];
		const auto translation_y = matrix.Rows[7] / matrix.Rows[15];
		const auto unit_x_w = matrix.Rows[15] + matrix.Rows[12];
		const auto unit_y_w = matrix.Rows[15] + matrix.Rows[13];
		LayerAffineTransform result;
		result.TranslationX = translation_x;
		result.TranslationY = translation_y;
		result.XX = (matrix.Rows[3] + matrix.Rows[0]) / unit_x_w - translation_x;
		result.YX = (matrix.Rows[7] + matrix.Rows[4]) / unit_x_w - translation_y;
		result.XY = (matrix.Rows[3] + matrix.Rows[1]) / unit_y_w - translation_x;
		result.YY = (matrix.Rows[7] + matrix.Rows[5]) / unit_y_w - translation_y;

		return result;
	}

	static float FlipAffinePlaneSign(float value) noexcept {
		return std::bit_cast<float>(std::bit_cast<std::uint32_t>(value) ^ 0x80000000u);
	}

	LayerAffineTransform InvertAffinePlaneTransform(const LayerAffineTransform& transform) {
		const auto determinant = transform.YY * transform.XX - transform.XY * transform.YX;
		if (determinant == 0) {
			return {};
		}

		LayerAffineTransform result;
		result.XX = transform.YY / determinant;
		result.YX = FlipAffinePlaneSign(transform.YX) / determinant;
		result.XY = FlipAffinePlaneSign(transform.XY) / determinant;
		result.YY = transform.XX / determinant;
		// Native AffineLayer uses the opposite off-diagonal terms for these stored offsets.
		result.TranslationX = (transform.TranslationY * transform.YX - transform.TranslationX * transform.YY) / determinant;
		result.TranslationY = (transform.TranslationX * transform.XY - transform.TranslationY * transform.XX) / determinant;

		return result;
	}

} // namespace sgre::scene
