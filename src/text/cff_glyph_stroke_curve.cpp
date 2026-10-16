#include "sgre/text/cff_glyph_stroke_curve.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace sgre::text {
	using StrokePoint = CffGlyphPathPoint;
	using StrokeQuadratic = std::array<StrokePoint, 3>;
	using StrokeCubic = std::array<StrokePoint, 4>;
	static constexpr std::size_t kMaximumStrokeCurvePieces = 4096;
	static constexpr double kStrokeAngleCondition = 0.414213562;
	static constexpr double kStrokeCubicTolerance = 0.05;

	static StrokePoint AddStrokePoints(StrokePoint first, StrokePoint second) {
		return {first.X + second.X, first.Y + second.Y};
	}

	static StrokePoint SubtractStrokePoints(StrokePoint first, StrokePoint second) {
		return {first.X - second.X, first.Y - second.Y};
	}

	static StrokePoint ScaleStrokePoint(StrokePoint point, double factor) {
		return {point.X * factor, point.Y * factor};
	}

	static double StrokeDot(StrokePoint first, StrokePoint second) {
		return first.X * second.X + first.Y * second.Y;
	}

	static double StrokeCross(StrokePoint first, StrokePoint second) {
		return first.X * second.Y - first.Y * second.X;
	}

	static StrokePoint InterpolateStrokePoint(StrokePoint first, StrokePoint second, double parameter) {
		return {(first.X - first.X * parameter) + second.X * parameter, (first.Y - first.Y * parameter) + second.Y * parameter};
	}

	static bool SameStrokeCurvePoint(StrokePoint first, StrokePoint second) {
		return std::bit_cast<std::uint64_t>(first.X) == std::bit_cast<std::uint64_t>(second.X) && std::bit_cast<std::uint64_t>(first.Y) == std::bit_cast<std::uint64_t>(second.Y);
	}

	static StrokePoint StrokeNormal(StrokePoint tangent) {
		const auto squared_length = StrokeDot(tangent, tangent);
		if (!std::isfinite(squared_length) || squared_length == 0) {
			throw std::runtime_error("CFF stroke curve requires its recovered nondegenerate tangent");
		}
		const auto length = std::sqrt(squared_length);

		return {-tangent.Y / length, tangent.X / length};
	}

	static std::pair<StrokeQuadratic, StrokeQuadratic> SplitStrokeQuadratic(const StrokeQuadratic& curve, double parameter) {
		const auto first = InterpolateStrokePoint(curve[0], curve[1], parameter);
		const auto second = InterpolateStrokePoint(curve[1], curve[2], parameter);
		const auto middle = InterpolateStrokePoint(first, second, parameter);

		return {{curve[0], first, middle}, {middle, second, curve[2]}};
	}

	static std::pair<StrokeCubic, StrokeCubic> SplitStrokeCubic(const StrokeCubic& curve, double parameter) {
		const auto first = InterpolateStrokePoint(curve[0], curve[1], parameter);
		const auto second = InterpolateStrokePoint(curve[1], curve[2], parameter);
		const auto third = InterpolateStrokePoint(curve[2], curve[3], parameter);
		const auto left_control = InterpolateStrokePoint(first, second, parameter);
		const auto right_control = InterpolateStrokePoint(second, third, parameter);
		const auto middle = InterpolateStrokePoint(left_control, right_control, parameter);

		return {{curve[0], first, left_control, middle}, {middle, right_control, third, curve[3]}};
	}

	static std::array<double, 3> StrokeCurvatureParameters(const StrokeQuadratic& curve, double radius, std::size_t& count) {
		const auto first = SubtractStrokePoints(curve[1], curve[0]);
		const auto second = SubtractStrokePoints(curve[2], curve[1]);
		const auto initial_derivative = ScaleStrokePoint(first, 2);
		const auto derivative_delta = SubtractStrokePoints(ScaleStrokePoint(second, 2), initial_derivative);
		const auto determinant = StrokeCross(initial_derivative, derivative_delta);
		std::array<double, 3> parameters{};
		count = 0;
		if (determinant != 0) {
			const auto mixed = StrokeDot(derivative_delta, initial_derivative);
			const auto squared_delta = StrokeDot(derivative_delta, derivative_delta);
			const auto squared_initial = StrokeDot(initial_derivative, initial_derivative);
			const auto curvature_term = ((radius * radius) * determinant) * determinant;
			const auto discriminant = (mixed * mixed) - (squared_initial - std::cbrt(curvature_term)) * squared_delta;
			if (discriminant >= 0) {
				const auto root = std::sqrt(discriminant);
				const auto reciprocal = -1.0 / squared_delta;
				const auto first_parameter = std::max(0.0, (mixed + root) * reciprocal);
				const auto second_parameter = (mixed - root) * reciprocal;
				if (first_parameter > 0 && first_parameter < 1) {
					parameters[count++] = first_parameter;
				}
				if (second_parameter > first_parameter && second_parameter < 1) {
					parameters[count++] = second_parameter;
				}
			}
		}
		parameters[count++] = 1;

		return parameters;
	}

	static void AppendOffsetQuadratic(CffStrokeCurveSides& output, const StrokeQuadratic& curve, double radius, bool direct_conic = false) {
		// Native434D9A skips an equal-endpoint reduced piece without changing its completed normal.
		if (SameStrokeCurvePoint(curve[0], curve[2])) {
			return;
		}
		if (output.Left.size() >= kMaximumStrokeCurvePieces) {
			throw std::runtime_error("CFF stroke curve exceeds its bounded offset-piece storage");
		}
		const auto first_tangent = SameStrokeCurvePoint(curve[0], curve[1]) ? SubtractStrokePoints(curve[2], curve[1]) : SubtractStrokePoints(curve[1], curve[0]);
		const auto last_tangent = SameStrokeCurvePoint(curve[1], curve[2]) ? SubtractStrokePoints(curve[1], curve[0]) : SubtractStrokePoints(curve[2], curve[1]);
		const auto first_normal = StrokeNormal(first_tangent);
		const auto last_normal = StrokeNormal(last_tangent);
		const auto sum_normal = AddStrokePoints(first_normal, last_normal);
		const auto squared_normal = StrokeDot(sum_normal, sum_normal);
		if (!std::isfinite(squared_normal) || squared_normal == 0) {
			throw std::runtime_error("CFF stroke quadratic reaches native nonfinite reversing-normal controls");
		}
		const auto control_offset = direct_conic ? StrokePoint{sum_normal.X * (radius + radius) / squared_normal, sum_normal.Y * (radius + radius) / squared_normal} : ScaleStrokePoint(sum_normal, (radius + radius) / squared_normal);
		const auto endpoint_offset = ScaleStrokePoint(last_normal, radius);
		CffGlyphPathCommand left;
		left.Kind = CffGlyphPathCommandKind::QuadraticTo;
		left.Points[0] = AddStrokePoints(curve[1], control_offset);
		left.Points[1] = AddStrokePoints(curve[2], endpoint_offset);
		CffGlyphPathCommand right;
		right.Kind = CffGlyphPathCommandKind::QuadraticTo;
		right.Points[0] = SubtractStrokePoints(curve[1], control_offset);
		right.Points[1] = SubtractStrokePoints(curve[2], endpoint_offset);
		for (std::size_t index = 0; index < 2; ++index) {
			if (!std::isfinite(left.Points[index].X) || !std::isfinite(left.Points[index].Y) || !std::isfinite(right.Points[index].X) || !std::isfinite(right.Points[index].Y)) {
				throw std::runtime_error("CFF stroke quadratic reaches native nonfinite offset coordinates");
			}
		}
		output.EndNormal = last_normal;
		output.Left.push_back(left);
		output.Right.push_back(right);
	}

	static void PrepareStrokeQuadratic(CffStrokeCurveSides& output, const StrokeQuadratic& curve, double radius) {
		std::size_t parameter_count = 0;
		const auto parameters = StrokeCurvatureParameters(curve, radius, parameter_count);
		auto remaining = curve;
		double previous_parameter = 0;
		for (std::size_t index = 0; index < parameter_count; ++index) {
			const auto parameter = parameters[index];
			const auto relative = (parameter - previous_parameter) / (1.0 - previous_parameter);
			auto [current, tail] = SplitStrokeQuadratic(remaining, relative);
			remaining = tail;
			previous_parameter = parameter;
			for (;;) {
				const auto first = SubtractStrokePoints(current[1], current[0]);
				const auto second = SubtractStrokePoints(current[2], current[1]);
				const auto delta = SubtractStrokePoints(second, first);
				const auto numerator = StrokeDot(first, first) * kStrokeAngleCondition;
				const auto denominator = std::abs(StrokeCross(delta, first)) - StrokeDot(delta, first) * kStrokeAngleCondition;
				const auto split = numerator / denominator;
				if (!(split > 0 && split < 1)) {
					AppendOffsetQuadratic(output, current, radius);
					break;
				}
				auto [piece, rest] = SplitStrokeQuadratic(current, split);
				AppendOffsetQuadratic(output, piece, radius);
				current = rest;
			}
		}
	}

	static StrokePoint RepairStrokeCubicControl(const StrokeCubic& curve, StrokePoint midpoint, bool at_start) {
		const auto [left, right] = SplitStrokeCubic(curve, .5);
		const auto tangent = ScaleStrokePoint(SubtractStrokePoints(right[1], left[2]), 3);
		const auto anchor = at_start ? curve[0] : curve[3];
		auto guide = at_start ? SubtractStrokePoints(curve[1], curve[0]) : SubtractStrokePoints(curve[3], curve[2]);
		if (guide.X == 0 && guide.Y == 0) {
			guide = at_start ? SubtractStrokePoints(curve[2], curve[0]) : SubtractStrokePoints(curve[3], curve[1]);
		}
		if (guide.X == 0 && guide.Y == 0) {
			guide = SubtractStrokePoints(curve[3], curve[0]);
		}
		const auto displacement = SubtractStrokePoints(midpoint, anchor);
		const auto numerator = displacement.Y * tangent.X - displacement.X * tangent.Y;
		const auto denominator = guide.Y * tangent.X - guide.X * tangent.Y;
		const auto parameter = numerator / denominator;
		const auto result = AddStrokePoints(anchor, ScaleStrokePoint(guide, parameter));
		if (!std::isfinite(result.X) || !std::isfinite(result.Y)) {
			throw std::runtime_error("CFF stroke cubic reaches native nonfinite tangent-intersection controls");
		}

		return result;
	}

	static void ReduceStrokeCubic(CffStrokeCurveSides& output, StrokeCubic curve, double radius) {
		const auto first = SubtractStrokePoints(curve[1], curve[0]);
		const auto second = SubtractStrokePoints(curve[2], curve[1]);
		const auto third = SubtractStrokePoints(curve[3], curve[2]);
		const auto difference = AddStrokePoints(SubtractStrokePoints(SubtractStrokePoints(third, second), second), first);
		const auto tolerance = kStrokeCubicTolerance * 54.0;
		auto parameter = std::pow((tolerance * tolerance) / StrokeDot(difference, difference), 1.0 / 6.0);
		for (std::size_t piece = 0; piece < kMaximumStrokeCurvePieces; ++piece) {
			parameter = std::min(parameter, 1.0);
			if (!(parameter < 0.999)) {
				parameter = 1;
			}
			auto [current, remaining] = SplitStrokeCubic(curve, parameter);
			auto first_control = InterpolateStrokePoint(current[0], current[1], 0.75);
			auto second_control = InterpolateStrokePoint(current[3], current[2], 0.75);
			const auto middle = AddStrokePoints(ScaleStrokePoint(first_control, 0.5), ScaleStrokePoint(second_control, 0.5));
			// Native434362/4345D8 retain endpoint tangents when reduction places a control at its endpoint.
			if (SameStrokeCurvePoint(first_control, current[0])) {
				first_control = RepairStrokeCubicControl(current, middle, true);
			}
			if (SameStrokeCurvePoint(second_control, current[3])) {
				second_control = RepairStrokeCubicControl(current, middle, false);
			}
			PrepareStrokeQuadratic(output, {current[0], first_control, middle}, radius);
			PrepareStrokeQuadratic(output, {middle, second_control, current[3]}, radius);
			if (parameter == 1) {
				return;
			}
			const auto previous = parameter;
			parameter /= 1.0 - parameter;
			// Native4351E7..217 tests this signed difference before applying its literal progress nudge.
			if (!(0.001 <= previous - parameter)) {
				parameter += 0.01;
			}
			curve = remaining;
		}
		throw std::runtime_error("CFF stroke cubic reduction exceeds its bounded piece count");
	}

	CffStrokeCurveSides PrepareCffGlyphStrokeCurve(const std::array<CffGlyphPathPoint, 4>& cubic, double width_pixels) {
		if (!std::isfinite(width_pixels) || width_pixels <= 0 || width_pixels > 65534) {
			throw std::runtime_error("CFF stroke curve requires a finite bounded positive native pixel width");
		}
		for (const auto point : cubic) {
			if (!std::isfinite(point.X) || !std::isfinite(point.Y) || std::abs(point.X) > 1048576 || std::abs(point.Y) > 1048576) {
				throw std::runtime_error("CFF stroke curve requires finite bounded translated coordinates");
			}
		}
		CffStrokeCurveSides result;
		// Native433030 normalizes the source tangent before reduction and its adaptive splits.
		auto tangent = SubtractStrokePoints(cubic[1], cubic[0]);
		if (tangent.X == 0 && tangent.Y == 0) {
			tangent = SubtractStrokePoints(cubic[2], cubic[0]);
		}
		result.StartNormal = StrokeNormal(tangent);
		ReduceStrokeCubic(result, cubic, width_pixels * 0.5);

		return result;
	}

	CffStrokeCurveSides PrepareCffGlyphStrokeQuadratic(const std::array<CffGlyphPathPoint, 3>& quadratic, double width_pixels) {
		if (!std::isfinite(width_pixels) || width_pixels <= 0 || width_pixels > 65534) {
			throw std::runtime_error("CFF stroke conic requires a finite bounded positive native pixel width");
		}
		for (const auto point : quadratic) {
			if (!std::isfinite(point.X) || !std::isfinite(point.Y) || std::abs(point.X) > 1048576 || std::abs(point.Y) > 1048576) {
				throw std::runtime_error("CFF stroke conic requires finite bounded translated coordinates");
			}
		}
		const auto first = SubtractStrokePoints(quadratic[1], quadratic[0]);
		const auto last = SubtractStrokePoints(quadratic[2], quadratic[1]);
		if (StrokeDot(first, first) < 1.0000000000000001e-20 || StrokeDot(last, last) < 1.0000000000000001e-20) {
			throw std::runtime_error("CFF nonflat conic requires its native bounded endpoint tangents");
		}
		CffStrokeCurveSides result;
		result.StartNormal = StrokeNormal(first);
		std::size_t parameter_count = 0;
		const auto radius = width_pixels * .5;
		const auto parameters = StrokeCurvatureParameters(quadratic, radius, parameter_count);
		auto previous_left = quadratic[0];
		auto previous_right = quadratic[1];
		auto previous_end = quadratic[0];
		for (std::size_t index = 0; index < parameter_count; ++index) {
			const auto parameter = parameters[index];
			// Native4308EF retains the original-curve tangent pair, not a normalized tail.
			const auto control = InterpolateStrokePoint(previous_left, previous_right, parameter);
			const auto left = InterpolateStrokePoint(quadratic[0], quadratic[1], parameter);
			const auto right = InterpolateStrokePoint(quadratic[1], quadratic[2], parameter);
			const auto end = InterpolateStrokePoint(left, right, parameter);
			StrokeQuadratic current{previous_end, control, end};
			previous_left = left;
			previous_right = right;
			previous_end = end;
			for (std::size_t piece = 0;; ++piece) {
				if (piece == kMaximumStrokeCurvePieces) {
					throw std::runtime_error("CFF direct conic exceeds its bounded subdivision work");
				}
				const auto initial = ScaleStrokePoint(SubtractStrokePoints(current[1], current[0]), 2);
				const auto delta = SubtractStrokePoints(ScaleStrokePoint(SubtractStrokePoints(current[2], current[1]), 2), initial);
				const auto mixed = delta.Y * initial.Y + delta.X * initial.X;
				double split = 1;
				if (mixed != 0) {
					const auto cross = delta.X * initial.Y - delta.Y * initial.X;
					const auto squared_initial = initial.Y * initial.Y + initial.X * initial.X;
					const auto numerator = squared_initial * kStrokeAngleCondition;
					const auto denominator = std::abs(cross) - mixed * kStrokeAngleCondition;
					const auto split_parameter = numerator / denominator;
					if (split_parameter > 1.0000000000000001e-5 && split_parameter < .99999) {
						split = split_parameter;
					}
				}
				auto [offset_piece, rest] = SplitStrokeQuadratic(current, split);
				AppendOffsetQuadratic(result, offset_piece, radius, true);
				if (split == 1) {
					break;
				}
				current = rest;
			}
		}

		return result;
	}
} // namespace sgre::text
