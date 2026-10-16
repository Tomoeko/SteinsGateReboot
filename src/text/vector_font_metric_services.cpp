#include "sgre/text/vector_font_metric_services.hpp"

#include "sgre/text/vector_font.hpp"
#include "sgre/text/vector_metrics.hpp"

#include <limits>
#include <stdexcept>

namespace sgre::text {
	VectorFontMetricServices::VectorFontMetricServices(const VectorFont& actual_font, VectorFontMetricScaleSign actual_initial_hb_x_sign, VectorFontMetricScaleSign actual_initial_hb_y_sign) : m_Font(actual_font), m_XNegative(actual_initial_hb_x_sign == VectorFontMetricScaleSign::Negative), m_YNegative(actual_initial_hb_y_sign == VectorFontMetricScaleSign::Negative) {
		if ((actual_initial_hb_x_sign != VectorFontMetricScaleSign::Nonnegative && actual_initial_hb_x_sign != VectorFontMetricScaleSign::Negative) || (actual_initial_hb_y_sign != VectorFontMetricScaleSign::Nonnegative && actual_initial_hb_y_sign != VectorFontMetricScaleSign::Negative)) {
			throw std::runtime_error("flags-2 font metrics require actual initialized HB scale signs");
		}

		if ((!actual_font.m_Cff && !actual_font.m_TrueType) || !Font().VerticalMetrics(0)) {
			throw std::runtime_error("flags-2 font metrics require the actual outline owner and original vertical tables");
		}
		static_cast<void>(RequireAppliedPixelSize());
	}

	const resource::SfntFont& VectorFontMetricServices::Font() const {
		return m_Font.Font();
	}

	bool VectorFontMetricServices::HasHorizontalOrigin() const noexcept {
		// Native3a6f50 installs no +150 callback; callers must retain that capability gate.
		return false;
	}

	bool VectorFontMetricServices::HasVerticalOrigin() const noexcept {
		return true;
	}

	std::int32_t VectorFontMetricServices::RequireAppliedPixelSize() const {
		const auto& size = m_Font.SizeState();
		if (!size.RequestedHeight26Dot6 || size.Word == 0 || size.Word > 2000 || *size.RequestedHeight26Dot6 != static_cast<std::int32_t>(size.Word * 64)) {
			throw std::runtime_error("flags-2 font metrics require the current applied positive supported size; initial/failed FT size preservation remains unrecovered");
		}

		return static_cast<std::int32_t>(size.Word);
	}

	static std::int32_t ScaleCallbackAdvance(const VectorFontScale& scale, std::uint16_t advance, bool negative, bool vertical) {
		// Actual3a1080 publishes 16.16 first;3a73d0/3a74b0 apply the immutable HB sign before rounding.
		const auto advance16 = (std::int64_t(advance) * scale.Scale16Dot16 + 32) / 64;
		if (advance16 > std::numeric_limits<std::int32_t>::max() - 512) {
			throw std::runtime_error("flags-2 font advance exceeds the supported signed 16.16 callback range");
		}

		const auto signed_advance = negative ? -advance16 : advance16;
		const auto rounded = (vertical ? 512 - signed_advance : signed_advance + 512) >> 10;

		return static_cast<std::int32_t>(rounded);
	}

	std::int32_t VectorFontMetricServices::HorizontalAdvance(std::uint32_t glyph_index) const {
		if (glyph_index >= Font().GlyphCount()) {
			return 0;
		}

		const auto scale = PrepareVectorFontScale(RequireAppliedPixelSize(), Font().UnitsPerEm());

		return ScaleCallbackAdvance(scale, Font().HorizontalMetrics(glyph_index).Advance, m_XNegative, false);
	}

	std::int32_t VectorFontMetricServices::VerticalAdvance(std::uint32_t glyph_index) const {
		if (glyph_index >= Font().GlyphCount()) {
			return 0;
		}

		const auto scale = PrepareVectorFontScale(RequireAppliedPixelSize(), Font().UnitsPerEm());
		const auto metrics = Font().VerticalMetrics(glyph_index);
		if (!metrics) {
			throw std::runtime_error("flags-2 vertical advance synthesis remains unrecovered");
		}

		return ScaleCallbackAdvance(scale, metrics->Advance, m_YNegative, true);
	}

	static std::int32_t ApplyOriginSign(std::int32_t coordinate, bool negative) {
		if (negative && coordinate == std::numeric_limits<std::int32_t>::min()) {
			throw std::runtime_error("flags-2 font origin sign exceeds its supported signed coordinate range");
		}

		return negative ? -coordinate : coordinate;
	}

	std::optional<VectorFontGlyphOrigin> VectorFontMetricServices::VerticalOrigin(std::uint32_t glyph_index) const {
		if (glyph_index >= Font().GlyphCount()) {
			return {};
		}

		VectorFontGlyphOrigin result;
		if (m_Font.m_Cff) {
			const auto glyph = m_Font.m_Cff->PrepareUnhintedGlyph(glyph_index, RequireAppliedPixelSize());
			result.X26Dot6 = ApplyOriginSign(glyph.Outline.Metrics.VerticalOriginX26Dot6, m_XNegative);
			result.Y26Dot6 = ApplyOriginSign(glyph.Outline.Metrics.VerticalOriginY26Dot6, m_YNegative);
		} else {
			const auto scale = PrepareVectorFontScale(RequireAppliedPixelSize(), Font().UnitsPerEm());
			const auto glyph = m_Font.m_TrueType->PrepareUnhintedGlyph(glyph_index, scale, scale);
			result.X26Dot6 = ApplyOriginSign(glyph.Metrics.VerticalOriginX26Dot6, m_XNegative);
			result.Y26Dot6 = ApplyOriginSign(glyph.Metrics.VerticalOriginY26Dot6, m_YNegative);
		}

		return result;
	}
} // namespace sgre::text
