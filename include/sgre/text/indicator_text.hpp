#pragma once

#include "sgre/resource/font.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace sgre::text {

	struct IndicatorTextElement {
		std::vector<std::uint16_t> CodeUnits;
		std::uint32_t Color = 0xffffffffu;
		float Scale = 1;

		[[nodiscard]] bool IsLineBreak() const noexcept;
	};

	struct IndicatorMeasuredElement {
		resource::GlyphSize Metrics;
		float Scale = 1;
		bool LineBreak = false;
	};

	class IndicatorTextMeasurement {
	public:
		explicit IndicatorTextMeasurement(resource::GlyphSize spacing = {});
		void Append(const IndicatorMeasuredElement& element);
		[[nodiscard]] resource::GlyphSize Finish() const;

	private:
		resource::GlyphSize m_Spacing;
		resource::GlyphSize m_CompletedLines;
		float m_LineWidth = 0;
		float m_LineHeight = 0;
		std::uint32_t m_LineElements = 0;
		std::uint32_t m_LineBreaks = 0;
		std::uint32_t m_Elements = 0;
	};

	// Prepared UTF-16 input is borrowed only during construction. The returned elements own their text.
	[[nodiscard]] std::vector<IndicatorTextElement> PrepareIndicatorText(std::span<const std::uint16_t> code_units, std::uint32_t default_color = 0xffffffffu, bool recognize_tags = false);
	// Glyph metrics are supplied by the actual font provider. This is the fixed Indicator path, without wrapping.
	[[nodiscard]] resource::GlyphSize MeasureIndicatorText(std::span<const IndicatorMeasuredElement> elements, resource::GlyphSize spacing = {});

} // namespace sgre::text
