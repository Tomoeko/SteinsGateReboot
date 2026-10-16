#include "sgre/scene/motion_bezier_lattice.hpp"
#include "motion_native_compared_mesh.hpp"

#include "sgre/scene/motion_bezier_inverse.hpp"
#include "sgre/scene/motion_deformation.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace sgre::scene {
	std::size_t MotionBezierLatticeControlCount(std::uint8_t encoded_degree) {
		if (encoded_degree == 0) {
			return 0;
		}

		return (static_cast<std::size_t>(encoded_degree & 15) + 1) * (static_cast<std::size_t>(encoded_degree >> 4) + 1);
	}

	void ValidateMotionBezierLattice(const resource::MotionBezierLattice& lattice) {
		if (lattice.Controls.size() != MotionBezierLatticeControlCount(lattice.EncodedDegree)) {
			throw std::runtime_error("motion Bezier lattice controls differ from the encoded axis degrees");
		}
		for (const auto& point : lattice.Controls) {
			if (!std::isfinite(point.X) || !std::isfinite(point.Y)) {
				throw std::runtime_error("motion Bezier lattice controls exceed finite coordinates");
			}
		}
	}

	resource::MotionBezierLattice IdentityMotionBezierLattice(std::uint8_t encoded_degree) {
		resource::MotionBezierLattice lattice;
		lattice.EncodedDegree = encoded_degree;
		if (encoded_degree == 0) {
			return lattice;
		}
		const auto horizontal = static_cast<std::uint32_t>(encoded_degree & 15);
		const auto vertical = static_cast<std::uint32_t>(encoded_degree >> 4);
		if (horizontal == 0 || vertical == 0) {
			throw std::runtime_error("motion Bezier lattice zero-axis identity operator remains unrecovered");
		}

		lattice.Controls.resize(MotionBezierLatticeControlCount(encoded_degree));
		for (std::uint32_t row = 0; row <= vertical; ++row) {
			for (std::uint32_t column = 0; column <= horizontal; ++column) {
				auto& point = lattice.Controls[static_cast<std::size_t>(row) * (horizontal + 1) + column];
				point.X = static_cast<float>(column) / static_cast<float>(horizontal);
				point.Y = static_cast<float>(row) / static_cast<float>(vertical);
			}
		}

		return lattice;
	}

	resource::MotionBezierLattice MotionBezierLatticeFromPatch(const resource::MotionBezierPatch& patch) {
		resource::MotionBezierLattice lattice;
		lattice.EncodedDegree = 0x33;
		lattice.Controls.assign(patch.Controls.begin(), patch.Controls.end());
		ValidateMotionBezierLattice(lattice);

		return lattice;
	}

	static std::array<float, 4> CubicWeights(float coordinate) {
		const auto inverse = 1.0f - coordinate;

		return {inverse * inverse * inverse, (inverse * 3.0f) * inverse * coordinate, (inverse * 3.0f) * coordinate * coordinate, coordinate * coordinate * coordinate};
	}

	static std::array<float, 5> QuarticWeights(float coordinate) {
		const auto inverse = 1.0f - coordinate;
		const auto last_cubic = ((coordinate * 4.0f) * coordinate) * coordinate;

		return {((inverse * inverse) * inverse) * inverse, (((inverse * 4.0f) * inverse) * coordinate) * inverse, (((inverse * 6.0f) * coordinate) * coordinate) * inverse, last_cubic * inverse, (coordinate * 0.25f) * last_cubic};
	}

	static std::array<float, 4> CachedCubicWeights(float coordinate) {
		const auto inverse = 1.0f - coordinate;
		const auto inverse_squared = inverse * inverse;
		const auto squared = coordinate * coordinate;

		return {inverse_squared * inverse, inverse_squared * coordinate * 3.0f, squared * inverse * 3.0f, squared * coordinate};
	}

	static std::array<float, 5> CachedQuarticWeights(float coordinate) {
		const auto inverse = 1.0f - coordinate;
		const auto squared = coordinate * coordinate;
		const auto inverse_squared = inverse * inverse;
		const auto inverse_cubic = inverse_squared * inverse;
		const auto cubic = squared * coordinate;

		return {inverse_cubic * inverse, inverse_cubic * coordinate * 4.0f, inverse_squared * squared * 6.0f, cubic * inverse * 4.0f, cubic * coordinate};
	}

	std::array<float, 16> MotionBezierBasisWeights(std::uint32_t degree, float coordinate) {
		if (degree < 1 || degree > 6 || !std::isfinite(coordinate)) {
			throw std::runtime_error("motion Bezier basis requires recovered degree1..6 and finite input");
		}
		std::array<float, 16> weights{};
		if (degree <= 2) {
			const auto inverse = 1.0f - coordinate;
			weights[0] = inverse;
			weights[1] = coordinate;
			if (degree == 2) {
				weights[0] = inverse * inverse;
				const auto product = inverse * coordinate;
				weights[1] = product + product;
				weights[2] = coordinate * coordinate;
			}
			return weights;
		}
		if (degree == 3) {
			const auto cubic = CachedCubicWeights(coordinate);
			std::copy(cubic.begin(), cubic.end(), weights.begin());
			return weights;
		}
		if (degree == 4) {
			const auto quartic = CachedQuarticWeights(coordinate);
			std::copy(quartic.begin(), quartic.end(), weights.begin());
			return weights;
		}
		static constexpr std::array<std::uint32_t, 7> kQuintic{1, 5, 10, 10, 5, 1, 0};
		static constexpr std::array<std::uint32_t, 7> kSextic{1, 6, 15, 20, 15, 6, 1};
		const auto& coefficients = degree == 5 ? kQuintic : kSextic;
		for (std::size_t index = 0; index <= degree; ++index) {
			weights[index] = static_cast<float>(coefficients[index]);
		}
		// Native53f8a0 accumulates powers in each direction, multiplying the integer coefficients first by u.
		float power = coordinate;
		for (std::size_t index = 1; index <= degree; ++index) {
			weights[index] = power * weights[index];
			power = power * coordinate;
		}
		const float inverse = 1.0f - coordinate;
		power = inverse;
		for (std::size_t index = degree; index > 0; --index) {
			weights[index - 1] = power * weights[index - 1];
			power = power * inverse;
		}
		return weights;
	}

	static resource::MotionPoint EvaluateQuarticLattice(const resource::MotionBezierLattice& lattice, const resource::MotionPoint& coordinate, bool cached) {
		const auto width = static_cast<std::size_t>(lattice.EncodedDegree & 15) + 1;
		const auto height = static_cast<std::size_t>(lattice.EncodedDegree >> 4) + 1;
		const auto cubic_u = cached ? CachedCubicWeights(coordinate.X) : CubicWeights(coordinate.X);
		const auto cubic_v = cached ? CachedCubicWeights(coordinate.Y) : CubicWeights(coordinate.Y);
		const auto quartic_u = cached ? CachedQuarticWeights(coordinate.X) : QuarticWeights(coordinate.X);
		const auto quartic_v = cached ? CachedQuarticWeights(coordinate.Y) : QuarticWeights(coordinate.Y);
		std::array<resource::MotionPoint, 4> columns;
		for (std::size_t column = 0; column < columns.size(); ++column) {
			const auto u = width == 5 ? quartic_u[column] : cubic_u[column];
			const auto& first = lattice.Controls[column];
			const auto& second = lattice.Controls[width + column];
			const auto& third = lattice.Controls[width * 2 + column];
			const auto& fourth = lattice.Controls[width * 3 + column];
			const auto& v = cubic_v;
			if (height == 4) {
				columns[column].X = ((second.X * v[1] + first.X * v[0]) + third.X * v[2] + fourth.X * v[3]) * u;
				columns[column].Y = ((second.Y * v[1] + first.Y * v[0]) + third.Y * v[2] + fourth.Y * v[3]) * u;
			} else {
				const auto& fifth = lattice.Controls[width * 4 + column];
				const auto& q = quartic_v;
				columns[column].X = (((second.X * q[1] + first.X * q[0]) + third.X * q[2]) + fourth.X * q[3] + fifth.X * q[4]) * u;
				columns[column].Y = (((second.Y * q[1] + first.Y * q[0]) + third.Y * q[2]) + fourth.Y * q[3] + fifth.Y * q[4]) * u;
			}
		}

		resource::MotionPoint point;
		if (width == 5) {
			// Native53dff0 distributes the fifth column across the two final SSE reduction lanes.
			const auto& a = lattice.Controls[4];
			const auto& b = lattice.Controls[9];
			const auto& c = lattice.Controls[14];
			const auto& d = lattice.Controls[19];
			point.X = ((columns[0].X + columns[1].X) + (a.X * cubic_v[0] + c.X * cubic_v[2]) * quartic_u[4]) + ((columns[2].X + columns[3].X) + (b.X * cubic_v[1] + d.X * cubic_v[3]) * quartic_u[4]);
			point.Y = ((columns[0].Y + columns[1].Y) + (a.Y * cubic_v[0] + c.Y * cubic_v[2]) * quartic_u[4]) + ((columns[2].Y + columns[3].Y) + (b.Y * cubic_v[1] + d.Y * cubic_v[3]) * quartic_u[4]);
		} else {
			point.X = (columns[0].X + columns[1].X) + (columns[2].X + columns[3].X);
			point.Y = (columns[0].Y + columns[1].Y) + (columns[2].Y + columns[3].Y);
		}

		return point;
	}

	static float EvaluateCurveComponent(std::span<const resource::MotionPoint> points, std::size_t degree, float coordinate, bool x_axis, bool cached) {
		const auto component = [&](std::size_t index) {
			return x_axis ? points[index].X : points[index].Y;
		};
		if (degree == 1) {
			return coordinate * component(1) + (1.0f - coordinate) * component(0);
		}
		if (!cached && degree == 2) {
			const auto inverse = 1.0f - coordinate;
			const auto middle = (inverse * component(1)) * coordinate;
			const auto first = (inverse * inverse) * component(0);
			const auto last = (coordinate * coordinate) * component(2);
			// Native5404a0 doubles the middle contribution before adding either endpoint.
			return x_axis ? ((middle + middle) + first) + last : (first + (middle + middle)) + last;
		}
		if (!cached && degree <= 4) {
			const auto inverse = 1.0f - coordinate;
			const auto inverse_squared = inverse * inverse;
			const auto squared = coordinate * coordinate;
			const auto inverse_cubic = inverse_squared * inverse;
			const auto cubic = squared * coordinate;
			if (degree == 3) {
				// Native540260 multiplies each actual control before its remaining Bernstein factors.
				return (((inverse_squared * component(1)) * coordinate) * 3.0f + inverse_cubic * component(0)) + ((inverse * component(2)) * squared) * 3.0f + cubic * component(3);
			}
			const auto inverse_quartic = inverse_cubic * inverse;
			const auto quartic = cubic * coordinate;
			return (((inverse_cubic * component(1)) * coordinate) * 4.0f + inverse_quartic * component(0)) + ((inverse_squared * component(2)) * squared) * 6.0f + ((inverse * component(3)) * cubic) * 4.0f + quartic * component(4);
		}
		const auto weights = MotionBezierBasisWeights(static_cast<std::uint32_t>(degree), coordinate);
		float output = weights[0] * component(0);
		for (std::size_t index = 1; index <= degree; ++index) {
			output = weights[index] * component(index) + output;
		}
		return output;
	}

	static resource::MotionPoint EvaluateGeneralLattice(const resource::MotionBezierLattice& lattice, const resource::MotionPoint& coordinate, bool cached) {
		const auto horizontal = static_cast<std::size_t>(lattice.EncodedDegree & 15);
		const auto vertical = static_cast<std::size_t>(lattice.EncodedDegree >> 4);
		if (horizontal < 1 || horizontal > 6 || vertical < 1 || vertical > 6) {
			throw std::runtime_error("motion general Bezier surface requires recovered degree1..6 axes");
		}
		const auto width = horizontal + 1;
		std::array<resource::MotionPoint, 7> rows{};
		// Native53e780/+28 point and542070/+20 cached-grid paths both reduce rows before columns.
		for (std::size_t row = 0; row <= vertical; ++row) {
			const auto controls = std::span(lattice.Controls).subspan(row * width, width);
			rows[row].X = EvaluateCurveComponent(controls, horizontal, coordinate.X, true, cached);
			rows[row].Y = EvaluateCurveComponent(controls, horizontal, coordinate.X, false, cached);
		}
		const auto controls = std::span(rows).first(vertical + 1);
		return {EvaluateCurveComponent(controls, vertical, coordinate.Y, true, cached), EvaluateCurveComponent(controls, vertical, coordinate.Y, false, cached)};
	}

	static void ValidateComparedLatticeStructure(const resource::MotionBezierLattice& lattice) {
		const auto horizontal = lattice.EncodedDegree & 15;
		const auto vertical = lattice.EncodedDegree >> 4;
		if (horizontal < 1 || horizontal > 6 || vertical < 1 || vertical > 6 || lattice.Controls.size() != static_cast<std::size_t>((horizontal + 1) * (vertical + 1))) {
			throw std::runtime_error("native compared mesh requires its exact bounded degree1..6 controls");
		}
	}

	resource::MotionPoint EvaluateUnorderedMotionBezierLattice(const resource::MotionBezierLattice& lattice, const resource::MotionPoint& coordinate) {
		ValidateComparedLatticeStructure(lattice);
		if (!std::isfinite(coordinate.X) || !std::isfinite(coordinate.Y)) {
			throw std::runtime_error("native compared mesh requires finite actual sample coordinates");
		}
		if (lattice.EncodedDegree == 0x33) {
			const auto horizontal = CubicWeights(coordinate.X);
			const auto vertical = CubicWeights(coordinate.Y);
			std::array<resource::MotionPoint, 4> columns;
			for (std::size_t column = 0; column != columns.size(); ++column) {
				columns[column].X = (lattice.Controls[column + 4].X * vertical[1] + lattice.Controls[column].X * vertical[0] + lattice.Controls[column + 8].X * vertical[2] + lattice.Controls[column + 12].X * vertical[3]) * horizontal[column];
				columns[column].Y = (lattice.Controls[column + 4].Y * vertical[1] + lattice.Controls[column].Y * vertical[0] + lattice.Controls[column + 8].Y * vertical[2] + lattice.Controls[column + 12].Y * vertical[3]) * horizontal[column];
			}
			return {(columns[0].X + columns[1].X) + (columns[2].X + columns[3].X), (columns[0].Y + columns[1].Y) + (columns[2].Y + columns[3].Y)};
		}
		return lattice.EncodedDegree == 0x34 || lattice.EncodedDegree == 0x43 ? EvaluateQuarticLattice(lattice, coordinate, false) : EvaluateGeneralLattice(lattice, coordinate, false);
	}

	resource::MotionPoint EvaluateMotionBezierLattice(const resource::MotionBezierLattice& lattice, const resource::MotionPoint& coordinate) {
		ValidateMotionBezierLattice(lattice);
		if (!std::isfinite(coordinate.X) || !std::isfinite(coordinate.Y)) {
			throw std::runtime_error("motion Bezier lattice input exceeds finite coordinates");
		}
		resource::MotionPoint output;
		if (lattice.EncodedDegree == 0x33) {
			resource::MotionBezierPatch patch;
			std::copy(lattice.Controls.begin(), lattice.Controls.end(), patch.Controls.begin());
			return EvaluateMotionBezierPatch(patch, coordinate);
		}
		if (lattice.EncodedDegree == 0x34 || lattice.EncodedDegree == 0x43) {
			output = EvaluateQuarticLattice(lattice, coordinate, false);
		} else {
			output = EvaluateGeneralLattice(lattice, coordinate, false);
		}
		if (!std::isfinite(output.X) || !std::isfinite(output.Y)) {
			throw std::runtime_error("motion Bezier lattice output exceeds finite coordinates");
		}

		return output;
	}

	void EvaluateMotionBezierLatticeGrid(const resource::MotionBezierLattice& lattice, const MotionMeshSubdivision& subdivision, const MotionDeformationWindow& window, std::span<resource::MotionPoint> vertices) {
		ValidateMotionBezierLattice(lattice);
		if (vertices.size() != MotionMeshGridVertexCount(subdivision) || !std::isfinite(window.Left) || !std::isfinite(window.Top) || !std::isfinite(window.Right) || !std::isfinite(window.Bottom)) {
			throw std::runtime_error("motion Bezier lattice grid requires its exact bounded subdivision and finite window");
		}
		if (lattice.EncodedDegree == 0x33) {
			resource::MotionBezierPatch patch;
			std::copy(lattice.Controls.begin(), lattice.Controls.end(), patch.Controls.begin());
			EvaluateMotionBezierGrid(patch, subdivision, window, vertices);
			return;
		}
		const auto width = static_cast<std::size_t>(subdivision.X) + 1;
		for (std::uint32_t row = 0; row <= subdivision.Y; ++row) {
			const auto vertical = static_cast<float>(row) / static_cast<float>(subdivision.Y);
			resource::MotionPoint coordinate;
			coordinate.Y = (1.0f - vertical) * window.Top + vertical * window.Bottom;
			for (std::uint32_t column = 0; column <= subdivision.X; ++column) {
				const auto horizontal = static_cast<float>(column) / static_cast<float>(subdivision.X);
				coordinate.X = (1.0f - horizontal) * window.Left + horizontal * window.Right;
				const auto point = lattice.EncodedDegree == 0x34 || lattice.EncodedDegree == 0x43 ? EvaluateQuarticLattice(lattice, coordinate, true) : EvaluateGeneralLattice(lattice, coordinate, true);
				if (!std::isfinite(point.X) || !std::isfinite(point.Y)) {
					throw std::runtime_error("motion Bezier lattice grid output exceeds finite coordinates");
				}
				vertices[static_cast<std::size_t>(row) * width + column] = point;
			}
		}
	}

	static void ElevateCurve(std::span<const resource::MotionPoint> input, std::span<resource::MotionPoint> output) {
		if (input.empty() || output.size() != input.size() + 1) {
			throw std::runtime_error("motion Bezier curve elevation requires its exact adjacent degrees");
		}
		output.front() = input.front();
		const auto degree = static_cast<float>(input.size());
		const auto reciprocal = 1.0f / degree;
		for (std::size_t index = 1; index < input.size(); ++index) {
			const auto i = static_cast<float>(index);
			output[index].X = (i * input[index - 1].X + (degree - i) * input[index].X) * reciprocal;
			output[index].Y = (i * input[index - 1].Y + (degree - i) * input[index].Y) * reciprocal;
		}
		output.back() = input.back();
	}

	resource::MotionBezierLattice ElevateMotionBezierLattice(const resource::MotionBezierLattice& lattice, std::uint8_t encoded_degree) {
		ValidateMotionBezierLattice(lattice);
		if (lattice.EncodedDegree == 0 || encoded_degree == 0 || (encoded_degree & 15) < (lattice.EncodedDegree & 15) || (encoded_degree >> 4) < (lattice.EncodedDegree >> 4)) {
			throw std::runtime_error("motion Bezier lattice degree reduction requires its unrecovered collocation inverse");
		}

		auto output = lattice;
		while (output.EncodedDegree != encoded_degree) {
			const auto width = static_cast<std::size_t>(output.EncodedDegree & 15) + 1;
			const auto height = static_cast<std::size_t>(output.EncodedDegree >> 4) + 1;
			const bool horizontal = (output.EncodedDegree & 15) < (encoded_degree & 15);
			resource::MotionBezierLattice elevated;
			elevated.EncodedDegree = static_cast<std::uint8_t>(output.EncodedDegree + (horizontal ? 1 : 16));
			elevated.Controls.resize(MotionBezierLatticeControlCount(elevated.EncodedDegree));
			if (horizontal) {
				for (std::size_t row = 0; row < height; ++row) {
					ElevateCurve(std::span(output.Controls).subspan(row * width, width), std::span(elevated.Controls).subspan(row * (width + 1), width + 1));
				}
			} else {
				std::array<resource::MotionPoint, 16> input_column;
				std::array<resource::MotionPoint, 16> output_column;
				for (std::size_t column = 0; column < width; ++column) {
					for (std::size_t row = 0; row < height; ++row) {
						input_column[row] = output.Controls[row * width + column];
					}
					ElevateCurve(std::span(input_column).first(height), std::span(output_column).first(height + 1));
					for (std::size_t row = 0; row <= height; ++row) {
						elevated.Controls[row * width + column] = output_column[row];
					}
				}
			}
			output = std::move(elevated);
		}
		ValidateMotionBezierLattice(output);

		return output;
	}

	resource::MotionBezierLattice ConvertMotionBezierLatticeDegree(const resource::MotionBezierLattice& lattice, std::uint8_t encoded_degree) {
		ValidateMotionBezierLattice(lattice);
		if (lattice.EncodedDegree == encoded_degree) {
			return lattice;
		}
		const auto horizontal = encoded_degree & 15;
		const auto vertical = encoded_degree >> 4;
		const auto source_horizontal = lattice.EncodedDegree & 15;
		const auto source_vertical = lattice.EncodedDegree >> 4;
		if (horizontal >= source_horizontal && vertical >= source_vertical) {
			return ElevateMotionBezierLattice(lattice, encoded_degree);
		}
		if (horizontal < 1 || horizontal > 6 || vertical < 1 || vertical > 6 || source_horizontal < 1 || source_horizontal > 6 || source_vertical < 1 || source_vertical > 6) {
			throw std::runtime_error("motion Bezier degree conversion requires its recovered degree1..6 collocation operators");
		}
		auto output = IdentityMotionBezierLattice(encoded_degree);
		// Native554400 samples the source operator at these actual target knots, preserving the chosen target degree.
		for (auto& sample : output.Controls) {
			sample = EvaluateMotionBezierLattice(lattice, sample);
		}
		ConvertMotionBezierSamplesToControls(output.Controls, encoded_degree);
		return output;
	}

	resource::MotionBezierLattice CombineMotionBezierLattices(const resource::MotionBezierLattice& first, const resource::MotionBezierLattice& second) {
		ValidateMotionBezierLattice(first);
		ValidateMotionBezierLattice(second);
		if (second.EncodedDegree == 0) {
			return first;
		}
		if (first.EncodedDegree == 0) {
			return second;
		}

		const bool second_grows_axis = (first.EncodedDegree & 15) < (second.EncodedDegree & 15) || (first.EncodedDegree >> 4) < (second.EncodedDegree >> 4);
		const auto degree = second_grows_axis ? second.EncodedDegree : first.EncodedDegree;
		auto output = ConvertMotionBezierLatticeDegree(first, degree);
		const auto other = ConvertMotionBezierLatticeDegree(second, degree);
		const auto identity = IdentityMotionBezierLattice(degree);
		for (std::size_t index = 0; index < output.Controls.size(); ++index) {
			output.Controls[index].X = (output.Controls[index].X + other.Controls[index].X) - identity.Controls[index].X;
			output.Controls[index].Y = (output.Controls[index].Y + other.Controls[index].Y) - identity.Controls[index].Y;
		}
		ValidateMotionBezierLattice(output);

		return output;
	}

	static bool EquivalentLatticeControls(const resource::MotionBezierLattice& first, const resource::MotionBezierLattice& second) {
		if (first.EncodedDegree != second.EncodedDegree) {
			return false;
		}
		static constexpr auto kSquaredTolerance = std::bit_cast<float>(std::uint32_t{0x358637bd});
		for (std::size_t index = 0; index < first.Controls.size(); ++index) {
			const auto x = first.Controls[index].X - second.Controls[index].X;
			const auto y = first.Controls[index].Y - second.Controls[index].Y;
			if (x * x > kSquaredTolerance || y * y > kSquaredTolerance) {
				return false;
			}
		}

		return true;
	}

	bool AssignMotionBezierLattice(resource::MotionBezierLattice& current, const resource::MotionBezierLattice* incoming, bool compare) {
		ValidateMotionBezierLattice(current);
		if (incoming != nullptr) {
			ValidateMotionBezierLattice(*incoming);
		}
		if (!compare) {
			current = incoming == nullptr ? resource::MotionBezierLattice{} : *incoming;
			return true;
		}
		if (incoming == nullptr) {
			const bool changed = current.EncodedDegree != 0;
			current = {};
			return changed;
		}
		if (incoming->EncodedDegree == 0) {
			throw std::runtime_error("motion Bezier lattice comparison requires null for a native clear request");
		}
		const auto identity = IdentityMotionBezierLattice(incoming->EncodedDegree);
		if (current.EncodedDegree == 0) {
			if (EquivalentLatticeControls(identity, *incoming)) {
				return false;
			}
		} else if (EquivalentLatticeControls(current, *incoming)) {
			if (EquivalentLatticeControls(identity, *incoming)) {
				current = {};
				return true;
			}
			return false;
		}

		current = *incoming;
		return true;
	}

	bool AssignNativeComparedMotionBezierLattice(resource::MotionBezierLattice& current, const resource::MotionBezierLattice& incoming) {
		ValidateMotionBezierLattice(current);
		ValidateComparedLatticeStructure(incoming);
		const auto identity = IdentityMotionBezierLattice(incoming.EncodedDegree);
		// Native554e80's COMISS/JA accepts unordered squared deltas. A no-op or clear never publishes those values.
		if (current.EncodedDegree == 0) {
			if (EquivalentLatticeControls(identity, incoming)) {
				return false;
			}
		} else if (EquivalentLatticeControls(current, incoming)) {
			if (EquivalentLatticeControls(identity, incoming)) {
				current = {};
				return true;
			}
			return false;
		}
		// Mixed finite/unordered output can require a native copy; that unresolved drawing domain remains rejected.
		ValidateMotionBezierLattice(incoming);
		current = incoming;
		return true;
	}
} // namespace sgre::scene
