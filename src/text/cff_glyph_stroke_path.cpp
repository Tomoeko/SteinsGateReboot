#include "sgre/text/cff_glyph_stroke_path.hpp"

#include "sgre/text/cff_glyph_stroke.hpp"
#include "sgre/text/cff_glyph_stroke_curve.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vector>

namespace sgre::text {
	static constexpr std::size_t kMaximumGlyphStrokeCommands = 1048576;
	static constexpr std::size_t kMaximumGlyphStrokeInputCommands = 32768;
	static constexpr double kCubicStrokeDistanceScale = .5625;
	static constexpr double kCubicStrokeFlatnessSquared = 1e-12;
	static constexpr double kCubicStrokeMinimumExtremum = 1e-10;
	static constexpr double kCubicStrokeMaximumExtremum = .99999;

	static CffGlyphPathPoint StrokePathEndpoint(const CffGlyphPathCommand& command) {
		switch (command.Kind) {
		case CffGlyphPathCommandKind::CubicTo:
			return command.Points[2];
		case CffGlyphPathCommandKind::QuadraticTo:
			return command.Points[1];
		case CffGlyphPathCommandKind::MoveTo:
		case CffGlyphPathCommandKind::LineTo:
			return command.Points[0];
		default:
			throw std::runtime_error("CFF stroke contour endpoint requires an actual point-bearing command");
		}
	}

	static void AppendStrokePathCommand(std::vector<CffGlyphPathCommand>& destination, CffGlyphPathCommand command) {
		if (destination.size() == kMaximumGlyphStrokeCommands) {
			throw std::runtime_error("CFF stroke output exceeds its bounded owned contour storage");
		}
		destination.push_back(command);
	}

	static void AppendStrokePathCommands(std::vector<CffGlyphPathCommand>& destination, const std::vector<CffGlyphPathCommand>& commands) {
		if (commands.size() > kMaximumGlyphStrokeCommands - destination.size()) {
			throw std::runtime_error("CFF stroke output exceeds its bounded owned contour storage");
		}
		destination.insert(destination.end(), commands.begin(), commands.end());
	}

	static CffGlyphPathCommand StrokePathPointCommand(CffGlyphPathCommandKind kind, CffGlyphPathPoint point) {
		CffGlyphPathCommand command;
		command.Kind = kind;
		command.Points[0] = point;

		return command;
	}

	static void BeginStrokeSides(StrokeContourSides& sides, CffGlyphPathPoint start, CffGlyphPathPoint normal, double radius) {
		const CffGlyphPathPoint offset{radius * normal.X, radius * normal.Y};
		sides.Left.push_back(StrokePathPointCommand(CffGlyphPathCommandKind::MoveTo, {start.X + offset.X, start.Y + offset.Y}));
		sides.Right.push_back(StrokePathPointCommand(CffGlyphPathCommandKind::MoveTo, {start.X - offset.X, start.Y - offset.Y}));
	}

	static void AppendReversedStrokeSide(std::vector<CffGlyphPathCommand>& destination, const std::vector<CffGlyphPathCommand>& side) {
		for (std::size_t index = side.size(); index > 1; --index) {
			const auto& source = side[index - 1];
			const auto target = StrokePathEndpoint(side[index - 2]);
			CffGlyphPathCommand reverse;
			reverse.Kind = source.Kind;
			switch (source.Kind) {
			case CffGlyphPathCommandKind::CubicTo:
				reverse.Points = {source.Points[1], source.Points[0], target};
				break;
			case CffGlyphPathCommandKind::QuadraticTo:
				reverse.Points[0] = source.Points[0];
				reverse.Points[1] = target;
				break;
			case CffGlyphPathCommandKind::LineTo:
				reverse.Points[0] = target;
				break;
			default:
				throw std::runtime_error("CFF stroke reverse side requires its native single open contour");
			}
			AppendStrokePathCommand(destination, reverse);
		}
	}

