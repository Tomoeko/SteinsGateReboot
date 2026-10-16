#pragma once

#include "sgre/resource/font.hpp"
#include "sgre/text/indicator_text.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace sgre::scene {

	struct BitmapConsoleState {
		resource::GlyphSize Extent;
		resource::GlyphSize Cursor;
		resource::GlyphSize Drawn;
		resource::GlyphSize Spacing;
		float LineBottom = 0;
		float FontSpeed = 0;
		float AdvanceBudget = 0;
		float ScrollSpeed = 0;
		float ScrollRemaining = 0;
		float ProhibitionWidth = 0;
		float HyphenWidth = 0;
		float FontScale = 1;
		std::uint32_t FontColor = 0xffffffff;
		std::uint32_t Opacity = 255;
		std::uint16_t PreviousCodeUnit = 0;
		std::int32_t Alignment = 0;
		bool Running = true;
		bool EraseLeftEndSpace = false;
		bool WarnOverflow = false;
		bool DrawAtIntegerCoordinates = false;
		bool RecognizeTags = false;
	};

	class BitmapConsole {
	public:
		// Metadata belongs to the actual bitmap provider; the containing owner retains its real font textures.
		BitmapConsole(std::shared_ptr<const resource::BitmapFont> font, resource::GlyphSize extent, std::size_t maximum_elements = 65536);
		void Print(std::string_view utf8);
		// Filter receives the actual popped element before layout. True consumes it, matching SQConsoleFilter.
		// Finite glyph wrapping uses the native initially empty word/prohibition sets.
		// Nonempty shared membership has no admitted owner/API; scrolling retirement remains unsupported.
		void Update(float actual_frame_delta, const std::function<bool(const text::IndicatorTextElement&)>& filter = {});
		void ClearDisplayed();
		void PushPending(text::IndicatorTextElement element);
		[[nodiscard]] text::IndicatorTextElement PopPending();
		void SetExtent(resource::GlyphSize extent);
		void MakeEmpty();
		void StorePage();
		void RestorePage(std::size_t index);
		void SetFontSpeed(float speed);
		void SetScrollSpeed(float speed);
		void SetProhibitionWidth(float width);
		void SetFontScale(float scale);
		void SetFontColor(std::uint32_t color) noexcept;
		void SetOpacity(std::uint32_t opacity) noexcept;
		void SetSpacing(resource::GlyphSize spacing);
		void SetAlignment(std::int32_t alignment) noexcept;
		void SetRecognizeTags(bool enabled) noexcept;
		void SetEraseLeftEndSpace(bool enabled) noexcept;
		void SetWarnOverflow(bool enabled) noexcept;
		void SetDrawAtIntegerCoordinates(bool enabled) noexcept;
		[[nodiscard]] const BitmapConsoleState& State() const noexcept;
		[[nodiscard]] resource::GlyphSize AlignedCursor() const noexcept;
		[[nodiscard]] std::span<const text::IndicatorTextElement> Displayed() const noexcept;
		[[nodiscard]] std::size_t PendingCount() const noexcept;
		[[nodiscard]] std::size_t PageCount() const noexcept;

	private:
		void RequireForwardLayout(const text::IndicatorTextElement& element) const;
		void WrapDefaultLine(const resource::GlyphSize& cell);
		void AppendNewline(text::IndicatorTextElement&& element);
		void RequireCanAppend() const;
		std::shared_ptr<const resource::BitmapFont> m_Font;
		BitmapConsoleState m_State;
		std::deque<text::IndicatorTextElement> m_Pending;
		std::vector<text::IndicatorTextElement> m_Displayed;
		std::vector<std::vector<text::IndicatorTextElement>> m_Pages;
		std::size_t m_MaximumElements = 0;
		std::size_t m_PageElements = 0;
		bool m_Updating = false;
	};

} // namespace sgre::scene
