#include "sgre/text/cff_glyph_path.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace sgre::text {

	static constexpr std::size_t kMaxOutlinePoints = 32767;

	static void ValidateContour(std::span<const CffScaledPoint> points, std::size_t start, std::size_t end) {
		if (points[start].Kind != resource::CffPointKind::OnCurve) {
			throw std::runtime_error("CFF glyph path contour must start on curve");
		}

		std::size_t index = start + 1;
		while (index <= end) {
			if (points[index].Kind == resource::CffPointKind::OnCurve) {
				++index;
				continue;
			}
			if (points[index].Kind != resource::CffPointKind::CubicControl || index + 1 > end || points[index + 1].Kind != resource::CffPointKind::CubicControl) {
				throw std::runtime_error("CFF glyph path requires paired cubic control points");
			}
			if (index + 2 <= end && points[index + 2].Kind != resource::CffPointKind::OnCurve) {
				throw std::runtime_error("CFF glyph path cubic endpoint must be on curve");
			}

			index += 3;
		}
	}

	static void ValidateGlyph(std::span<const CffScaledPoint> points, std::span<const std::uint16_t> contour_ends) {
		if (points.size() > kMaxOutlinePoints || contour_ends.size() > points.size()) {
			throw std::runtime_error("CFF glyph path outline exceeds native point or contour counts");
		}
		if (points.empty() != contour_ends.empty()) {
			throw std::runtime_error("CFF glyph path point and contour storage disagree");
		}

		std::size_t start = 0;
		for (const auto end : contour_ends) {
			if (end < start || end >= points.size()) {
				throw std::runtime_error("CFF glyph path contour endpoint is out of order or range");
			}

			ValidateContour(points, start, end);
			start = std::size_t(end) + 1;
		}
		if (start != points.size()) {
			throw std::runtime_error("CFF glyph path final contour does not cover its points");
		}
		for (const auto& point : points) {
			if (point.Y26Dot6 == std::numeric_limits<std::int32_t>::min()) {
				throw std::runtime_error("CFF glyph path drawing-axis negation exceeds the native signed range");
			}
		}
	}

	static CffGlyphPathPoint PixelPoint(const CffScaledPoint& point) {
		CffGlyphPathPoint pixel;
		pixel.X = double(point.X26Dot6) / 64;
		pixel.Y = double(-point.Y26Dot6) / 64;

		return pixel;
	}

	static void AppendPointCommand(CffGlyphPath& path, CffGlyphPathCommandKind kind, const CffScaledPoint& point) {
		CffGlyphPathCommand command;
		command.Kind = kind;
		command.Points[0] = PixelPoint(point);
		path.Commands.push_back(command);
	}

	static void AppendCubic(CffGlyphPath& path, const CffScaledPoint& first, const CffScaledPoint& second, const CffScaledPoint& end) {
		CffGlyphPathCommand command;
		command.Kind = CffGlyphPathCommandKind::CubicTo;
		command.Points[0] = PixelPoint(first);
		command.Points[1] = PixelPoint(second);
		command.Points[2] = PixelPoint(end);
		path.Commands.push_back(command);
	}

	static void AppendContour(CffGlyphPath& path, std::span<const CffScaledPoint> points, std::size_t start, std::size_t end) {
		AppendPointCommand(path, CffGlyphPathCommandKind::MoveTo, points[start]);
		std::size_t index = start + 1;
		bool closed_by_cubic = false;
		while (index <= end) {
			if (points[index].Kind == resource::CffPointKind::OnCurve) {
				AppendPointCommand(path, CffGlyphPathCommandKind::LineTo, points[index]);
				++index;
				continue;
			}

			closed_by_cubic = index + 2 > end;
			const auto endpoint = closed_by_cubic ? start : index + 2;
			AppendCubic(path, points[index], points[index + 1], points[endpoint]);
			index += 3;
		}
		if (!closed_by_cubic) {
			AppendPointCommand(path, CffGlyphPathCommandKind::LineTo, points[start]);
		}
	}

	static void IncludePoint(CffGlyphPathBounds& bounds, const CffGlyphPathPoint& point) {
		bounds.Left = std::min(bounds.Left, point.X);
		bounds.Top = std::min(bounds.Top, point.Y);
		bounds.Right = std::max(bounds.Right, point.X);
		bounds.Bottom = std::max(bounds.Bottom, point.Y);
	}

	static double ClampRoot(double root) {
		// Native MAXSD with zero maps NaN roots from constant/linear axes to zero.
		if (!(root > 0)) {
			return 0;
		}

		return root < 1 ? root : 1;
	}

	static std::array<double, 2> DerivativeRoots(double start, double first, double second, double end) {
		const auto first_delta = first - start;
		const auto second_delta = second - first;
		const auto third_delta = end - second;
		const auto a = ((third_delta - second_delta) - second_delta + first_delta) * 3;
		const auto b = (second_delta - first_delta) * 6;
		const auto c = first_delta * 3;
		const auto discriminant = std::max(0.0, b * b - (a * 4) * c);
		const auto q = (b + std::copysign(std::sqrt(discriminant), b)) * -0.5;

		return {ClampRoot(q / a), ClampRoot(c / q)};
	}

	static double Interpolate(double first, double second, double amount) {
		return (first - first * amount) + second * amount;
	}

	static double EvaluateCubic(double start, double first, double second, double end, double amount) {
		const auto a = Interpolate(start, first, amount);
		const auto b = Interpolate(first, second, amount);
		const auto c = Interpolate(second, end, amount);
		const auto d = Interpolate(a, b, amount);
		const auto e = Interpolate(b, c, amount);

		return Interpolate(d, e, amount);
	}

	static void IncludeCubicExtrema(CffGlyphPathBounds& bounds, const CffGlyphPathPoint& start, const CffGlyphPathCommand& command) {
		const auto& first = command.Points[0];
		const auto& second = command.Points[1];
		const auto& end = command.Points[2];
		IncludePoint(bounds, end);
		if (std::min(first.X, second.X) >= bounds.Left && std::min(first.Y, second.Y) >= bounds.Top && std::max(first.X, second.X) <= bounds.Right && std::max(first.Y, second.Y) <= bounds.Bottom) {
			return;
		}

		for (const auto root : DerivativeRoots(start.X, first.X, second.X, end.X)) {
			const auto value = EvaluateCubic(start.X, first.X, second.X, end.X, root);
			bounds.Left = std::min(bounds.Left, value);
			bounds.Right = std::max(bounds.Right, value);
		}
		for (const auto root : DerivativeRoots(start.Y, first.Y, second.Y, end.Y)) {
			const auto value = EvaluateCubic(start.Y, first.Y, second.Y, end.Y, root);
			bounds.Top = std::min(bounds.Top, value);
			bounds.Bottom = std::max(bounds.Bottom, value);
		}
	}

	static CffGlyphPathBounds MeasurePath(const CffGlyphPath& path) {
		CffGlyphPathBounds bounds;
		if (path.Commands.empty()) {
			return bounds;
		}

		CffGlyphPathPoint current = path.Commands.front().Points[0];
		bounds.Left = bounds.Right = current.X;
		bounds.Top = bounds.Bottom = current.Y;
		for (const auto& command : path.Commands) {
			if (command.Kind == CffGlyphPathCommandKind::CubicTo) {
				IncludeCubicExtrema(bounds, current, command);
				current = command.Points[2];
			} else {
				IncludePoint(bounds, command.Points[0]);
				current = command.Points[0];
			}
		}

		return bounds;
	}

	CffGlyphPath PrepareCffGlyphPath(std::span<const CffScaledPoint> points, std::span<const std::uint16_t> contour_ends) {
		ValidateGlyph(points, contour_ends);

		CffGlyphPath result;
		result.Commands.reserve(points.size() + contour_ends.size());
		std::size_t start = 0;
		for (const auto end : contour_ends) {
			AppendContour(result, points, start, end);
			start = std::size_t(end) + 1;
		}
		result.Bounds = MeasurePath(result);

		return result;
	}

	CffGlyphPath PrepareCffGlyphPath(const CffScaledGlyph& glyph) {
		return PrepareCffGlyphPath(glyph.Points, glyph.ContourEnds);
	}

	static std::int32_t CheckedPixel(double pixel) {
		if (pixel < std::numeric_limits<std::int32_t>::min() || pixel > std::numeric_limits<std::int32_t>::max()) {
			throw std::runtime_error("CFF glyph bitmap bound exceeds the native signed pixel range");
		}

		return static_cast<std::int32_t>(pixel);
	}

	static std::int32_t CheckedPlanInteger(std::int64_t value) {
		if (value < std::numeric_limits<std::int32_t>::min() || value > std::numeric_limits<std::int32_t>::max()) {
			throw std::runtime_error("CFF glyph bitmap extent or translation exceeds the native signed pixel range");
		}

		return static_cast<std::int32_t>(value);
	}

	std::optional<CffGlyphBitmapPlan> PlanCffGlyphBitmap(const CffGlyphPathBounds& bounds, std::int32_t border_width_pixels) {
		if (!std::isfinite(bounds.Left) || !std::isfinite(bounds.Top) || !std::isfinite(bounds.Right) || !std::isfinite(bounds.Bottom) || bounds.Left > bounds.Right || bounds.Top > bounds.Bottom) {
			throw std::runtime_error("CFF glyph bitmap requires finite ordered path bounds");
		}
		if (border_width_pixels < 0) {
			throw std::runtime_error("CFF glyph bitmap border width must be nonnegative");
		}

		const auto left = CheckedPixel(std::floor(bounds.Left));
		const auto top = CheckedPixel(std::floor(bounds.Top));
		const auto right = CheckedPixel(std::ceil(bounds.Right));
		const auto bottom = CheckedPixel(std::ceil(bounds.Bottom));
		const auto width = std::int64_t(right) - left;
		const auto height = std::int64_t(bottom) - top;
		if (width <= 0 || height <= 0) {
			return std::nullopt;
		}

		CffGlyphBitmapPlan plan;
		plan.LeftPixel = CheckedPlanInteger(std::int64_t(left) - border_width_pixels);
		plan.TopPixel = CheckedPlanInteger(std::int64_t(top) - border_width_pixels);
		plan.WidthPixels = CheckedPlanInteger(width + std::int64_t(border_width_pixels) * 2);
		plan.HeightPixels = CheckedPlanInteger(height + std::int64_t(border_width_pixels) * 2);
		plan.PathTranslationPixels.X = CheckedPlanInteger(-std::int64_t(plan.LeftPixel));
		plan.PathTranslationPixels.Y = CheckedPlanInteger(-std::int64_t(plan.TopPixel));

		return plan;
	}

} // namespace sgre::text
