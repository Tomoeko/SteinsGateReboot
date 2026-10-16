#pragma once

#include "sgre/resource/font.hpp"
#include "sgre/scene/indicator_placement.hpp"
#include "sgre/text/indicator_text.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace sgre::scene {

	class BitmapIndicator {
	public:
		// This CPU owner retains validated font metadata. Its caller retains the PSB and physical font textures.
		explicit BitmapIndicator(resource::BitmapFont font);
		// Immutable metadata is shared with its actual loaded asset; per-Indicator style/text remain owned here.
		explicit BitmapIndicator(std::shared_ptr<const resource::BitmapFont> font);
		void Print(std::string_view utf8);
		void SetFontScale(resource::GlyphSize scale);
		void SetUniformFontScale(float scale);
		void SetFontScaleX(float scale);
		void SetFontScaleY(float scale);
		void SetFontColor(std::uint32_t color);
		void SetRecognizeTags(bool enabled);
		void SetSpacing(resource::GlyphSize spacing);
		void SetSpacingX(float spacing);
		void SetSpacingY(float spacing);
		void SetOpacity(std::uint32_t opacity);
		void SetAlignment(std::int32_t alignment);
		void SetDrawAtIntegerCoordinates(bool enabled);
		[[nodiscard]] resource::GlyphSize FontScale() const noexcept;
		[[nodiscard]] resource::GlyphSize MaximumFontSize() const noexcept;
		[[nodiscard]] std::uint32_t FontColor() const noexcept;
		[[nodiscard]] bool RecognizeTags() const noexcept;
		[[nodiscard]] resource::GlyphSize Spacing() const noexcept;
		[[nodiscard]] std::uint32_t Opacity() const noexcept;
		[[nodiscard]] std::int32_t Alignment() const noexcept;
		[[nodiscard]] bool DrawAtIntegerCoordinates() const noexcept;
		// Returned elements borrow this owner until its next successful print or destruction. Detached views return no elements.
		[[nodiscard]] std::span<const text::IndicatorTextElement> Elements() const noexcept;
		[[nodiscard]] IndicatorPlacement& Placement() noexcept;
		[[nodiscard]] const IndicatorPlacement& Placement() const noexcept;

	private:
		[[nodiscard]] resource::GlyphSize Measure(std::span<const text::IndicatorTextElement> elements, resource::GlyphSize scale, resource::GlyphSize spacing) const;

		std::shared_ptr<const resource::BitmapFont> m_Font;
		IndicatorPlacement m_Placement;
		std::vector<text::IndicatorTextElement> m_Elements;
		resource::GlyphSize m_FontScale{1, 1};
		resource::GlyphSize m_MaximumFontSize;
		resource::GlyphSize m_Spacing;
		std::uint32_t m_FontColor = 0xffffffffu;
		std::uint32_t m_Opacity = 255;
		std::int32_t m_Alignment = 0;
		bool m_RecognizeTags = false;
		bool m_DrawAtIntegerCoordinates = false;
	};

} // namespace sgre::scene
