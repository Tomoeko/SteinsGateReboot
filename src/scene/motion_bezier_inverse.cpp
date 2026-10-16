#include "sgre/scene/motion_bezier_inverse.hpp"

#include "motion_native_compared_mesh.hpp"
#include "sgre/scene/motion_bezier_lattice.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace sgre::scene {
	struct MotionInverseBasis {
		std::size_t Degree = 0;
		std::array<std::array<float, 2>, 5> EndpointWeights{};
		std::array<std::array<float, 5>, 5> Coefficients{};
		std::array<float, 5> ReciprocalPivots{};
	};

	static std::array<float, 7> KnotWeights(std::size_t degree, float coordinate) {
		const auto inverse = 1.0f - coordinate;
		const auto inverse_squared = inverse * inverse;
		const auto squared = coordinate * coordinate;
		if (degree == 2) {
			const auto product = inverse * coordinate;
			return {inverse_squared, product + product, squared, 0, 0, 0, 0};
		}
		if (degree == 3) {
			return {inverse_squared * inverse, (inverse_squared * coordinate) * 3.0f, (squared * inverse) * 3.0f, squared * coordinate, 0.0f};
		}

		const auto inverse_cubic = inverse_squared * inverse;
		const auto cubic = squared * coordinate;
		if (degree == 4) {
			return {inverse_cubic * inverse, (inverse_cubic * coordinate) * 4.0f, (inverse_squared * squared) * 6.0f, (cubic * inverse) * 4.0f, cubic * coordinate, 0, 0};
		}
		if (degree == 5) {
			const auto inverse_quartic = inverse_cubic * inverse;
			const auto quartic = cubic * coordinate;
			// Degree5's specialized inverse preparation543040 groups these powers before its LU factorization.
			return {inverse * inverse_quartic, (5.0f * coordinate) * inverse_quartic, (10.0f * squared) * inverse_cubic, (10.0f * cubic) * inverse_squared, (5.0f * quartic) * inverse, coordinate * quartic, 0};
		}
		const auto weights = MotionBezierBasisWeights(static_cast<std::uint32_t>(degree), coordinate);
		std::array<float, 7> output{};
		std::copy_n(weights.begin(), degree + 1, output.begin());
		return output;
	}

	static void ValidateInverseKnots(std::span<const float> knots, std::size_t degree) {
		if (knots.empty()) {
			return;
		}
		if (knots.size() != degree + 1 || knots.front() != 0 || knots.back() != 1) {
			throw std::runtime_error("motion Bezier inverse knots require exact degree endpoints0/1");
		}
		for (std::size_t index = 0; index < knots.size(); ++index) {
			if (!std::isfinite(knots[index]) || (index != 0 && knots[index] <= knots[index - 1])) {
				throw std::runtime_error("motion Bezier inverse knots require a finite strictly increasing basis");
			}
		}
	}

	static MotionInverseBasis PrepareInverseBasis(std::size_t degree, std::span<const float> knots) {
		MotionInverseBasis basis;
		basis.Degree = degree;
		if (degree == 1) {
			return basis;
		}
		std::array<std::array<float, 7>, 5> rows{};
		for (std::size_t index = 0; index < degree - 1; ++index) {
			const auto coordinate = knots.empty() ? static_cast<float>(index + 1) / static_cast<float>(degree) : knots[index + 1];
			rows[index] = KnotWeights(degree, coordinate);
			basis.EndpointWeights[index] = {rows[index][0], rows[index][degree]};
		}

		if (degree >= 5) {
			const auto count = degree - 1;
			for (std::size_t row = 0; row < count; ++row) {
				for (std::size_t column = 0; column < count; ++column) {
					basis.Coefficients[row][column] = rows[row][column + 1];
				}
			}
			// Native543040/5433e0 retain L below the diagonal and U on/above it, without pivoting.
			for (std::size_t pivot = 0; pivot + 1 < count; ++pivot) {
				for (std::size_t row = pivot + 1; row < count; ++row) {
					auto& factor = basis.Coefficients[row][pivot];
					factor = factor / basis.Coefficients[pivot][pivot];
					for (std::size_t column = pivot + 1; column < count; ++column) {
						basis.Coefficients[row][column] = basis.Coefficients[row][column] - basis.Coefficients[pivot][column] * factor;
					}
				}
			}
			for (std::size_t row = 0; row < count; ++row) {
				basis.ReciprocalPivots[row] = 1.0f / basis.Coefficients[row][row];
			}
			return basis;
		}

		if (degree == 2) {
			basis.Coefficients[0][0] = 1.0f / rows[0][1];
			return basis;
		}
		const auto a = rows[0][1];
		const auto b = rows[0][2];
		if (degree == 3) {
			const auto c = rows[1][1];
			const auto d = rows[1][2];
			const auto reciprocal = 1.0f / (d * a - c * b);
			basis.Coefficients[0] = {d * reciprocal, -b * reciprocal, 0.0f};
			basis.Coefficients[1] = {-c * reciprocal, a * reciprocal, 0.0f};

			return basis;
		}

		const auto c = rows[0][3];
		const auto d = rows[1][1];
		const auto e = rows[1][2];
		const auto f = rows[1][3];
		const auto g = rows[2][1];
		const auto h = rows[2][2];
		const auto i = rows[2][3];
		const auto fa = f * a;
		const auto fb = f * b;
		const auto ec = e * c;
		const auto ea = e * a;
		const auto dc = d * c;
		const auto db = d * b;
		// Native543950 groups the six rounded determinant terms in this order.
		const auto determinant = ((((i * ea + g * fb) + h * dc) - g * ec) - i * db) - h * fa;
		const auto reciprocal = 1.0f / determinant;
		basis.Coefficients[0] = {(i * e - h * f) * reciprocal, -(i * b - h * c) * reciprocal, (fb - ec) * reciprocal};
		basis.Coefficients[1] = {-(i * d - g * f) * reciprocal, (i * a - g * c) * reciprocal, -(fa - dc) * reciprocal};
		basis.Coefficients[2] = {(h * d - g * e) * reciprocal, -(h * a - g * b) * reciprocal, (ea - db) * reciprocal};

		return basis;
	}

	static void ConvertCurve(std::span<resource::MotionPoint> samples, std::size_t stride, const MotionInverseBasis& basis) {
		if (basis.Degree == 1) {
			return;
		}
		const auto first = samples.front();
		const auto last = samples[basis.Degree * stride];
		std::array<resource::MotionPoint, 5> residuals{};
		for (std::size_t index = 0; index < basis.Degree - 1; ++index) {
			const auto& sample = samples[(index + 1) * stride];
			const auto& endpoints = basis.EndpointWeights[index];
			residuals[index].X = (sample.X - first.X * endpoints[0]) - last.X * endpoints[1];
			residuals[index].Y = (sample.Y - first.Y * endpoints[0]) - last.Y * endpoints[1];
		}

		if (basis.Degree == 2) {
			const auto reciprocal = basis.Coefficients[0][0];
			samples[stride] = {reciprocal * residuals[0].X, reciprocal * residuals[0].Y};
			return;
		}
		if (basis.Degree >= 5) {
			const auto count = basis.Degree - 1;
			for (std::size_t row = 1; row < count; ++row) {
				for (std::size_t column = 0; column < row; ++column) {
					residuals[row].X = residuals[row].X - basis.Coefficients[row][column] * residuals[column].X;
					residuals[row].Y = residuals[row].Y - basis.Coefficients[row][column] * residuals[column].Y;
				}
			}
			for (std::size_t following = count; following > 0; --following) {
				const auto row = following - 1;
				for (std::size_t column = row + 1; column < count; ++column) {
					residuals[row].X = residuals[row].X - basis.Coefficients[row][column] * residuals[column].X;
					residuals[row].Y = residuals[row].Y - basis.Coefficients[row][column] * residuals[column].Y;
				}
				residuals[row].X = basis.ReciprocalPivots[row] * residuals[row].X;
				residuals[row].Y = basis.ReciprocalPivots[row] * residuals[row].Y;
				samples[(row + 1) * stride] = residuals[row];
			}
			samples.front() = first;
			samples[basis.Degree * stride] = last;
			return;
		}

		for (std::size_t index = 0; index < basis.Degree - 1; ++index) {
			const auto& coefficients = basis.Coefficients[index];
			auto& control = samples[(index + 1) * stride];
			// Native53f3a0/53f610 multiply the second residual before adding the first.
			control.X = residuals[1].X * coefficients[1] + residuals[0].X * coefficients[0];
			control.Y = residuals[1].Y * coefficients[1] + residuals[0].Y * coefficients[0];
			if (basis.Degree == 4) {
				control.X = control.X + residuals[2].X * coefficients[2];
				control.Y = control.Y + residuals[2].Y * coefficients[2];
			}
		}
		samples.front() = first;
		samples[basis.Degree * stride] = last;
	}

	static void ConvertSamplesToControls(std::span<resource::MotionPoint> samples, std::uint8_t encoded_degree, std::span<const float> horizontal_knots, std::span<const float> vertical_knots, bool unordered_comparison) {
		const auto horizontal = static_cast<std::size_t>(encoded_degree & 15);
		const auto vertical = static_cast<std::size_t>(encoded_degree >> 4);
		if ((horizontal < 1 || horizontal > 6) || (vertical < 1 || vertical > 6) || samples.size() != (horizontal + 1) * (vertical + 1)) {
			throw std::runtime_error("motion sampled Bezier inverse requires its exact recovered degree1..6 collocation grid");
		}
		ValidateInverseKnots(horizontal_knots, horizontal);
		ValidateInverseKnots(vertical_knots, vertical);
		for (const auto& sample : samples) {
			if (!unordered_comparison && (!std::isfinite(sample.X) || !std::isfinite(sample.Y))) {
				throw std::runtime_error("motion sampled Bezier inverse requires finite input points");
			}
		}

		std::array<resource::MotionPoint, 49> controls;
		std::copy(samples.begin(), samples.end(), controls.begin());
		const auto active = std::span(controls).first(samples.size());
		const auto width = horizontal + 1;
		const auto horizontal_basis = PrepareInverseBasis(horizontal, horizontal_knots);
		const auto vertical_basis = PrepareInverseBasis(vertical, vertical_knots);
		for (std::size_t row = 0; row <= vertical; ++row) {
			ConvertCurve(active.subspan(row * width, width), 1, horizontal_basis);
		}
		for (std::size_t column = 0; column < width; ++column) {
			ConvertCurve(active.subspan(column), width, vertical_basis);
		}
		for (const auto& control : active) {
			if (!unordered_comparison && (!std::isfinite(control.X) || !std::isfinite(control.Y))) {
				throw std::runtime_error("motion sampled Bezier inverse exceeds finite control points");
			}
		}

		std::copy(active.begin(), active.end(), samples.begin());
	}

	void ConvertMotionBezierSamplesToControls(std::span<resource::MotionPoint> samples, std::uint8_t encoded_degree) {
		ConvertSamplesToControls(samples, encoded_degree, {}, {}, false);
	}

	void ConvertMotionBezierSamplesToControls(std::span<resource::MotionPoint> samples, std::uint8_t encoded_degree, std::span<const float> horizontal_knots, std::span<const float> vertical_knots) {
		ConvertSamplesToControls(samples, encoded_degree, horizontal_knots, vertical_knots, false);
	}

	void ConvertUnorderedMotionBezierSamplesToControls(std::span<resource::MotionPoint> samples, std::uint8_t encoded_degree, std::span<const float> horizontal_knots, std::span<const float> vertical_knots) {
		ConvertSamplesToControls(samples, encoded_degree, horizontal_knots, vertical_knots, true);
	}

	void ConvertUnorderedMotionBezierSamplesToControls(std::span<resource::MotionPoint> samples, std::uint8_t encoded_degree) {
		ConvertSamplesToControls(samples, encoded_degree, {}, {}, true);
	}
} // namespace sgre::scene
