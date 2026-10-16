#include "sgre/text/cff_glyph_stroke.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace sgre::text {

	static constexpr double kStrokeSquaredEpsilon = 1e-20;
	static constexpr double kRoundCapWeight = 0.55228474983;
	static constexpr std::size_t kMaximumStrokeCommands = 1'000'000;

	static void RequireStrokePoint(CffGlyphPathPoint point) {
		if (!std::isfinite(point.X) || !std::isfinite(point.Y)) {
			throw std::runtime_error("glyph stroke requires finite owned path coordinates");
		}
	}

	static void RequireStrokeWidth(double width) {
		if (!std::isfinite(width) || width <= 0) {
			throw std::runtime_error("glyph stroke requires its positive finite native width");
		}
	}

	static CffGlyphPathPoint StrokeEndpoint(const CffGlyphPathCommand& command) {
		if (command.Kind == CffGlyphPathCommandKind::Close) {
			throw std::runtime_error("glyph stroke side has no endpoint after Close");
		}
		if (command.Kind == CffGlyphPathCommandKind::CubicTo) {
			return command.Points[2];
		}
		if (command.Kind == CffGlyphPathCommandKind::QuadraticTo) {
			return command.Points[1];
		}

		if (command.Kind != CffGlyphPathCommandKind::MoveTo && command.Kind != CffGlyphPathCommandKind::LineTo) {
			throw std::runtime_error("glyph stroke side has an unsupported command kind");
		}

		return command.Points[0];
	}

	static bool SameStrokeNormal(CffGlyphPathPoint first, CffGlyphPathPoint second) {
		return std::bit_cast<std::uint64_t>(first.X) == std::bit_cast<std::uint64_t>(second.X) && std::bit_cast<std::uint64_t>(first.Y) == std::bit_cast<std::uint64_t>(second.Y);
	}

	static void RequireStrokeSide(const std::vector<CffGlyphPathCommand>& side) {
		if (side.empty() || side.size() > kMaximumStrokeCommands - 5) {
			throw std::runtime_error("glyph stroke join requires its bounded nonempty actual side owner");
		}
		RequireStrokePoint(StrokeEndpoint(side.back()));
		if (side.size() > 1) {
			RequireStrokePoint(StrokeEndpoint(side[side.size() - 2]));
		}
	}

	static void AppendStrokeLine(std::vector<CffGlyphPathCommand>& side, CffGlyphPathPoint point) {
		RequireStrokePoint(point);
		CffGlyphPathCommand command;
		command.Kind = CffGlyphPathCommandKind::LineTo;
		command.Points[0] = point;
		side.push_back(command);
	}

	static CffGlyphPathCommand StrokeCubic(CffGlyphPathPoint start, CffGlyphPathPoint end, CffGlyphPathPoint intersection, double weight) {
		CffGlyphPathCommand command;
		command.Kind = CffGlyphPathCommandKind::CubicTo;
		command.Points[0] = {(intersection.X - start.X) * weight + start.X, (intersection.Y - start.Y) * weight + start.Y};
		command.Points[1] = {(intersection.X - end.X) * weight + end.X, (intersection.Y - end.Y) * weight + end.Y};
		command.Points[2] = end;
		for (const auto point : command.Points) {
			RequireStrokePoint(point);
		}

		return command;
	}

	static double RoundJoinWeight(CffGlyphPathPoint center, CffGlyphPathPoint midpoint, CffGlyphPathPoint intersection) {
		const auto first_x = center.X - midpoint.X;
		const auto first_y = center.Y - midpoint.Y;
		const auto second_x = center.X - intersection.X;
		const auto second_y = center.Y - intersection.Y;
		const auto first_length = std::sqrt(first_y * first_y + first_x * first_x);
		const auto second_length = std::sqrt(second_y * second_y + second_x * second_x);
		const auto cosine = std::sqrt(first_length / second_length);
		const auto weight = (cosine * 4.0) / ((cosine + 1.0) * 3.0);
		if (!std::isfinite(weight)) {
			throw std::runtime_error("glyph round join exceeds its finite tangent-intersection domain");
		}

		return weight;
	}

	std::optional<CffGlyphPathPoint> PrepareStrokeLineNormal(CffGlyphPathPoint start, CffGlyphPathPoint end) {
		RequireStrokePoint(start);
		RequireStrokePoint(end);
		const auto delta_x = end.X - start.X;
		const auto delta_y = end.Y - start.Y;
		const auto squared = delta_x * delta_x + delta_y * delta_y;
		if (squared < kStrokeSquaredEpsilon) {
			return std::nullopt;
		}
		const auto length = std::sqrt(squared);
		const CffGlyphPathPoint normal{-(delta_y / length), delta_x / length};
		RequireStrokePoint(normal);
		if (!std::isfinite(squared)) {
			throw std::runtime_error("glyph stroke line length exceeds its finite native domain");
		}

		return normal;
	}

	std::vector<CffGlyphPathCommand> PrepareRoundStrokeJoin(const StrokeRoundJoinInput& input) {
		RequireStrokePoint(input.Center);
		RequireStrokePoint(input.Start);
		RequireStrokePoint(input.End);
		RequireStrokePoint(input.IncomingNormal);
		if (!std::isfinite(input.SignedRadius) || !std::isfinite(input.SignedWidth) || input.SignedRadius == 0 || input.SignedWidth == 0) {
			throw std::runtime_error("glyph round join requires its actual signed width and radius");
		}
		const auto dot = (input.Center.Y - input.End.Y) * (input.Center.Y - input.Start.Y) + (input.Center.X - input.End.X) * (input.Center.X - input.Start.X);
		if (!std::isfinite(dot)) {
			throw std::runtime_error("glyph round join radial comparison exceeds its finite domain");
		}
		if (dot >= 0) {
			RequireStrokePoint(input.IntersectionOffset);
			const CffGlyphPathPoint midpoint{input.Start.X * .5 + input.End.X * .5, input.Start.Y * .5 + input.End.Y * .5};
			const CffGlyphPathPoint intersection{input.Center.X + input.IntersectionOffset.X, input.Center.Y + input.IntersectionOffset.Y};
			return {StrokeCubic(input.Start, input.End, intersection, RoundJoinWeight(input.Center, midpoint, intersection))};
		}

		const auto delta_x = input.End.X - input.Start.X;
		const auto delta_y = input.End.Y - input.Start.Y;
		const auto length = std::sqrt(delta_x * delta_x + delta_y * delta_y);
		const CffGlyphPathPoint normal{-(delta_y / length), delta_x / length};
		const CffGlyphPathPoint sum{input.IncomingNormal.X + normal.X, input.IncomingNormal.Y + normal.Y};
		const auto squared_sum = sum.Y * sum.Y + sum.X * sum.X;
		const CffGlyphPathPoint middle{normal.X * input.SignedRadius + input.Center.X, input.SignedRadius * normal.Y + input.Center.Y};
		const CffGlyphPathPoint first{input.SignedWidth * sum.X / squared_sum + input.Center.X, input.SignedWidth * sum.Y / squared_sum + input.Center.Y};
		// Preserve the original two additions/subtraction rather than reassociating its reflected tangent.
		const CffGlyphPathPoint second{first.X - (first.X + first.X) + (middle.X + middle.X), first.Y - (first.Y + first.Y) + (middle.Y + middle.Y)};
		const CffGlyphPathPoint first_midpoint{(input.Start.X + middle.X) * .5, (input.Start.Y + middle.Y) * .5};
		const CffGlyphPathPoint second_midpoint{(input.End.X + middle.X) * .5, (input.End.Y + middle.Y) * .5};

		return {StrokeCubic(input.Start, middle, first, RoundJoinWeight(input.Center, first_midpoint, first)), StrokeCubic(middle, input.End, second, RoundJoinWeight(input.Center, second_midpoint, second))};
	}

	std::vector<CffGlyphPathCommand> PrepareRoundStrokeCap(CffGlyphPathPoint center, CffGlyphPathPoint start, CffGlyphPathPoint end) {
		RequireStrokePoint(center);
		RequireStrokePoint(start);
		RequireStrokePoint(end);
		const CffGlyphPathPoint tangent{(-(end.Y - start.Y)) * .5, (end.X - start.X) * .5};
		const CffGlyphPathPoint middle{center.X + tangent.X, tangent.Y + center.Y};
		const CffGlyphPathPoint first{tangent.X + start.X, tangent.Y + start.Y};
		const CffGlyphPathPoint second{tangent.X + end.X, tangent.Y + end.Y};

		return {StrokeCubic(start, middle, first, kRoundCapWeight), StrokeCubic(middle, end, second, kRoundCapWeight)};
	}

	static bool IntersectionWithinStrokeSegments(CffGlyphPathPoint intersection, CffGlyphPathPoint old_start, CffGlyphPathPoint old_end, CffGlyphPathPoint next_start, CffGlyphPathPoint next_end) {
		const auto minimum_x = std::max(std::min(next_start.X, next_end.X), std::min(old_start.X, old_end.X));
		const auto minimum_y = std::max(std::min(next_start.Y, next_end.Y), std::min(old_start.Y, old_end.Y));
		const auto maximum_x = std::min(std::max(next_start.X, next_end.X), std::max(old_start.X, old_end.X));
		const auto maximum_y = std::min(std::max(next_start.Y, next_end.Y), std::max(old_start.Y, old_end.Y));

		return intersection.X >= minimum_x && intersection.Y >= minimum_y && maximum_x >= intersection.X && maximum_y >= intersection.Y;
	}

	static void AppendStrokeJoin(StrokeContourSides& sides, const StrokeSegmentJoinInput& input, bool line_continuation) {
		RequireStrokeSide(sides.Left);
		RequireStrokeSide(sides.Right);
		RequireStrokeWidth(input.Width);
		RequireStrokePoint(input.Center);
		RequireStrokePoint(input.NextTangentEnd);
		RequireStrokePoint(input.IncomingNormal);
		RequireStrokePoint(input.OutgoingNormal);
		if (SameStrokeNormal(input.IncomingNormal, input.OutgoingNormal)) {
			return;
		}
		const auto cross = input.IncomingNormal.X * input.OutgoingNormal.Y - input.IncomingNormal.Y * input.OutgoingNormal.X;
		const auto right_is_outer = cross >= 0;
		auto& outer = right_is_outer ? sides.Right : sides.Left;
		auto& inner = right_is_outer ? sides.Left : sides.Right;
		const auto radius = input.Width * .5;
		const auto signed_radius = right_is_outer ? -radius : radius;
		const auto signed_width = right_is_outer ? -input.Width : input.Width;
		const CffGlyphPathPoint sum{input.IncomingNormal.X + input.OutgoingNormal.X, input.IncomingNormal.Y + input.OutgoingNormal.Y};
		const auto squared_sum = sum.Y * sum.Y + sum.X * sum.X;
		// Exact reversal has no finite intersection; only the split arc and center fallback consume it.
		const CffGlyphPathPoint intersection_offset{signed_width * sum.X / squared_sum, signed_width * sum.Y / squared_sum};
		const CffGlyphPathPoint offset{signed_radius * input.OutgoingNormal.X, signed_radius * input.OutgoingNormal.Y};
		const CffGlyphPathPoint outer_start{input.Center.X + offset.X, input.Center.Y + offset.Y};
		if (intersection_offset.Y * intersection_offset.Y + intersection_offset.X * intersection_offset.X <= kStrokeSquaredEpsilon) {
			if (outer.size() > 1 && outer.back().Kind == CffGlyphPathCommandKind::LineTo) {
				outer.pop_back();
			}
			AppendStrokeLine(outer, {input.Center.X + intersection_offset.X, input.Center.Y + intersection_offset.Y});
			AppendStrokeLine(outer, outer_start);
		} else {
			StrokeRoundJoinInput round;
			round.Center = input.Center;
			round.Start = StrokeEndpoint(outer.back());
			round.End = outer_start;
			round.IncomingNormal = input.IncomingNormal;
			round.IntersectionOffset = intersection_offset;
			round.SignedRadius = signed_radius;
			round.SignedWidth = signed_width;
			const auto commands = PrepareRoundStrokeJoin(round);
			outer.insert(outer.end(), commands.begin(), commands.end());
		}

		const CffGlyphPathPoint intersection{input.Center.X - intersection_offset.X, input.Center.Y - intersection_offset.Y};
		const CffGlyphPathPoint inner_start{input.Center.X - offset.X, input.Center.Y - offset.Y};
		const CffGlyphPathPoint inner_end{input.NextTangentEnd.X - offset.X, input.NextTangentEnd.Y - offset.Y};
		if (line_continuation && inner.size() > 1 && inner.back().Kind == CffGlyphPathCommandKind::LineTo && IntersectionWithinStrokeSegments(intersection, StrokeEndpoint(inner[inner.size() - 2]), StrokeEndpoint(inner.back()), inner_start, inner_end)) {
			inner.back().Points[0] = intersection;
		} else {
			AppendStrokeLine(inner, input.Center);
			AppendStrokeLine(inner, inner_start);
		}
	}

	void AppendStrokeSegmentJoin(StrokeContourSides& sides, const StrokeSegmentJoinInput& input) {
		AppendStrokeJoin(sides, input, false);
	}

	bool AppendStrokeLineSegment(StrokeContourSides& sides, CffGlyphPathPoint center, CffGlyphPathPoint end, CffGlyphPathPoint incoming_normal, double width) {
		RequireStrokeSide(sides.Left);
		RequireStrokeSide(sides.Right);
		RequireStrokeWidth(width);
		RequireStrokePoint(incoming_normal);
		const auto normal = PrepareStrokeLineNormal(center, end);
		if (!normal) {
			return false;
		}
		if (SameStrokeNormal(incoming_normal, *normal)) {
			for (auto* side : {&sides.Left, &sides.Right}) {
				if (side->size() > 1 && side->back().Kind == CffGlyphPathCommandKind::LineTo) {
					side->pop_back();
				}
			}
		} else {
			StrokeSegmentJoinInput join;
			join.Center = center;
			join.NextTangentEnd = end;
			join.IncomingNormal = incoming_normal;
			join.OutgoingNormal = *normal;
			join.Width = width;
			AppendStrokeJoin(sides, join, true);
		}
		const auto radius = width * .5;
		const CffGlyphPathPoint offset{radius * normal->X, radius * normal->Y};
		AppendStrokeLine(sides.Left, {end.X + offset.X, end.Y + offset.Y});
		AppendStrokeLine(sides.Right, {end.X - offset.X, end.Y - offset.Y});

		return true;
	}

} // namespace sgre::text
