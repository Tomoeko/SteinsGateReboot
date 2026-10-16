#include "sgre/text/glyph_raster_edges.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace sgre::text {

	static constexpr std::size_t kMaximumPolylinePoints = 32767;
	static constexpr std::size_t kMaximumRasterEdges = 65536;
	static constexpr std::size_t kMaximumRasterEdgePoints = 1024 * 1024;

	struct RasterEdgeAccumulator {
		std::vector<GlyphRasterEdge> Edges;
		std::size_t PointCount = 0;
	};

	static void AppendCompletedEdge(GlyphRasterEdge edge, RasterEdgeAccumulator& output) {
		if (output.Edges.size() == kMaximumRasterEdges || edge.Points.size() > kMaximumRasterEdgePoints - output.PointCount) {
			throw std::runtime_error("glyph raster edge geometry exceeds bounded allocation/work limits");
		}

		output.PointCount += edge.Points.size();
		output.Edges.push_back(std::move(edge));
	}

	static void ValidateClip(const GlyphRasterClip& clip) {
		if (clip.WidthPixels == 0 || clip.HeightPixels == 0 || clip.WidthPixels == 65535 || clip.HeightPixels == 65535 || (clip.BandHeightPixels != 8 && clip.BandHeightPixels != 16 && clip.BandHeightPixels != 32 && clip.BandHeightPixels != 64)) {
			throw std::runtime_error("glyph raster dimensions or band height are invalid");
		}
	}

	static GlyphRasterPoint QuantizePoint(CffGlyphPathPoint point, const GlyphRasterClip& clip) {
		if (!std::isfinite(point.X) || !std::isfinite(point.Y) || point.X < 0 || point.Y < 0 || point.X > clip.WidthPixels || point.Y > clip.HeightPixels) {
			throw std::runtime_error("glyph raster polyline requires finite points within its clip rectangle");
		}

		GlyphRasterPoint result;
		result.X24Dot8 = static_cast<std::int32_t>(point.X * 256);
		result.Y24Dot8 = static_cast<std::int32_t>(point.Y * 256);

		return result;
	}

	static void FinishEdge(GlyphRasterEdge& pending, RasterEdgeAccumulator& edges, std::uint8_t band_height) {
		if (pending.Points.empty()) {
			return;
		}

		if (pending.ReverseWinding) {
			std::reverse(pending.Points.begin(), pending.Points.end());
		}

		pending.BandIndex = static_cast<std::uint32_t>(pending.Points.front().Y24Dot8) / (std::uint32_t(band_height) * 256);
		AppendCompletedEdge(std::move(pending), edges);
		pending = {};
	}

	static void AppendSegment(GlyphRasterPoint start, GlyphRasterPoint end, GlyphRasterEdge& pending, RasterEdgeAccumulator& edges, std::uint8_t band_height) {
		const auto reverse = end.Y24Dot8 < start.Y24Dot8;
		const auto horizontal = end.Y24Dot8 == start.Y24Dot8;
		if (!pending.Points.empty() && !horizontal && reverse != pending.ReverseWinding) {
			FinishEdge(pending, edges, band_height);
		}

		if (pending.Points.empty()) {
			if (horizontal) {
				return;
			}

			pending.ReverseWinding = reverse;
			pending.Points.push_back(start);
		}

		pending.Points.push_back(end);
	}

	std::vector<GlyphRasterEdge> BuildGlyphRasterPolylineEdges(std::span<const CffGlyphPathPoint> points, const GlyphRasterClip& clip) {
		ValidateClip(clip);
		if (points.size() > kMaximumPolylinePoints) {
			throw std::runtime_error("glyph raster polyline point count is invalid");
		}

		std::vector<GlyphRasterPoint> vertices;
		vertices.reserve(points.size());
		for (const auto point : points) {
			vertices.push_back(QuantizePoint(point, clip));
		}

		RasterEdgeAccumulator edges;
		if (vertices.empty()) {
			return std::move(edges.Edges);
		}

		GlyphRasterEdge pending;
		for (std::size_t index = 1; index < vertices.size(); ++index) {
			AppendSegment(vertices[index - 1], vertices[index], pending, edges, clip.BandHeightPixels);
		}

		FinishEdge(pending, edges, clip.BandHeightPixels);

		if (vertices.front().Y24Dot8 != vertices.back().Y24Dot8) {
			AppendSegment(vertices.back(), vertices.front(), pending, edges, clip.BandHeightPixels);
		}

		FinishEdge(pending, edges, clip.BandHeightPixels);
		std::stable_sort(edges.Edges.begin(), edges.Edges.end(), [](const auto& first, const auto& second) {
			return first.BandIndex < second.BandIndex;
		});

		return std::move(edges.Edges);
	}

	struct RasterCubic {
		std::array<CffGlyphPathPoint, 4> Points{};
	};

	struct CubicAxisPolynomial {
		double A = 0;
		double B = 0;
		double C = 0;
		double Start = 0;
	};

	static CubicAxisPolynomial PrepareAxis(double start, double first, double second, double end) {
		const auto first_delta = first - start;
		const auto second_delta = second - first;
		const auto last_delta = end - second;
		CubicAxisPolynomial result;
		result.A = ((last_delta - second_delta) - second_delta) + first_delta;
		result.B = (second_delta - first_delta) * 3;
		result.C = first_delta * 3;
		result.Start = start;

		return result;
	}

	static void AppendInteriorRoot(double root, std::vector<double>& roots) {
		if (root >= 1e-40 && root <= 0.9999999999999999) {
			roots.push_back(root);
		}
	}

	static void AppendQuadraticRoots(double a, double b, double c, std::vector<double>& roots) {
		const auto discriminant = std::max(0.0, b * b - (a * 4) * c);
		const auto q = (std::copysign(std::sqrt(discriminant), b) + b) * -0.5;
		const auto first = c / q;
		const auto second = q / a;
		// MINSD/MAXSD select their second operand when either is unordered.
		const auto low = first < second ? first : second;
		const auto high = first > second ? first : second;
		AppendInteriorRoot(low, roots);
		if (high > low) {
			AppendInteriorRoot(high, roots);
		}
	}

	static double EvaluateAxis(const CubicAxisPolynomial& axis, double time) {
		return ((axis.A * time + axis.B) * time + axis.C) * time + axis.Start;
	}

	static double EvaluateAxisDerivative(const CubicAxisPolynomial& axis, double time) {
		return (axis.A * (time * 3) + axis.B * 2) * time + axis.C;
	}

	static std::vector<RasterCubic> SplitCubic(const RasterCubic& curve) {
		const auto& points = curve.Points;
		const auto x = PrepareAxis(points[0].X, points[1].X, points[2].X, points[3].X);
		const auto y = PrepareAxis(points[0].Y, points[1].Y, points[2].Y, points[3].Y);
		const auto cross_a = x.B * y.A - y.B * x.A;
		const auto cross_b = x.C * y.A - y.C * x.A;
		const auto cross_c = (x.C * y.B - y.C * x.B) * 2;
		std::vector<double> roots;
		roots.reserve(8);
		AppendInteriorRoot((cross_b / cross_a) * -0.5, roots);
		AppendQuadraticRoots(cross_a * 6, cross_b * 6, cross_c, roots);
		AppendQuadraticRoots(x.A * 3, x.B * 2, x.C, roots);
		AppendQuadraticRoots(y.A * 3, y.B * 2, y.C, roots);
		if (roots.empty()) {
			return {curve};
		}

		std::sort(roots.begin(), roots.end());
		roots.push_back(1);
		std::vector<RasterCubic> pieces;
		pieces.reserve(roots.size());
		auto previous_time = 0.0;
		auto previous_point = points[0];
		for (std::size_t index = 0; index < roots.size(); ++index) {
			const auto time = roots[index];
			if (time == previous_time) {
				continue;
			}

			const auto interval = (time - previous_time) * 0.3333333333333333;
			const CffGlyphPathPoint endpoint{index + 1 == roots.size() ? points[3].X : EvaluateAxis(x, time), index + 1 == roots.size() ? points[3].Y : EvaluateAxis(y, time)};
			RasterCubic piece;
			piece.Points[0] = previous_point;
			piece.Points[1] = {previous_point.X + EvaluateAxisDerivative(x, previous_time) * interval, previous_point.Y + EvaluateAxisDerivative(y, previous_time) * interval};
			piece.Points[2] = {endpoint.X - EvaluateAxisDerivative(x, time) * interval, endpoint.Y - EvaluateAxisDerivative(y, time) * interval};
			piece.Points[3] = endpoint;
			pieces.push_back(piece);
			previous_point = endpoint;
			previous_time = time;
		}

		return pieces;
	}

	static CffGlyphPathPoint Midpoint(CffGlyphPathPoint first, CffGlyphPathPoint second) {
		return {(first.X + second.X) * 0.5, (first.Y + second.Y) * 0.5};
	}

	static bool IsFlat(const RasterCubic& curve) {
		const auto& points = curve.Points;
		const auto dx = points[3].X - points[0].X;
		const auto dy = points[3].Y - points[0].Y;
		const auto first_cross = (points[1].Y - points[0].Y) * dx - (points[1].X - points[0].X) * dy;
		const auto second_cross = (points[2].Y - points[0].Y) * dx - (points[2].X - points[0].X) * dy;
		const auto tolerance = 0.2 * 256;

		return (dx * dx + dy * dy) * (tolerance * tolerance) >= std::max(first_cross * first_cross, second_cross * second_cross);
	}

	static void FlattenCubic(RasterCubic curve, GlyphRasterEdge& edge) {
		std::array<RasterCubic, 32> pending{};
		std::size_t pending_count = 0;
		edge.Points.push_back({static_cast<std::int32_t>(curve.Points[0].X), static_cast<std::int32_t>(curve.Points[0].Y)});
		for (;;) {
			if (!IsFlat(curve) && pending_count < pending.size()) {
				const auto left_first = Midpoint(curve.Points[0], curve.Points[1]);
				const auto middle = Midpoint(curve.Points[1], curve.Points[2]);
				const auto right_second = Midpoint(curve.Points[2], curve.Points[3]);
				const auto left_second = Midpoint(left_first, middle);
				const auto right_first = Midpoint(middle, right_second);
				const auto center = Midpoint(left_second, right_first);
				pending[pending_count++].Points = {center, right_first, right_second, curve.Points[3]};
				curve.Points = {curve.Points[0], left_first, left_second, center};
				continue;
			}

			if (edge.Points.size() >= kMaximumPolylinePoints) {
				throw std::runtime_error("glyph raster cubic subdivision exceeds bounded point count");
			}

			edge.Points.push_back({static_cast<std::int32_t>(curve.Points[3].X), static_cast<std::int32_t>(curve.Points[3].Y)});
			if (pending_count == 0) {
				break;
			}

			curve = pending[--pending_count];
		}
	}

	static void ValidateCurveControl(CffGlyphPathPoint point) {
		if (!std::isfinite(point.X) || !std::isfinite(point.Y) || std::abs(point.X) > 65534 || std::abs(point.Y) > 65534) {
			throw std::runtime_error("glyph raster cubic control requires finite coordinates in the supported conversion range");
		}
	}

	static void ValidateCurveEndpoint(CffGlyphPathPoint point, const GlyphRasterClip& clip) {
		if (point.X < 0 || point.Y < 0 || point.X > double(clip.WidthPixels) * 256 || point.Y > double(clip.HeightPixels) * 256) {
			throw std::runtime_error("glyph raster cubic requires out-of-rectangle clipping");
		}
	}

	static void AppendCubicEdges(const RasterCubic& original, RasterEdgeAccumulator& edges, const GlyphRasterClip& clip) {
		for (auto piece : SplitCubic(original)) {
			ValidateCurveEndpoint(piece.Points[0], clip);
			ValidateCurveEndpoint(piece.Points[3], clip);
			if (static_cast<std::int32_t>(piece.Points[0].Y) == static_cast<std::int32_t>(piece.Points[3].Y)) {
				continue;
			}

			GlyphRasterEdge edge;
			edge.ReverseWinding = piece.Points[0].Y > piece.Points[3].Y;
			if (edge.ReverseWinding) {
				std::reverse(piece.Points.begin(), piece.Points.end());
			}

			for (std::size_t index = 1; index < 3; ++index) {
				piece.Points[index].X = std::clamp(piece.Points[index].X, std::min(piece.Points[0].X, piece.Points[3].X), std::max(piece.Points[0].X, piece.Points[3].X));
				piece.Points[index].Y = std::clamp(piece.Points[index].Y, piece.Points[0].Y, piece.Points[3].Y);
			}

			FlattenCubic(piece, edge);
			edge.BandIndex = static_cast<std::uint32_t>(edge.Points.front().Y24Dot8) / (std::uint32_t(clip.BandHeightPixels) * 256);
			AppendCompletedEdge(std::move(edge), edges);
		}
	}

	struct RasterQuadratic {
		std::array<CffGlyphPathPoint, 3> Points;
	};

	static std::vector<RasterQuadratic> SplitQuadratic(const RasterQuadratic& curve) {
		const auto& points = curve.Points;
		const auto first_x = points[1].X - points[0].X;
		const auto first_y = points[1].Y - points[0].Y;
		const CffGlyphPathPoint a{(points[2].X - points[1].X) - first_x, (points[2].Y - points[1].Y) - first_y};
		const CffGlyphPathPoint b{first_x + first_x, first_y + first_y};
		const auto x_root = (points[0].X - points[1].X) / ((points[0].X - (points[1].X + points[1].X)) + points[2].X);
		const auto y_root = (points[0].Y - points[1].Y) / ((points[0].Y - (points[1].Y + points[1].Y)) + points[2].Y);
		// Native MINSD/MAXSD choose their second operand for unordered roots.
		const auto low = y_root < x_root ? y_root : x_root;
		const auto high = y_root > x_root ? y_root : x_root;
		std::vector<double> roots;
		if (low > 0 && low < 1) {
			roots.push_back(low);
		}
		if (high > (low > 0 ? low : 0) && high < 1) {
			roots.push_back(high);
		}
		if (roots.empty()) {
			return {curve};
		}

		roots.push_back(1);
		std::vector<RasterQuadratic> pieces;
		pieces.reserve(roots.size());
		auto previous_time = 0.0;
		auto previous = points[0];
		for (std::size_t index = 0; index < roots.size(); ++index) {
			const auto time = roots[index];
			const auto interval = (time - previous_time) * 0.5;
			const CffGlyphPathPoint endpoint{index + 1 == roots.size() ? points[2].X : (a.X * time + b.X) * time + points[0].X, index + 1 == roots.size() ? points[2].Y : (a.Y * time + b.Y) * time + points[0].Y};
			const CffGlyphPathPoint control{endpoint.X - (a.X * (time + time) + b.X) * interval, endpoint.Y - (a.Y * (time + time) + b.Y) * interval};
			pieces.push_back({{previous, control, endpoint}});
			previous = endpoint;
			previous_time = time;
		}

		return pieces;
	}

	static bool IsFlat(const RasterQuadratic& curve) {
		const auto& points = curve.Points;
		const auto dx = points[2].X - points[0].X;
		const auto dy = points[2].Y - points[0].Y;
		const auto cross = (points[1].Y - points[0].Y) * dx - (points[1].X - points[0].X) * dy;
		const auto tolerance = 0.2 * 256;

		return (dx * dx + dy * dy) * (tolerance * tolerance) >= cross * cross;
	}

	static void FlattenQuadratic(RasterQuadratic curve, GlyphRasterEdge& edge) {
		std::array<RasterQuadratic, 32> pending{};
		std::size_t pending_count = 0;
		edge.Points.push_back({static_cast<std::int32_t>(curve.Points[0].X), static_cast<std::int32_t>(curve.Points[0].Y)});
		for (;;) {
			if (!IsFlat(curve) && pending_count < pending.size()) {
				const auto left_control = Midpoint(curve.Points[0], curve.Points[1]);
				const auto right_control = Midpoint(curve.Points[1], curve.Points[2]);
				const auto center = Midpoint(left_control, right_control);
				pending[pending_count++].Points = {center, right_control, curve.Points[2]};
				curve.Points = {curve.Points[0], left_control, center};
				continue;
			}
			if (edge.Points.size() >= kMaximumPolylinePoints) {
				throw std::runtime_error("glyph raster quadratic subdivision exceeds bounded point count");
			}

			edge.Points.push_back({static_cast<std::int32_t>(curve.Points[2].X), static_cast<std::int32_t>(curve.Points[2].Y)});
			if (pending_count == 0) {
				break;
			}

			curve = pending[--pending_count];
		}
	}

	static void AppendQuadraticEdges(const RasterQuadratic& original, RasterEdgeAccumulator& edges, const GlyphRasterClip& clip) {
		for (auto piece : SplitQuadratic(original)) {
			ValidateCurveEndpoint(piece.Points[0], clip);
			ValidateCurveEndpoint(piece.Points[2], clip);
			if (static_cast<std::int32_t>(piece.Points[0].Y) == static_cast<std::int32_t>(piece.Points[2].Y)) {
				continue;
			}

			GlyphRasterEdge edge;
			edge.ReverseWinding = piece.Points[0].Y > piece.Points[2].Y;
			if (edge.ReverseWinding) {
				std::reverse(piece.Points.begin(), piece.Points.end());
			}
			piece.Points[1].X = std::clamp(piece.Points[1].X, std::min(piece.Points[0].X, piece.Points[2].X), std::max(piece.Points[0].X, piece.Points[2].X));
			piece.Points[1].Y = std::clamp(piece.Points[1].Y, piece.Points[0].Y, piece.Points[2].Y);
			FlattenQuadratic(piece, edge);
			edge.BandIndex = static_cast<std::uint32_t>(edge.Points.front().Y24Dot8) / (std::uint32_t(clip.BandHeightPixels) * 256);
			AppendCompletedEdge(std::move(edge), edges);
		}
	}

	std::vector<GlyphRasterEdge> BuildGlyphRasterQuadraticEdges(std::span<const CffGlyphPathPoint, 3> points, const GlyphRasterClip& clip) {
		ValidateClip(clip);
		RasterQuadratic original;
		for (std::size_t index = 0; index < points.size(); ++index) {
			if (index == 1) {
				ValidateCurveControl(points[index]);
			} else {
				static_cast<void>(QuantizePoint(points[index], clip));
			}
			original.Points[index] = {points[index].X * 256, points[index].Y * 256};
		}
		RasterEdgeAccumulator edges;
		AppendQuadraticEdges(original, edges, clip);
		GlyphRasterEdge closing;
		AppendSegment(QuantizePoint(points.back(), clip), QuantizePoint(points.front(), clip), closing, edges, clip.BandHeightPixels);
		FinishEdge(closing, edges, clip.BandHeightPixels);
		std::stable_sort(edges.Edges.begin(), edges.Edges.end(), [](const auto& first, const auto& second) {
			return first.BandIndex < second.BandIndex;
		});

		return std::move(edges.Edges);
	}

	std::vector<GlyphRasterEdge> BuildGlyphRasterCubicEdges(std::span<const CffGlyphPathPoint, 4> points, const GlyphRasterClip& clip) {
		ValidateClip(clip);
		RasterCubic original;
		for (std::size_t index = 0; index < points.size(); ++index) {
			if (index == 0 || index == 3) {
				static_cast<void>(QuantizePoint(points[index], clip));
			} else {
				ValidateCurveControl(points[index]);
			}

			original.Points[index] = {points[index].X * 256, points[index].Y * 256};
		}

		RasterEdgeAccumulator edges;
		AppendCubicEdges(original, edges, clip);

		GlyphRasterEdge closing;
		AppendSegment(QuantizePoint(points.back(), clip), QuantizePoint(points.front(), clip), closing, edges, clip.BandHeightPixels);
		FinishEdge(closing, edges, clip.BandHeightPixels);
		std::stable_sort(edges.Edges.begin(), edges.Edges.end(), [](const auto& first, const auto& second) {
			return first.BandIndex < second.BandIndex;
		});

		return std::move(edges.Edges);
	}

	static void CloseContour(CffGlyphPathPoint current, CffGlyphPathPoint start, GlyphRasterEdge& pending, RasterEdgeAccumulator& edges, const GlyphRasterClip& clip) {
		FinishEdge(pending, edges, clip.BandHeightPixels);
		AppendSegment(QuantizePoint(current, clip), QuantizePoint(start, clip), pending, edges, clip.BandHeightPixels);
		FinishEdge(pending, edges, clip.BandHeightPixels);
	}

	std::vector<GlyphRasterEdge> BuildGlyphRasterPathEdges(std::span<const CffGlyphPathCommand> commands, const GlyphRasterClip& clip) {
		ValidateClip(clip);
		if (commands.size() > kMaximumPolylinePoints) {
			throw std::runtime_error("glyph raster path command count is invalid");
		}

		for (const auto& command : commands) {
			if (command.Kind != CffGlyphPathCommandKind::MoveTo && command.Kind != CffGlyphPathCommandKind::LineTo && command.Kind != CffGlyphPathCommandKind::CubicTo && command.Kind != CffGlyphPathCommandKind::QuadraticTo && command.Kind != CffGlyphPathCommandKind::Close) {
				throw std::runtime_error("glyph raster path command kind is invalid");
			}

			const auto count = command.Kind == CffGlyphPathCommandKind::Close ? std::size_t(0) : command.Kind == CffGlyphPathCommandKind::QuadraticTo ? std::size_t(2) : command.Kind == CffGlyphPathCommandKind::CubicTo ? std::size_t(3) : std::size_t(1);
			for (std::size_t index = 0; index < count; ++index) {
				if ((command.Kind == CffGlyphPathCommandKind::CubicTo && index < 2) || (command.Kind == CffGlyphPathCommandKind::QuadraticTo && index == 0)) {
					ValidateCurveControl(command.Points[index]);
				} else {
					static_cast<void>(QuantizePoint(command.Points[index], clip));
				}
			}
		}

		RasterEdgeAccumulator edges;
		GlyphRasterEdge pending;
		CffGlyphPathPoint current;
		CffGlyphPathPoint start;
		bool active = false;
		for (const auto& command : commands) {
			if (command.Kind == CffGlyphPathCommandKind::MoveTo) {
				if (active) {
					CloseContour(current, start, pending, edges, clip);
				}

				start = command.Points[0];
				current = start;
				active = true;
				continue;
			}

			if (!active) {
				throw std::runtime_error("glyph raster path requires a move before drawing");
			}

			if (command.Kind == CffGlyphPathCommandKind::Close) {
				CloseContour(current, start, pending, edges, clip);
				active = false;
				continue;
			}
			if (command.Kind == CffGlyphPathCommandKind::QuadraticTo) {
				FinishEdge(pending, edges, clip.BandHeightPixels);
				RasterQuadratic curve;
				curve.Points = {{{current.X * 256, current.Y * 256}, {command.Points[0].X * 256, command.Points[0].Y * 256}, {command.Points[1].X * 256, command.Points[1].Y * 256}}};
				AppendQuadraticEdges(curve, edges, clip);
				current = command.Points[1];
				continue;
			}

			if (command.Kind == CffGlyphPathCommandKind::LineTo) {
				AppendSegment(QuantizePoint(current, clip), QuantizePoint(command.Points[0], clip), pending, edges, clip.BandHeightPixels);
				current = command.Points[0];
				continue;
			}

			FinishEdge(pending, edges, clip.BandHeightPixels);
			RasterCubic curve;
			curve.Points[0] = {current.X * 256, current.Y * 256};
			for (std::size_t index = 0; index < 3; ++index) {
				curve.Points[index + 1] = {command.Points[index].X * 256, command.Points[index].Y * 256};
			}

			AppendCubicEdges(curve, edges, clip);
			current = command.Points[2];
		}

		if (active) {
			CloseContour(current, start, pending, edges, clip);
		}

		std::stable_sort(edges.Edges.begin(), edges.Edges.end(), [](const auto& first, const auto& second) {
			return first.BandIndex < second.BandIndex;
		});

		return std::move(edges.Edges);
	}

} // namespace sgre::text
