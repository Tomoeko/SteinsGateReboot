#include "sgre/scene/motion_scissor.hpp"

#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace sgre::scene {

	static void RequireFiniteBounds(const MotionBounds& bounds) {
		if (!std::isfinite(bounds.MinimumX) || !std::isfinite(bounds.MinimumY) || !std::isfinite(bounds.MaximumX) || !std::isfinite(bounds.MaximumY)) {
			throw std::runtime_error("motion scissor transform requires finite rectangle coordinates");
		}
	}

	static void RequireFiniteMatrix(const MotionScissorMatrix& matrix) {
		for (const auto coefficient : matrix.Coefficients) {
			if (!std::isfinite(coefficient)) {
				throw std::runtime_error("motion scissor transform requires finite affine coefficients");
			}
		}
	}

	static MotionScissorMatrix ComposeScissorMatrices(const MotionScissorMatrix& view, const MotionScissorMatrix& model) {
		MotionScissorMatrix result;
		for (std::size_t row = 0; row < 3; ++row) {
			for (std::size_t column = 0; column < 4; ++column) {
				const auto offset = row * 4;
				const auto xy = view.Coefficients[offset + 1] * model.Coefficients[column + 4] + view.Coefficients[offset] * model.Coefficients[column];
				const auto xyz = xy + view.Coefficients[offset + 2] * model.Coefficients[column + 8];
				result.Coefficients[offset + column] = column == 3 ? xyz + view.Coefficients[offset + 3] : xyz;
			}
		}

		return result;
	}

	static std::array<float, 2> TransformScissorCorner(const MotionScissorMatrix& matrix, float x, float y, MotionScissorViewport viewport) {
		const auto& coefficients = matrix.Coefficients;
		const auto transformed_x = (y * coefficients[1] + x * coefficients[0]) + 0.0f * coefficients[2] + coefficients[3];
		const auto transformed_y = (y * coefficients[5] + x * coefficients[4]) + 0.0f * coefficients[6] + coefficients[7];
		const auto half_width = static_cast<float>(viewport.Width / 2);
		const auto half_height = static_cast<float>(viewport.Height / 2);
		const auto screen_x = static_cast<float>(viewport.Width) * transformed_x * 0.5f + half_width;
		const auto screen_y = -half_height * transformed_y + half_height;

		return {screen_x, screen_y};
	}

	static float NativeMinimum(float first, float second) {
		return first < second ? first : second;
	}

	static float NativeMaximum(float first, float second) {
		return first > second ? first : second;
	}

	MotionBounds TransformMotionScissorBounds(const MotionBounds& bounds, const MotionScissorMatrix& view, const MotionScissorMatrix& model, MotionScissorViewport viewport) {
		RequireFiniteBounds(bounds);
		RequireFiniteMatrix(view);
		RequireFiniteMatrix(model);
		if (viewport.Width <= 0 || viewport.Height <= 0) {
			throw std::runtime_error("motion scissor transform requires positive physical viewport dimensions");
		}

		const auto matrix = ComposeScissorMatrices(view, model);
		RequireFiniteMatrix(matrix);
		const auto first = TransformScissorCorner(matrix, bounds.MinimumX, bounds.MinimumY, viewport);
		MotionBounds result;
		result.MinimumX = first[0];
		result.MinimumY = first[1];
		result.MaximumX = first[0];
		result.MaximumY = first[1];
		const std::array<std::array<float, 2>, 3> corners{{{bounds.MaximumX, bounds.MinimumY}, {bounds.MaximumX, bounds.MaximumY}, {bounds.MinimumX, bounds.MaximumY}}};
		for (const auto& corner : corners) {
			const auto screen = TransformScissorCorner(matrix, corner[0], corner[1], viewport);
			result.MinimumX = NativeMinimum(screen[0], result.MinimumX);
			result.MinimumY = NativeMinimum(screen[1], result.MinimumY);
			result.MaximumX = NativeMaximum(screen[0], result.MaximumX);
			result.MaximumY = NativeMaximum(screen[1], result.MaximumY);
		}

		RequireFiniteBounds(result);

		return result;
	}

	static float RoundScissorCoordinate(float value) {
		const auto bits = std::bit_cast<std::uint32_t>(value);
		const auto magnitude = bits & 0x7fffffffu;
		const auto sign = bits & 0x80000000u;
		if (magnitude >= 0x4b000000u) {
			return value;
		}

		if (magnitude < 0x3f800000u) {
			return std::bit_cast<float>(sign | (magnitude >= 0x3f000000u ? 0x3f800000u : 0u));
		}

		const auto shift = 150u - (magnitude >> 23);
		const auto rounded = (magnitude + (1u << (shift - 1))) & ~((1u << shift) - 1u);

		return std::bit_cast<float>(sign | rounded);
	}

	static std::int32_t TruncateScissorCoordinate(float value) {
		if (!std::isfinite(value) || value < -2147483648.0f || value >= 2147483648.0f) {
			// CVTTSS2SI returns the x86 integer-indefinite word for these inputs.
			return std::numeric_limits<std::int32_t>::min();
		}

		return static_cast<std::int32_t>(value);
	}

	MotionScissorUpdate PrepareMotionScissorUpdate(const MotionBounds& requested, const MotionBounds& applied_rounded) {
		MotionScissorUpdate update;
		update.Raw = requested;
		update.Rounded.MinimumX = RoundScissorCoordinate(requested.MinimumX);
		update.Rounded.MinimumY = RoundScissorCoordinate(requested.MinimumY);
		update.Rounded.MaximumX = RoundScissorCoordinate(requested.MaximumX);
		update.Rounded.MaximumY = RoundScissorCoordinate(requested.MaximumY);
		if (update.Rounded.MinimumX != applied_rounded.MinimumX || update.Rounded.MinimumY != applied_rounded.MinimumY || update.Rounded.MaximumX != applied_rounded.MaximumX || update.Rounded.MaximumY != applied_rounded.MaximumY) {
			MotionIntegerScissor rectangle;
			rectangle.MinimumX = TruncateScissorCoordinate(requested.MinimumX);
			rectangle.MinimumY = TruncateScissorCoordinate(requested.MinimumY);
			rectangle.MaximumX = TruncateScissorCoordinate(requested.MaximumX);
			rectangle.MaximumY = TruncateScissorCoordinate(requested.MaximumY);
			update.Submission = rectangle;
		}

		return update;
	}

} // namespace sgre::scene