	static void FinishStrokeSides(std::vector<CffGlyphPathCommand>& output, StrokeContourSides& sides, CffGlyphPathPoint first_center, CffGlyphPathPoint last_center) {
		if (sides.Left.empty()) {
			return;
		}
		const auto first_left = StrokePathEndpoint(sides.Left.front());
		const auto first_right = StrokePathEndpoint(sides.Right.front());
		const auto last_left = StrokePathEndpoint(sides.Left.back());
		const auto last_right = StrokePathEndpoint(sides.Right.back());
		AppendStrokePathCommands(output, sides.Left);
		AppendStrokePathCommands(output, PrepareRoundStrokeCap(last_center, last_left, last_right));
		// Native49ABB0 consumes A forward, B in reverse and C forward without closing each piece.
		AppendReversedStrokeSide(output, sides.Right);
		AppendStrokePathCommands(output, PrepareRoundStrokeCap(first_center, first_right, first_left));
		sides.Left.clear();
		sides.Right.clear();
	}

	static void RequireStrokePath(std::span<const CffGlyphPathCommand> commands, double width) {
		if (!std::isfinite(width) || width <= 0 || width > 65534 || commands.size() > kMaximumGlyphStrokeInputCommands) {
			throw std::runtime_error("CFF glyph stroke requires its bounded native positive width and translated input");
		}
		if (!commands.empty() && commands.front().Kind != CffGlyphPathCommandKind::MoveTo) {
			throw std::runtime_error("CFF glyph stroke input must begin at its actual contour move");
		}
		for (const auto& command : commands) {
			if (command.Kind != CffGlyphPathCommandKind::MoveTo && command.Kind != CffGlyphPathCommandKind::LineTo && command.Kind != CffGlyphPathCommandKind::CubicTo && command.Kind != CffGlyphPathCommandKind::QuadraticTo) {
				throw std::runtime_error("CFF glyph stroke input requires its measured move/line/conic/cubic decomposition with native open flags");
			}
			const auto count = command.Kind == CffGlyphPathCommandKind::CubicTo ? std::size_t(3) : command.Kind == CffGlyphPathCommandKind::QuadraticTo ? std::size_t(2)
			                                                                                                                                            : std::size_t(1);
			for (std::size_t index = 0; index < count; ++index) {
				const auto point = command.Points[index];
				if (!std::isfinite(point.X) || !std::isfinite(point.Y) || std::abs(point.X) > 1048576 || std::abs(point.Y) > 1048576) {
					throw std::runtime_error("CFF glyph stroke input contains an unbounded or nonfinite translated point");
				}
			}
		}
	}

	struct StrokeCubicLineFallback {
		std::array<CffGlyphPathPoint, 3> Points{};
		std::size_t Count = 0;
	};

	static bool SameStrokePathPoint(CffGlyphPathPoint first, CffGlyphPathPoint second) {
		return std::bit_cast<std::uint64_t>(first.X) == std::bit_cast<std::uint64_t>(second.X) && std::bit_cast<std::uint64_t>(first.Y) == std::bit_cast<std::uint64_t>(second.Y);
	}

	static CffGlyphPathPoint EvaluateStrokeCubicExtremum(const std::array<CffGlyphPathPoint, 4>& cubic, double parameter) {
		CffGlyphPathPoint result;
		for (std::size_t axis = 0; axis < 2; ++axis) {
			const auto start = axis == 0 ? cubic[0].X : cubic[0].Y;
			const auto first = (axis == 0 ? cubic[1].X : cubic[1].Y) - start;
			const auto second = (axis == 0 ? cubic[2].X : cubic[2].Y) - (axis == 0 ? cubic[1].X : cubic[1].Y);
			const auto third = (axis == 0 ? cubic[3].X : cubic[3].Y) - (axis == 0 ? cubic[2].X : cubic[2].Y);
			const auto value = (((((third - second) - second) + first) * parameter + (second - first) * 3) * parameter + first * 3) * parameter + start;
			if (axis == 0) {
				result.X = value;
			} else {
				result.Y = value;
			}
		}

		return result;
	}

