#include "sgre/text/cff_hinted_metrics.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace sgre::text {

	static constexpr std::size_t kMaximumOutlinePoints = 32767;

	static std::int32_t CheckedMetric(std::int64_t value) {
		if (value < std::numeric_limits<std::int32_t>::min() || value > std::numeric_limits<std::int32_t>::max()) {
			throw std::runtime_error("CFF hinted metrics exceed the native signed range");
		}

		return static_cast<std::int32_t>(value);
	}

	static std::int32_t ScaleAdvance(std::uint16_t advance, std::int32_t scale, std::int64_t divisor) {
		return CheckedMetric((std::int64_t(advance) * scale + divisor / 2) / divisor);
	}

	static std::int64_t FloorToPixel(std::int64_t value) {
		const auto whole = value >= 0 ? value / 64 : -((-value + 63) / 64);

		return whole * 64;
	}

	static std::int32_t CeilToPixel(std::int64_t value) {
		return CheckedMetric(FloorToPixel(value + 63));
	}

	static void ValidateOutline(const CffContourOutline& outline) {
		if (outline.Points.size() > kMaximumOutlinePoints || outline.ContourEnds.size() > outline.Points.size() || outline.Points.empty() != outline.ContourEnds.empty()) {
			throw std::runtime_error("CFF hinted metrics point or contour count is invalid");
		}

		std::size_t start = 0;
		for (const auto end : outline.ContourEnds) {
			if (end < start || end >= outline.Points.size()) {
				throw std::runtime_error("CFF hinted metrics contour endpoint is out of order or range");
			}

			start = std::size_t(end) + 1;
		}

		if (start != outline.Points.size()) {
			throw std::runtime_error("CFF hinted metrics final contour does not cover all points");
		}

		for (const auto& point : outline.Points) {
			if (point.Kind != resource::CffPointKind::OnCurve && point.Kind != resource::CffPointKind::CubicControl) {
				throw std::runtime_error("CFF hinted metrics point tag is unsupported");
			}
		}
	}

	CffHintedGlyphMetrics MeasureCffHintedGlyph(const CffContourOutline& outline, VectorFontScale horizontal_scale, VectorFontScale vertical_scale, const CffGlyphFontMetrics& font_metrics, std::uint16_t vertical_pixels_per_em) {
		if (horizontal_scale.Scale16Dot16 <= 0 || vertical_scale.Scale16Dot16 <= 0 || vertical_pixels_per_em == 0) {
			throw std::runtime_error("CFF hinted metrics require positive scales and pixels per em");
		}

		ValidateOutline(outline);
		std::int32_t minimum_x = 0;
		std::int32_t minimum_y = 0;
		std::int32_t maximum_x = 0;
		std::int32_t maximum_y = 0;
		if (!outline.Points.empty()) {
			minimum_x = maximum_x = outline.Points.front().X26Dot6;
			minimum_y = maximum_y = outline.Points.front().Y26Dot6;
		}

		for (const auto& point : outline.Points) {
			minimum_x = std::min(minimum_x, point.X26Dot6);
			minimum_y = std::min(minimum_y, point.Y26Dot6);
			maximum_x = std::max(maximum_x, point.X26Dot6);
			maximum_y = std::max(maximum_y, point.Y26Dot6);
		}

		const auto horizontal_advance = ScaleAdvance(font_metrics.HorizontalAdvance, horizontal_scale.Scale16Dot16, 65536);
		const auto vertical_advance = ScaleAdvance(font_metrics.VerticalAdvance, vertical_scale.Scale16Dot16, 65536);
		const auto horizontal_bearing = CheckedMetric(FloorToPixel(minimum_x));
		const auto top = CeilToPixel(maximum_y);
		CffHintedGlyphMetrics result;
		result.Width26Dot6 = CheckedMetric(std::int64_t(CeilToPixel(maximum_x)) - horizontal_bearing);
		result.Height26Dot6 = CheckedMetric(std::int64_t(top) - FloorToPixel(minimum_y));
		result.HorizontalBearingX26Dot6 = horizontal_bearing;
		result.HorizontalBearingY26Dot6 = top;
		result.HorizontalAdvance26Dot6 = CheckedMetric(FloorToPixel(std::int64_t(horizontal_advance) + 32));
		result.VerticalBearingX26Dot6 = CheckedMetric(FloorToPixel(std::int64_t(minimum_x) - horizontal_advance / 2));
		result.VerticalBearingY26Dot6 = CheckedMetric(FloorToPixel(font_metrics.VerticalSideBearing));
		result.VerticalAdvance26Dot6 = CheckedMetric(FloorToPixel(std::int64_t(vertical_advance) + 32));
		result.LinearHorizontalAdvance16Dot16 = ScaleAdvance(font_metrics.HorizontalAdvance, horizontal_scale.Scale16Dot16, 64);
		result.LinearVerticalAdvance16Dot16 = ScaleAdvance(font_metrics.VerticalAdvance, vertical_scale.Scale16Dot16, 64);
		result.AdvanceX26Dot6 = result.HorizontalAdvance26Dot6;
		result.OutlineFlags = vertical_pixels_per_em < 24 ? 0x104 : 4;

		return result;
	}

} // namespace sgre::text
