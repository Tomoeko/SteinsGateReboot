#include "sgre/scene/motion_deformation.hpp"

#include "sgre/scene/motion_bezier_lattice.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace sgre::scene {

	resource::MotionBezierPatch IdentityMotionBezierPatch() {
		resource::MotionBezierPatch patch;
		for (std::size_t row = 0; row < 4; ++row) {
			for (std::size_t column = 0; column < 4; ++column) {
				patch.Controls[row * 4 + column].X = static_cast<float>(column) / 3.0f;
				patch.Controls[row * 4 + column].Y = static_cast<float>(row) / 3.0f;
			}
		}

		return patch;
	}

	static void ValidatePatch(const resource::MotionBezierPatch& patch) {
		for (const auto& point : patch.Controls) {
			if (!std::isfinite(point.X) || !std::isfinite(point.Y)) {
				throw std::runtime_error("motion Bezier patch has nonfinite control coordinates");
			}
		}
	}

	std::optional<resource::MotionBezierPatch> InterpolateMotionBezierPatch(const std::optional<resource::MotionBezierPatch>& start, const std::optional<resource::MotionBezierPatch>& end, float ratio, const resource::EasingCurve* easing) {
		if (!std::isfinite(ratio)) {
			throw std::runtime_error("motion Bezier patch interpolation ratio is nonfinite");
		}

		if (!start && !end) {
			return std::nullopt;
		}

		const auto first = start ? *start : IdentityMotionBezierPatch();
		const auto last = end ? *end : IdentityMotionBezierPatch();
		ValidatePatch(first);
		ValidatePatch(last);
		const auto position = easing ? resource::EvaluateEasing(*easing, ratio) : ratio;
		static constexpr float kNativeEndpointTolerance = 0x1p-23f;
		if (std::abs(position) < kNativeEndpointTolerance) {
			return first;
		}

		if (std::abs(1.0f - position) < kNativeEndpointTolerance) {
			return last;
		}

		resource::MotionBezierPatch patch;
		for (std::size_t index = 0; index < patch.Controls.size(); ++index) {
			patch.Controls[index].X = (last.Controls[index].X - first.Controls[index].X) * position + first.Controls[index].X;
			patch.Controls[index].Y = (last.Controls[index].Y - first.Controls[index].Y) * position + first.Controls[index].Y;
		}

		ValidatePatch(patch);

		return patch;
	}

	static std::array<float, 4> CubicPatchWeights(float coordinate) {
		const auto inverse = 1.0f - coordinate;
		// This order matches the direct SSE mesh evaluator, including the early multiplication by 3.
		return {inverse * inverse * inverse, (inverse * 3.0f) * inverse * coordinate, (inverse * 3.0f) * coordinate * coordinate, coordinate * coordinate * coordinate};
	}

	static resource::MotionPoint EvaluatePatchWeights(const resource::MotionBezierPatch& patch, const std::array<float, 4>& u, const std::array<float, 4>& v) {
		std::array<resource::MotionPoint, 4> columns;
		for (std::size_t column = 0; column < columns.size(); ++column) {
			columns[column].X = (patch.Controls[column + 4].X * v[1] + patch.Controls[column].X * v[0] + patch.Controls[column + 8].X * v[2] + patch.Controls[column + 12].X * v[3]) * u[column];
			columns[column].Y = (patch.Controls[column + 4].Y * v[1] + patch.Controls[column].Y * v[0] + patch.Controls[column + 8].Y * v[2] + patch.Controls[column + 12].Y * v[3]) * u[column];
		}

		resource::MotionPoint point;
		point.X = (columns[0].X + columns[1].X) + (columns[2].X + columns[3].X);
		point.Y = (columns[0].Y + columns[1].Y) + (columns[2].Y + columns[3].Y);
		if (!std::isfinite(point.X) || !std::isfinite(point.Y)) {
			throw std::runtime_error("motion Bezier patch output exceeds finite coordinates");
		}

		return point;
	}

	resource::MotionPoint EvaluateMotionBezierPatch(const resource::MotionBezierPatch& patch, const resource::MotionPoint& coordinate) {
		ValidatePatch(patch);
		if (!std::isfinite(coordinate.X) || !std::isfinite(coordinate.Y)) {
			throw std::runtime_error("motion Bezier patch input coordinate is nonfinite");
		}

		return EvaluatePatchWeights(patch, CubicPatchWeights(coordinate.X), CubicPatchWeights(coordinate.Y));
	}

	std::optional<resource::MotionBezierPatch> CombineMotionBezierPatches(const std::optional<resource::MotionBezierPatch>& first, const std::optional<resource::MotionBezierPatch>& second) {
		if (!first) {
			if (second) {
				ValidatePatch(*second);
			}

			return second;
		}

		ValidatePatch(*first);
		if (!second) {
			return first;
		}

		ValidatePatch(*second);
		const auto identity = IdentityMotionBezierPatch();
		resource::MotionBezierPatch combined;
		for (std::size_t index = 0; index < combined.Controls.size(); ++index) {
			combined.Controls[index].X = (first->Controls[index].X + second->Controls[index].X) - identity.Controls[index].X;
			combined.Controls[index].Y = (first->Controls[index].Y + second->Controls[index].Y) - identity.Controls[index].Y;
		}

		ValidatePatch(combined);

		return combined;
	}

	static void ValidateMatrix(const resource::MotionMeshMatrix& matrix) {
		if (!std::isfinite(matrix.XX) || !std::isfinite(matrix.XY) || !std::isfinite(matrix.TranslationX) || !std::isfinite(matrix.YX) || !std::isfinite(matrix.YY) || !std::isfinite(matrix.TranslationY)) {
			throw std::runtime_error("motion deformation matrix has nonfinite coefficients");
		}
	}

	resource::MotionMeshMatrix InverseMotionMeshMatrix(const resource::MotionMeshMatrix& matrix) {
		ValidateMatrix(matrix);
		const auto determinant = matrix.YY * matrix.XX - matrix.YX * matrix.XY;
		if (!std::isfinite(determinant) || determinant == 0) {
			throw std::runtime_error("motion deformation matrix is singular or overflows its determinant");
		}

		const auto reciprocal = 1.0f / determinant;
		resource::MotionMeshMatrix inverse;
		inverse.XX = reciprocal * matrix.YY;
		inverse.XY = -matrix.XY * reciprocal;
		inverse.TranslationX = (matrix.TranslationY * matrix.XY - matrix.TranslationX * matrix.YY) * reciprocal;
		inverse.YX = -matrix.YX * reciprocal;
		inverse.YY = reciprocal * matrix.XX;
		inverse.TranslationY = (matrix.TranslationX * matrix.YX - matrix.TranslationY * matrix.XX) * reciprocal;
		ValidateMatrix(inverse);

		return inverse;
	}

	resource::MotionPoint TransformMotionMeshPoint(const resource::MotionMeshMatrix& matrix, const resource::MotionPoint& point) {
		ValidateMatrix(matrix);
		if (!std::isfinite(point.X) || !std::isfinite(point.Y)) {
			throw std::runtime_error("motion deformation point is nonfinite");
		}

		resource::MotionPoint transformed;
		transformed.X = point.Y * matrix.XY + point.X * matrix.XX + matrix.TranslationX;
		transformed.Y = point.Y * matrix.YY + point.X * matrix.YX + matrix.TranslationY;
		if (!std::isfinite(transformed.X) || !std::isfinite(transformed.Y)) {
			throw std::runtime_error("motion deformation transform exceeds finite coordinates");
		}

		return transformed;
	}

	resource::MotionBezierPatch TransformMotionBezierPatch(const resource::MotionBezierPatch& patch, const resource::MotionMeshMatrix& matrix) {
		ValidatePatch(patch);
		ValidateMatrix(matrix);
		resource::MotionBezierPatch transformed;
		for (std::size_t index = 0; index < transformed.Controls.size(); ++index) {
			transformed.Controls[index] = TransformMotionMeshPoint(matrix, patch.Controls[index]);
		}

		return transformed;
	}

	PreparedMotionDeformation PrepareMotionDeformation(const resource::MotionBezierPatch& patch, const resource::MotionMeshMatrix& space) {
		PreparedMotionDeformation deformation;
		deformation.WorldPatch = TransformMotionBezierPatch(patch, space);
		deformation.WorldToPatch = InverseMotionMeshMatrix(space);

		return deformation;
	}

	PreparedMotionDeformation PrepareMotionDeformation(const resource::MotionBezierLattice& lattice, const resource::MotionMeshMatrix& space) {
		ValidateMotionBezierLattice(lattice);
		if (lattice.EncodedDegree == 0x33) {
			resource::MotionBezierPatch patch;
			std::copy(lattice.Controls.begin(), lattice.Controls.end(), patch.Controls.begin());

			return PrepareMotionDeformation(patch, space);
		}
		const auto horizontal = lattice.EncodedDegree & 15;
		const auto vertical = lattice.EncodedDegree >> 4;
		if (horizontal < 1 || horizontal > 6 || vertical < 1 || vertical > 6) {
			throw std::runtime_error("motion world lattice requires its recovered degree1..6 operator");
		}

		PreparedMotionDeformation deformation;
		deformation.WorldToPatch = InverseMotionMeshMatrix(space);
		deformation.WorldLattice = lattice;
		for (auto& control : deformation.WorldLattice->Controls) {
			control = TransformMotionMeshPoint(space, control);
		}

		return deformation;
	}

	void EvaluateMotionDeformationGrid(const PreparedMotionDeformation& deformation, const MotionMeshSubdivision& subdivision, const MotionDeformationWindow& window, std::span<resource::MotionPoint> vertices) {
		if (deformation.WorldLattice) {
			EvaluateMotionBezierLatticeGrid(*deformation.WorldLattice, subdivision, window, vertices);
		} else {
			EvaluateMotionBezierGrid(deformation.WorldPatch, subdivision, window, vertices);
		}
	}

	void ApplyMotionDeformation(const PreparedMotionDeformation& deformation, std::span<resource::MotionPoint> positions) {
		static constexpr std::size_t kMaxPositions = 1000000;
		if (deformation.WorldLattice) {
			ValidateMotionBezierLattice(*deformation.WorldLattice);
			const auto degree = deformation.WorldLattice->EncodedDegree;
			const auto horizontal = degree & 15;
			const auto vertical = degree >> 4;
			if (degree == 0x33 || horizontal < 1 || horizontal > 6 || vertical < 1 || vertical > 6) {
				throw std::runtime_error("motion world lattice discriminator requires its actual generalized degree1..6 operator");
			}
		} else {
			ValidatePatch(deformation.WorldPatch);
		}
		ValidateMatrix(deformation.WorldToPatch);
		if (positions.size() > kMaxPositions) {
			throw std::runtime_error("motion deformation position count exceeds its limit");
		}

		for (const auto& position : positions) {
			if (!std::isfinite(position.X) || !std::isfinite(position.Y)) {
				throw std::runtime_error("motion deformation input position is nonfinite");
			}
		}

		for (auto& position : positions) {
			const auto coordinate = TransformMotionMeshPoint(deformation.WorldToPatch, position);
			// Original 0x140554850 applies the inverse space before evaluating the transformed controls.
			position = deformation.WorldLattice ? EvaluateMotionBezierLattice(*deformation.WorldLattice, coordinate) : EvaluatePatchWeights(deformation.WorldPatch, CubicPatchWeights(coordinate.X), CubicPatchWeights(coordinate.Y));
		}
	}

	MotionMeshSubdivision SelectMotionMeshSubdivision(std::uint32_t division, std::uint32_t width, std::uint32_t height) {
		static constexpr auto kNativeIntegerLimit = static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max());
		if (width > kNativeIntegerLimit || height > kNativeIntegerLimit - width || width + height == 0 || division > kNativeIntegerLimit || (width != 0 && division > kNativeIntegerLimit / width)) {
			throw std::runtime_error("motion mesh subdivision dimensions exceed native integer arithmetic");
		}

		const auto horizontal = division * width / (width + height);
		const auto vertical = division - horizontal;
		MotionMeshSubdivision subdivision;
		subdivision.X = std::max(std::uint32_t{1}, horizontal) + 1;
		subdivision.Y = std::max(std::uint32_t{1}, vertical) + 1;
		static_cast<void>(MotionMeshGridVertexCount(subdivision));

		return subdivision;
	}

	std::size_t MotionMeshGridVertexCount(const MotionMeshSubdivision& subdivision) {
		static constexpr std::uint32_t kMaxAxisSubdivision = 4096;
		static constexpr std::size_t kMaxGridVertices = 1000000;
		if (subdivision.X == 0 || subdivision.Y == 0 || subdivision.X > kMaxAxisSubdivision || subdivision.Y > kMaxAxisSubdivision) {
			throw std::runtime_error("motion mesh grid subdivision is zero or exceeds its axis limit");
		}

		const auto columns = static_cast<std::size_t>(subdivision.X) + 1;
		const auto rows = static_cast<std::size_t>(subdivision.Y) + 1;
		if (rows > kMaxGridVertices / columns) {
			throw std::runtime_error("motion mesh grid exceeds its vertex limit");
		}

		return rows * columns;
	}

	static std::array<float, 4> CachedCubicWeights(float coordinate) {
		const auto inverse = 1.0f - coordinate;
		const auto inverse_squared = inverse * inverse;
		const auto coordinate_squared = coordinate * coordinate;
		// Native cache generation at 0x14053fcb0 differs from the direct point evaluator's ordering.
		return {inverse_squared * inverse, inverse_squared * coordinate * 3.0f, coordinate_squared * inverse * 3.0f, coordinate_squared * coordinate};
	}

	void EvaluateMotionBilinearGrid(const std::array<resource::MotionPoint, 4>& corners, const MotionMeshSubdivision& subdivision, std::span<resource::MotionPoint> vertices) {
		if (vertices.size() != MotionMeshGridVertexCount(subdivision)) {
			throw std::runtime_error("motion bilinear grid output differs from its vertex count");
		}

		for (const auto& point : corners) {
			if (!std::isfinite(point.X) || !std::isfinite(point.Y)) {
				throw std::runtime_error("motion bilinear grid has nonfinite corner coordinates");
			}
		}

		const auto columns = static_cast<std::size_t>(subdivision.X) + 1;
		const auto horizontal_step = 1.0f / static_cast<float>(subdivision.X);
		const auto vertical_step = 1.0f / static_cast<float>(subdivision.Y);
		for (std::uint32_t row = 0; row <= subdivision.Y; ++row) {
			const auto vertical = static_cast<float>(row) * vertical_step;
			const auto inverse_vertical = 1.0f - vertical;
			const auto left_x = vertical * corners[2].X + inverse_vertical * corners[0].X;
			const auto left_y = inverse_vertical * corners[0].Y + vertical * corners[2].Y;
			const auto right_x = inverse_vertical * corners[1].X + vertical * corners[3].X;
			const auto right_y = vertical * corners[3].Y + inverse_vertical * corners[1].Y;
			for (std::uint32_t column = 0; column <= subdivision.X; ++column) {
				const auto horizontal = static_cast<float>(column) * horizontal_step;
				auto& position = vertices[static_cast<std::size_t>(row) * columns + column];
				position.X = (1.0f - horizontal) * left_x + horizontal * right_x;
				position.Y = (1.0f - horizontal) * left_y + horizontal * right_y;
				if (!std::isfinite(position.X) || !std::isfinite(position.Y)) {
					throw std::runtime_error("motion bilinear grid exceeds finite coordinates");
				}
			}
		}
	}

	void EvaluateMotionBezierGrid(const resource::MotionBezierPatch& patch, const MotionMeshSubdivision& subdivision, const MotionDeformationWindow& window, std::span<resource::MotionPoint> vertices) {
		ValidatePatch(patch);
		const auto count = MotionMeshGridVertexCount(subdivision);
		if (vertices.size() != count || !std::isfinite(window.Left) || !std::isfinite(window.Top) || !std::isfinite(window.Right) || !std::isfinite(window.Bottom)) {
			throw std::runtime_error("motion mesh grid output span or deformation window is invalid");
		}

		const auto columns = static_cast<std::size_t>(subdivision.X) + 1;
		for (std::uint32_t row = 0; row <= subdivision.Y; ++row) {
			const auto vertical = static_cast<float>(row) / static_cast<float>(subdivision.Y);
			const auto v = CachedCubicWeights((1.0f - vertical) * window.Top + vertical * window.Bottom);
			for (std::uint32_t column = 0; column <= subdivision.X; ++column) {
				const auto horizontal = static_cast<float>(column) / static_cast<float>(subdivision.X);
				const auto u = CachedCubicWeights((1.0f - horizontal) * window.Left + horizontal * window.Right);
				vertices[static_cast<std::size_t>(row) * columns + column] = EvaluatePatchWeights(patch, u, v);
			}
		}
	}

} // namespace sgre::scene
