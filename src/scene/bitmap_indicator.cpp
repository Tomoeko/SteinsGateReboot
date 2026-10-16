#include "sgre/scene/bitmap_indicator.hpp"

#include "sgre/text/encoding.hpp"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace sgre::scene {

	static void RequireFiniteIndicatorSize(resource::GlyphSize size) {
		if (!std::isfinite(size.Width) || !std::isfinite(size.Height)) {
			throw std::runtime_error("bitmap Indicator requires finite font scale, spacing and dimensions");
		}
	}

	BitmapIndicator::BitmapIndicator(resource::BitmapFont font) : BitmapIndicator(std::make_shared<const resource::BitmapFont>(std::move(font))) {
	}

	BitmapIndicator::BitmapIndicator(std::shared_ptr<const resource::BitmapFont> font) : m_Font(std::move(font)) {
		if (!m_Font) {
			throw std::runtime_error("bitmap Indicator requires owned immutable font metadata");
		}

		m_MaximumFontSize = {m_Font->MaxWidth, m_Font->MaxHeight};
		RequireFiniteIndicatorSize(m_MaximumFontSize);
	}

	resource::GlyphSize BitmapIndicator::Measure(std::span<const text::IndicatorTextElement> elements, resource::GlyphSize scale, resource::GlyphSize spacing) const {
		RequireFiniteIndicatorSize(scale);
		RequireFiniteIndicatorSize(spacing);
		text::IndicatorTextMeasurement measurement(spacing);
		for (const auto& element : elements) {
			if (element.CodeUnits.empty()) {
				throw std::runtime_error("bitmap Indicator contains an invalid empty text element");
			}

			text::IndicatorMeasuredElement entry;
			entry.LineBreak = element.IsLineBreak();
			entry.Scale = element.Scale;
			if (!entry.LineBreak) {
				// The original bitmap provider measures only the first unit of a prepared cluster.
				entry.Metrics = resource::MeasureBitmapGlyph(m_Font->FindGlyphCodeUnit(element.CodeUnits.front()), scale.Width, scale.Height);
			}

			measurement.Append(entry);
		}

		return measurement.Finish();
	}

	void BitmapIndicator::Print(std::string_view utf8) {
		if (m_Placement.IsDetached()) {
			return;
		}

		utf8 = utf8.substr(0, utf8.find('\0'));
		const auto units = text::DecodeTextCodeUnits(utf8);
		auto elements = text::PrepareIndicatorText(units, m_FontColor, m_RecognizeTags);
		const auto size = Measure(elements, m_FontScale, m_Spacing);
		m_Elements = std::move(elements);
		m_Placement.UpdateMeasuredSize({size.Width, size.Height});
	}

	void BitmapIndicator::SetFontScale(resource::GlyphSize scale) {
		if (m_Placement.IsDetached()) {
			return;
		}

		const auto size = Measure(m_Elements, scale, m_Spacing);
		m_FontScale = scale;
		m_Placement.UpdateMeasuredSize({size.Width, size.Height});
	}

	void BitmapIndicator::SetUniformFontScale(float scale) {
		SetFontScale({scale, scale});
	}

	void BitmapIndicator::SetFontScaleX(float scale) {
		SetFontScale({scale, m_FontScale.Height});
	}

	void BitmapIndicator::SetFontScaleY(float scale) {
		SetFontScale({m_FontScale.Width, scale});
	}

	void BitmapIndicator::SetFontColor(std::uint32_t color) {
		if (!m_Placement.IsDetached()) {
			// Previously prepared elements retain the color captured when their text was printed.
			m_FontColor = color;
		}
	}

	void BitmapIndicator::SetRecognizeTags(bool enabled) {
		if (m_Placement.IsDetached()) {
			return;
		}

		const auto size = Measure(m_Elements, m_FontScale, m_Spacing);
		m_RecognizeTags = enabled;
		m_Placement.UpdateMeasuredSize({size.Width, size.Height});
	}

	void BitmapIndicator::SetSpacing(resource::GlyphSize spacing) {
		if (m_Placement.IsDetached()) {
			return;
		}

		const auto size = Measure(m_Elements, m_FontScale, spacing);
		m_Spacing = spacing;
		m_Placement.UpdateMeasuredSize({size.Width, size.Height});
	}

	void BitmapIndicator::SetSpacingX(float spacing) {
		SetSpacing({spacing, m_Spacing.Height});
	}

	void BitmapIndicator::SetSpacingY(float spacing) {
		SetSpacing({m_Spacing.Width, spacing});
	}

	void BitmapIndicator::SetOpacity(std::uint32_t opacity) {
		if (!m_Placement.IsDetached()) {
			m_Opacity = opacity;
		}
	}

	void BitmapIndicator::SetAlignment(std::int32_t alignment) {
		if (!m_Placement.IsDetached()) {
			m_Alignment = alignment;
		}
	}

	void BitmapIndicator::SetDrawAtIntegerCoordinates(bool enabled) {
		if (!m_Placement.IsDetached()) {
			m_DrawAtIntegerCoordinates = enabled;
		}
	}

	resource::GlyphSize BitmapIndicator::FontScale() const noexcept {
		return m_Placement.IsDetached() ? resource::GlyphSize{1, 1} : m_FontScale;
	}

	resource::GlyphSize BitmapIndicator::MaximumFontSize() const noexcept {
		// MPSBFont scale setters do not rewrite its constructor-cached maximum dimensions.
		return m_Placement.IsDetached() ? resource::GlyphSize{1, 1} : m_MaximumFontSize;
	}

	std::uint32_t BitmapIndicator::FontColor() const noexcept {
		return m_Placement.IsDetached() ? 0xffffffffu : m_FontColor;
	}

	bool BitmapIndicator::RecognizeTags() const noexcept {
		return m_Placement.IsDetached() || m_RecognizeTags;
	}

	resource::GlyphSize BitmapIndicator::Spacing() const noexcept {
		return m_Placement.IsDetached() ? resource::GlyphSize{1, 1} : m_Spacing;
	}

	std::uint32_t BitmapIndicator::Opacity() const noexcept {
		return m_Placement.IsDetached() ? 255 : m_Opacity;
	}

	std::int32_t BitmapIndicator::Alignment() const noexcept {
		return m_Placement.IsDetached() ? 0 : m_Alignment;
	}

	bool BitmapIndicator::DrawAtIntegerCoordinates() const noexcept {
		return m_Placement.IsDetached() || m_DrawAtIntegerCoordinates;
	}

	std::span<const text::IndicatorTextElement> BitmapIndicator::Elements() const noexcept {
		return m_Placement.IsDetached() ? std::span<const text::IndicatorTextElement>{} : m_Elements;
	}

	IndicatorPlacement& BitmapIndicator::Placement() noexcept {
		return m_Placement;
	}

	const IndicatorPlacement& BitmapIndicator::Placement() const noexcept {
		return m_Placement;
	}

} // namespace sgre::scene