	static std::optional<StrokeCubicLineFallback> PrepareStrokeCubicLineFallback(const std::array<CffGlyphPathPoint, 4>& cubic) {
		const auto& start = cubic[0];
		const auto& first = cubic[1];
		const auto& second = cubic[2];
		const auto& end = cubic[3];
		double squared_chord;
		double squared_distance;
		if (SameStrokePathPoint(start, end)) {
			const auto delta_x = second.X - first.X;
			const auto delta_y = second.Y - first.Y;
			const auto cross = (first.Y - start.Y) * delta_x - (first.X - start.X) * delta_y;
			squared_chord = delta_x * delta_x + delta_y * delta_y;
			squared_distance = (cross * kCubicStrokeDistanceScale) * cross;
		} else {
			const auto delta_x = end.X - start.X;
			const auto delta_y = end.Y - start.Y;
			const auto first_cross = (first.Y - start.Y) * delta_x - (first.X - start.X) * delta_y;
			const auto second_cross = (second.Y - start.Y) * delta_x - (second.X - start.X) * delta_y;
			squared_chord = delta_x * delta_x + delta_y * delta_y;
			squared_distance = std::max(second_cross * second_cross, first_cross * first_cross) * kCubicStrokeDistanceScale;
		}
		if (!(squared_chord * kCubicStrokeFlatnessSquared >= squared_distance)) {
			return std::nullopt;
		}

		StrokeCubicLineFallback result;
		const auto first_projection = (start.Y - first.Y) * (end.Y - first.Y) + (start.X - first.X) * (end.X - first.X);
		const auto second_projection = (start.Y - second.Y) * (end.Y - second.Y) + (start.X - second.X) * (end.X - second.X);
		if (!(first_projection < 0 && second_projection < 0)) {
			auto guide = CffGlyphPathPoint{first.X - start.X, first.Y - start.Y};
			if (guide.X == 0 && guide.Y == 0) {
				guide = {second.X - start.X, second.Y - start.Y};
			}
			if (guide.X == 0 && guide.Y == 0) {
				guide = {end.X - start.X, end.Y - start.Y};
			}
			const auto first_dot = (first.Y - start.Y) * guide.Y + guide.X * (first.X - start.X);
			const auto second_dot = (second.X - start.X) * guide.X + (second.Y - start.Y) * guide.Y;
			const auto end_dot = (end.X - start.X) * guide.X + (end.Y - start.Y) * guide.Y;
			const auto denominator = (first_dot * 3 - second_dot * 3) + end_dot;
			const auto discriminant = (second_dot - first_dot) * second_dot - (end_dot - first_dot) * first_dot;
			// Native zero-denominator and negative-radicand roots are unordered/outside the accepted interval.
			if (denominator != 0 && discriminant >= 0) {
				const auto root = std::sqrt(discriminant);
				const auto numerator = (first_dot + first_dot) - second_dot;
				const auto reciprocal = 1.0 / denominator;
				const std::array<double, 2> parameters{{(numerator - root) * reciprocal, (numerator + root) * reciprocal}};
				for (const auto parameter : parameters) {
					if (parameter > kCubicStrokeMinimumExtremum && parameter < kCubicStrokeMaximumExtremum) {
						result.Points[result.Count++] = EvaluateStrokeCubicExtremum(cubic, parameter);
					}
				}
			}
		}
		result.Points[result.Count++] = end;

		return result;
	}

	static std::optional<StrokeCubicLineFallback> PrepareStrokeConicLineFallback(const std::array<CffGlyphPathPoint, 3>& conic) {
		const CffGlyphPathPoint first{conic[1].X - conic[0].X, conic[1].Y - conic[0].Y};
		const CffGlyphPathPoint second{conic[2].X - conic[1].X, conic[2].Y - conic[1].Y};
		const auto cross = second.X * first.Y - second.Y * first.X;
		if (std::abs(cross) > 1e-10) {
			return std::nullopt;
		}
		StrokeCubicLineFallback result;
		const auto opposite = -first.Y * second.Y - second.X * first.X;
		if (opposite > 0) {
			const auto squared_first = first.X * first.X + first.Y * first.Y;
			const auto chord_projection = (conic[2].Y - conic[0].Y) * first.Y + (conic[2].X - conic[0].X) * first.X;
			const auto parameter = squared_first / ((squared_first + squared_first) - chord_projection);
			if (parameter > 0 && parameter < 1) {
				const auto x = ((second.X - first.X) * parameter + (first.X + first.X)) * parameter + conic[0].X;
				const auto y = ((second.Y - first.Y) * parameter + (first.Y + first.Y)) * parameter + conic[0].Y;
				result.Points[result.Count++] = {x, y};
			}
		}
		result.Points[result.Count++] = conic[2];

		return result;
	}

