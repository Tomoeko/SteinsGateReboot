#include "sgre/text/cff_glyph_metrics.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace sgre::text {

	static constexpr std::size_t kMaxOutlinePoints = 32767;

	static std::int32_t CheckedCoordinate(std::int64_t value) {
		if (value < std::numeric_limits<std::int32_t>::min() || value > std::numeric_limits<std::int32_t>::max()) {
			throw std::runtime_error("CFF glyph metrics exceed the native signed coordinate range");
		}

		return static_cast<std::int32_t>(value);
	}

	static std::int32_t ScaleCoordinate(std::int32_t value, std::int32_t scale) {
		const auto product = std::int64_t(value) * scale;
		const auto magnitude = product < 0 ? -product : product;
		const auto rounded = (magnitude + 32768) / 65536;

		return CheckedCoordinate(product < 0 ? -rounded : rounded);
	}

	static void ValidateOutline(const resource::CffDesignOutline& outline) {
		if (outline.Points.size() > kMaxOutlinePoints || outline.ContourEnds.size() > outline.Points.size()) {
			throw std::runtime_error("CFF glyph metrics outline exceeds native point or contour counts");
		}
		if (outline.Points.empty() != outline.ContourEnds.empty()) {
			throw std::runtime_error("CFF glyph metrics point and contour storage disagree");
		}

		std::size_t start = 0;
		for (const auto end : outline.ContourEnds) {
			if (end < start || end >= outline.Points.size()) {
				throw std::runtime_error("CFF glyph metrics contour endpoint is out of order or range");
			}

			start = std::size_t(end) + 1;
		}
		if (start != outline.Points.size()) {
			throw std::runtime_error("CFF glyph metrics final contour does not cover its point storage");
		}
		for (const auto& point : outline.Points) {
			if (point.Kind != resource::CffPointKind::OnCurve && point.Kind != resource::CffPointKind::CubicControl) {
				throw std::runtime_error("CFF glyph metrics has an unknown point tag");
			}
		}
	}

	static CffGlyphMetrics MeasureScaledPoints(const CffScaledGlyph& glyph, const CffGlyphFontMetrics& font_metrics, std::int32_t horizontal_advance, std::int32_t vertical_advance) {
		std::int32_t minimum_x = 0;
		std::int32_t minimum_y = 0;
		std::int32_t maximum_x = 0;
		std::int32_t maximum_y = 0;
		if (!glyph.Points.empty()) {
			minimum_x = maximum_x = glyph.Points.front().X26Dot6;
			minimum_y = maximum_y = glyph.Points.front().Y26Dot6;
		}
		for (const auto& point : glyph.Points) {
			minimum_x = std::min(minimum_x, point.X26Dot6);
			minimum_y = std::min(minimum_y, point.Y26Dot6);
			maximum_x = std::max(maximum_x, point.X26Dot6);
			maximum_y = std::max(maximum_y, point.Y26Dot6);
		}

		CffGlyphMetrics metrics;
		metrics.Width26Dot6 = CheckedCoordinate(std::int64_t(maximum_x) - minimum_x);
		metrics.Height26Dot6 = CheckedCoordinate(std::int64_t(maximum_y) - minimum_y);
		metrics.HorizontalBearingX26Dot6 = minimum_x;
		metrics.HorizontalBearingY26Dot6 = maximum_y;
		metrics.HorizontalAdvance26Dot6 = horizontal_advance;
		metrics.VerticalBearingX26Dot6 = CheckedCoordinate(std::int64_t(minimum_x) - horizontal_advance / 2);
		metrics.VerticalSideBearingFontUnits = font_metrics.VerticalSideBearing;
		metrics.VerticalAdvance26Dot6 = vertical_advance;
		metrics.VerticalOriginX26Dot6 = CheckedCoordinate(std::int64_t(minimum_x) - metrics.VerticalBearingX26Dot6);
		metrics.VerticalOriginY26Dot6 = CheckedCoordinate(std::int64_t(font_metrics.VerticalSideBearing) + maximum_y);

		return metrics;
	}

	CffScaledGlyph ScaleCffGlyphOutline26Dot6(const resource::CffDesignOutline& outline, VectorFontScale horizontal_scale, VectorFontScale vertical_scale, const CffGlyphFontMetrics& font_metrics) {
		if (horizontal_scale.Scale16Dot16 <= 0 || vertical_scale.Scale16Dot16 <= 0) {
			throw std::runtime_error("CFF glyph metrics require positive horizontal and vertical scales");
		}
		ValidateOutline(outline);

		const auto horizontal_advance = ScaleCoordinate(font_metrics.HorizontalAdvance, horizontal_scale.Scale16Dot16);
		const auto vertical_advance = ScaleCoordinate(font_metrics.VerticalAdvance, vertical_scale.Scale16Dot16);
		CffScaledGlyph result;
		result.Points.reserve(outline.Points.size());
		for (const auto& point : outline.Points) {
			CffScaledPoint scaled;
			scaled.X26Dot6 = ScaleCoordinate(point.X, horizontal_scale.Scale16Dot16);
			scaled.Y26Dot6 = ScaleCoordinate(point.Y, vertical_scale.Scale16Dot16);
			scaled.Kind = point.Kind;
			result.Points.push_back(scaled);
		}
		result.ContourEnds = outline.ContourEnds;
		result.Metrics = MeasureScaledPoints(result, font_metrics, horizontal_advance, vertical_advance);

		return result;
	}

} // namespace sgre::text
