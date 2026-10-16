#include "sgre/text/truetype_glyph_path.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>

namespace sgre::text {

	static constexpr std::size_t kMaximumOutlinePoints = 32767;

	static void ValidateOutline(std::span<const resource::TrueTypePoint> points, std::span<const std::uint16_t> ends) {
		if (points.size() > kMaximumOutlinePoints || ends.size() > points.size() || points.empty() != ends.empty()) {
			throw std::runtime_error("TrueType glyph path point and contour counts disagree or exceed the native domain");
		}

		std::size_t start = 0;
		for (const auto end : ends) {
			if (end < start || end >= points.size()) {
				throw std::runtime_error("TrueType glyph path contour endpoint is out of order or range");
			}

			start = std::size_t(end) + 1;
		}
		if (start != points.size()) {
			throw std::runtime_error("TrueType glyph path final contour does not cover all points");
		}

		for (const auto& point : points) {
			if (point.Kind != resource::TrueTypePointKind::OnCurve && point.Kind != resource::TrueTypePointKind::ConicControl) {
				throw std::runtime_error("TrueType glyph path requires on-curve or quadratic control tags");
			}
			if (point.Y == std::numeric_limits<std::int32_t>::min()) {
				throw std::runtime_error("TrueType glyph path drawing-axis negation exceeds the signed range");
			}
		}
	}

	static std::int32_t MidpointCoordinate(std::int32_t first, std::int32_t second) {
		// Native 39d930 wraps the signed coordinate sum, then divides toward zero.
		return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(first) + static_cast<std::uint32_t>(second)) / 2;
	}

	static resource::TrueTypePoint Midpoint(const resource::TrueTypePoint& first, const resource::TrueTypePoint& second) {
		resource::TrueTypePoint result;
		result.X = MidpointCoordinate(first.X, second.X);
		result.Y = MidpointCoordinate(first.Y, second.Y);

		return result;
	}

	static CffGlyphPathPoint PixelPoint(const resource::TrueTypePoint& point) {
		CffGlyphPathPoint result;
		result.X = double(point.X) / 64;
		result.Y = double(-point.Y) / 64;

		return result;
	}

	static void IncludePoint(CffGlyphPathBounds& bounds, CffGlyphPathPoint point) {
		bounds.Left = std::min(bounds.Left, point.X);
		bounds.Top = std::min(bounds.Top, point.Y);
		bounds.Right = std::max(bounds.Right, point.X);
		bounds.Bottom = std::max(bounds.Bottom, point.Y);
	}

	static double QuadraticExtremum(double start, double control, double end) {
		auto amount = (start - control) / ((start - (control + control)) + end);
		// Native MAXSD with zero also maps the constant-axis NaN to zero.
		amount = amount > 0 ? std::min(amount, 1.0) : 0;
		const auto first = (start - start * amount) + control * amount;
		const auto second = (control - control * amount) + end * amount;

		return (first - first * amount) + second * amount;
	}

	static void AppendPoint(CffGlyphPath& path, CffGlyphPathCommandKind kind, const resource::TrueTypePoint& point) {
		CffGlyphPathCommand command;
		command.Kind = kind;
		command.Points[0] = PixelPoint(point);
		if (path.Commands.empty()) {
			path.Bounds.Left = path.Bounds.Right = command.Points[0].X;
			path.Bounds.Top = path.Bounds.Bottom = command.Points[0].Y;
		} else {
			IncludePoint(path.Bounds, command.Points[0]);
		}
		path.Commands.push_back(command);
	}

	static void AppendQuadratic(CffGlyphPath& path, const resource::TrueTypePoint& control, const resource::TrueTypePoint& end) {
		const auto& previous = path.Commands.back();
		const auto start = previous.Points[previous.Kind == CffGlyphPathCommandKind::QuadraticTo ? 1 : 0];
		CffGlyphPathCommand command;
		command.Kind = CffGlyphPathCommandKind::QuadraticTo;
		command.Points[0] = PixelPoint(control);
		command.Points[1] = PixelPoint(end);
		IncludePoint(path.Bounds, command.Points[1]);
		const auto point = command.Points[0];
		if (point.X < path.Bounds.Left || point.X > path.Bounds.Right || point.Y < path.Bounds.Top || point.Y > path.Bounds.Bottom) {
			const auto x = QuadraticExtremum(start.X, point.X, command.Points[1].X);
			const auto y = QuadraticExtremum(start.Y, point.Y, command.Points[1].Y);
			path.Bounds.Left = std::min(path.Bounds.Left, x);
			path.Bounds.Right = std::max(path.Bounds.Right, x);
			path.Bounds.Top = std::min(path.Bounds.Top, y);
			path.Bounds.Bottom = std::max(path.Bounds.Bottom, y);
		}
		path.Commands.push_back(command);
	}

	static void AppendContour(CffGlyphPath& path, std::span<const resource::TrueTypePoint> points, std::size_t first, std::size_t last) {
		auto start = points[first];
		auto index = first + 1;
		auto limit = last + 1;
		if (start.Kind == resource::TrueTypePointKind::ConicControl) {
			if (points[last].Kind == resource::TrueTypePointKind::OnCurve) {
				start = points[last];
				--limit;
			} else {
				start = Midpoint(start, points[last]);
			}

			index = first;
		}

		AppendPoint(path, CffGlyphPathCommandKind::MoveTo, start);
		while (index < limit) {
			const auto point = points[index++];
			if (point.Kind == resource::TrueTypePointKind::OnCurve) {
				AppendPoint(path, CffGlyphPathCommandKind::LineTo, point);
				continue;
			}

			if (index == limit) {
				AppendQuadratic(path, point, start);
				return;
			}
			if (points[index].Kind == resource::TrueTypePointKind::OnCurve) {
				AppendQuadratic(path, point, points[index++]);
			} else {
				AppendQuadratic(path, point, Midpoint(point, points[index]));
			}
		}

		AppendPoint(path, CffGlyphPathCommandKind::LineTo, start);
	}

	CffGlyphPath PrepareTrueTypeGlyphPath(std::span<const resource::TrueTypePoint> scaled_points, std::span<const std::uint16_t> contour_ends) {
		ValidateOutline(scaled_points, contour_ends);

		CffGlyphPath result;
		result.Commands.reserve(scaled_points.size() + contour_ends.size());
		std::size_t start = 0;
		for (const auto end : contour_ends) {
			AppendContour(result, scaled_points, start, end);
			start = std::size_t(end) + 1;
		}

		return result;
	}

} // namespace sgre::text