	static bool AppendStrokePathLine(StrokeContourSides& sides, CffGlyphPathPoint& current, CffGlyphPathPoint& normal, CffGlyphPathPoint end, double width_pixels) {
		const auto next_normal = PrepareStrokeLineNormal(current, end);
		if (!next_normal) {
			return false;
		}
		if (sides.Left.empty()) {
			const auto radius = width_pixels * .5;
			BeginStrokeSides(sides, current, *next_normal, radius);
			const CffGlyphPathPoint offset{radius * next_normal->X, radius * next_normal->Y};
			AppendStrokePathCommand(sides.Left, StrokePathPointCommand(CffGlyphPathCommandKind::LineTo, {end.X + offset.X, end.Y + offset.Y}));
			AppendStrokePathCommand(sides.Right, StrokePathPointCommand(CffGlyphPathCommandKind::LineTo, {end.X - offset.X, end.Y - offset.Y}));
		} else {
			static_cast<void>(AppendStrokeLineSegment(sides, current, end, normal, width_pixels));
		}
		normal = *next_normal;
		current = end;

		return true;
	}

	static void AppendStrokeCubicCuspLine(StrokeContourSides& sides, CffGlyphPathPoint& current, CffGlyphPathPoint& normal, CffGlyphPathPoint end, double width_pixels) {
		const auto next_normal = PrepareStrokeLineNormal(current, end);
		if (!next_normal) {
			return;
		}
		if (sides.Left.empty()) {
			AppendStrokePathLine(sides, current, normal, end, width_pixels);
			return;
		}
		const auto cross = normal.X * next_normal->Y - normal.Y * next_normal->X;
		const auto right_is_outer = cross >= 0;
		auto& outer = right_is_outer ? sides.Right : sides.Left;
		auto& inner = right_is_outer ? sides.Left : sides.Right;
		const auto signed_radius = right_is_outer ? -width_pixels * .5 : width_pixels * .5;
		const auto signed_width = right_is_outer ? -width_pixels : width_pixels;
		const CffGlyphPathPoint sum{normal.X + next_normal->X, normal.Y + next_normal->Y};
		const auto squared_sum = sum.Y * sum.Y + sum.X * sum.X;
		const CffGlyphPathPoint offset{signed_radius * next_normal->X, signed_radius * next_normal->Y};
		StrokeRoundJoinInput round;
		round.Center = current;
		round.Start = StrokePathEndpoint(outer.back());
		round.End = {current.X + offset.X, current.Y + offset.Y};
		round.IncomingNormal = normal;
		round.IntersectionOffset = {signed_width * sum.X / squared_sum, signed_width * sum.Y / squared_sum};
		round.SignedRadius = signed_radius;
		round.SignedWidth = signed_width;
		AppendStrokePathCommands(outer, PrepareRoundStrokeJoin(round));
		AppendStrokePathCommand(outer, StrokePathPointCommand(CffGlyphPathCommandKind::LineTo, {end.X + offset.X, end.Y + offset.Y}));
		// Native432C30 emits the old opposite offset directly, then the new endpoint; no inner center line.
		AppendStrokePathCommand(inner, StrokePathPointCommand(CffGlyphPathCommandKind::LineTo, {current.X - offset.X, current.Y - offset.Y}));
		AppendStrokePathCommand(inner, StrokePathPointCommand(CffGlyphPathCommandKind::LineTo, {end.X - offset.X, end.Y - offset.Y}));
		normal = *next_normal;
		current = end;
	}

	std::vector<CffGlyphPathCommand> PrepareCffGlyphStroke(std::span<const CffGlyphPathCommand> translated_commands, double width_pixels) {
		RequireStrokePath(translated_commands, width_pixels);
		std::vector<CffGlyphPathCommand> output;
		StrokeContourSides sides;
		CffGlyphPathPoint first_center;
		CffGlyphPathPoint current;
		CffGlyphPathPoint normal;
		const auto radius = width_pixels * 0.5;
		for (const auto& command : translated_commands) {
			if (command.Kind == CffGlyphPathCommandKind::MoveTo) {
				FinishStrokeSides(output, sides, first_center, current);
				first_center = command.Points[0];
				current = first_center;
				continue;
			}
			if (command.Kind == CffGlyphPathCommandKind::LineTo) {
				AppendStrokePathLine(sides, current, normal, command.Points[0], width_pixels);
				continue;
			}
			if (command.Kind == CffGlyphPathCommandKind::QuadraticTo) {
				const std::array<CffGlyphPathPoint, 3> conic{current, command.Points[0], command.Points[1]};
				if (const auto fallback = PrepareStrokeConicLineFallback(conic)) {
					for (std::size_t index = 0; index < fallback->Count; ++index) {
						if (index == 0) {
							if (!AppendStrokePathLine(sides, current, normal, fallback->Points[index], width_pixels)) {
								break;
							}
						} else {
							AppendStrokeCubicCuspLine(sides, current, normal, fallback->Points[index], width_pixels);
						}
					}
					continue;
				}
				const CffGlyphPathPoint first_tangent{conic[1].X - conic[0].X, conic[1].Y - conic[0].Y};
				const CffGlyphPathPoint last_tangent{conic[2].X - conic[1].X, conic[2].Y - conic[1].Y};
				if (first_tangent.X * first_tangent.X + first_tangent.Y * first_tangent.Y < 1.0000000000000001e-20 || last_tangent.X * last_tangent.X + last_tangent.Y * last_tangent.Y < 1.0000000000000001e-20) {
					current = command.Points[1];
					continue;
				}
				const auto curve = PrepareCffGlyphStrokeQuadratic(conic, width_pixels);
				if (curve.Left.empty() || curve.Right.empty()) {
					throw std::runtime_error("CFF direct conic stroke has no measured nonflat offset pieces");
				}
				if (sides.Left.empty()) {
					BeginStrokeSides(sides, current, curve.StartNormal, radius);
				} else {
					StrokeSegmentJoinInput join;
					join.Center = current;
					join.NextTangentEnd = command.Points[0];
					join.IncomingNormal = normal;
					join.OutgoingNormal = curve.StartNormal;
					join.Width = width_pixels;
					AppendStrokeSegmentJoin(sides, join);
				}
				AppendStrokePathCommands(sides.Left, curve.Left);
				AppendStrokePathCommands(sides.Right, curve.Right);
				normal = curve.EndNormal;
				current = command.Points[1];
				continue;
			}
			const std::array<CffGlyphPathPoint, 4> cubic{current, command.Points[0], command.Points[1], command.Points[2]};
			if (const auto fallback = PrepareStrokeCubicLineFallback(cubic)) {
				for (std::size_t index = 0; index < fallback->Count; ++index) {
					if (index == 0) {
						if (!AppendStrokePathLine(sides, current, normal, fallback->Points[index], width_pixels)) {
							break;
						}
					} else {
						AppendStrokeCubicCuspLine(sides, current, normal, fallback->Points[index], width_pixels);
					}
				}
				continue;
			}
			auto curve = PrepareCffGlyphStrokeCurve(cubic, width_pixels);
			if (curve.Left.empty() || curve.Right.empty()) {
				throw std::runtime_error("CFF cubic stroke has no measured nonflat offset pieces");
			}
			if (sides.Left.empty()) {
				BeginStrokeSides(sides, current, curve.StartNormal, radius);
			} else {
				StrokeSegmentJoinInput join;
				join.Center = current;
				join.NextTangentEnd = command.Points[0];
				join.IncomingNormal = normal;
				join.OutgoingNormal = curve.StartNormal;
				join.Width = width_pixels;
				AppendStrokeSegmentJoin(sides, join);
			}
			AppendStrokePathCommands(sides.Left, curve.Left);
			AppendStrokePathCommands(sides.Right, curve.Right);
			normal = curve.EndNormal;
			current = command.Points[2];
		}
		FinishStrokeSides(output, sides, first_center, current);

		return output;
	}
} // namespace sgre::text
