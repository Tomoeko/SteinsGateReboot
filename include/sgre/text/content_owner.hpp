#pragma once

#include "sgre/text/block_geometry.hpp"
#include "sgre/text/element_draw.hpp"
#include "sgre/text/formatting.hpp"
#include "sgre/text/glyph_run.hpp"
#include "sgre/text/property_settings.hpp"
#include "sgre/text/render_plan.hpp"
#include "sgre/text/reveal.hpp"
#include "sgre/text/wrapping.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace sgre::text {

	struct TextContentRuby {
		std::u16string Text;
		OwnedTextGlyphRun Run;
		// Native offsets are relative to the parent for Draw, but enter block bounds unshifted.
		TextGlyphGeometry Geometry;
	};

	struct TextContentElement {
		std::u16string Text;
		OwnedTextGlyphRun Run;
		TextGlyphGeometry Geometry;
		TextElementDrawStyle Style;
		TextFontSelectionKey Font;
		TextFontSelectionKey RubyFont;
		std::vector<TextContentRuby> Ruby;
		float RevealTime = 0;
		std::int32_t LinkIndex = -1;
	};

	struct TextContentLine {
		std::deque<TextContentElement> Elements;
		TextBounds Bounds;
	};

	struct TextContentRequest {
		// Zero bypasses quote indentation; a negative mode additionally shifts the first opening unit.
		std::int32_t Indent = 0;
		std::int32_t ElementDelay = 0;
		std::int32_t TargetTime = 0;
		bool KeepPreviousTimes = false;
	};

	struct TextContentCallbacks {
		TextFontResetCallbacks Fonts;
		// Executes the actual selected provider and transfers its shaped positions into this owner.
		std::function<OwnedTextGlyphRun(std::uint16_t)> PrepareNormal;
		std::function<OwnedTextGlyphRun(std::u16string_view)> PrepareRuby;
		std::function<float(std::uint16_t)> MeasureRuby;
	};

	class TextContentOwner {
	public:
		// These callbacks are synchronous borrows; retained lines own all text, glyphs, style and geometry.
		[[nodiscard]] bool Render(std::span<const std::uint16_t> input, TextFormattingState& formatting, const TextProcessingOptions& options, TextCursor render_size, const TextContentRequest& request, const TextContentCallbacks& callbacks);
		void Clear();
		void Finalize(const TextFormattingState& formatting, const TextProcessingOptions& options, TextCursor render_size);
		[[nodiscard]] std::span<const TextContentLine> Lines() const noexcept;
		[[nodiscard]] const std::deque<TextContentElement>& CurrentLine() const noexcept;
		[[nodiscard]] const std::u16string& ProcessedText() const noexcept;
		[[nodiscard]] const TextRenderPlan& Plan() const noexcept;
		// Borrows the actual current boundary table, including its final terminal record.
		[[nodiscard]] std::span<const TextRevealBoundary> RevealBoundaries() const noexcept;
		[[nodiscard]] TextBounds Bounds() const noexcept;
		[[nodiscard]] TextCursor Cursor() const noexcept;
		// Native Done snapshots the unaligned current cursor before finalizing the last line.
		[[nodiscard]] TextCursor RenderCursor() const noexcept;
		[[nodiscard]] bool Overflowed() const noexcept;
		[[nodiscard]] std::size_t ElementCount() const noexcept;
		[[nodiscard]] std::uint32_t RenderedElementCount() const noexcept;
		[[nodiscard]] float RenderDelay(float time_scale) const noexcept;
		[[nodiscard]] std::uint32_t CalculateShownElementCount(std::int32_t elapsed_time, float time_scale) const;

	private:
		friend class TextRenderContext;
		void BeginClear(TextCursor initial_cursor = {});
		void CaptureRenderCursor() noexcept;
		void FinishClear();
		void CancelClear() noexcept;
		[[nodiscard]] bool Append(TextContentElement element, const TextFormattingState& formatting, const TextProcessingOptions& options, TextCursor render_size, std::size_t& work, std::size_t depth = 0);
		void Place(TextContentElement element, const TextFormattingState& formatting);
		void ClearPendingRuby() noexcept;
		void SetRubyOperand(std::span<const std::uint16_t> payload);
		[[nodiscard]] bool AppendCodeUnit(std::uint16_t unit, const TextFormattingState& formatting, const TextProcessingOptions& options, TextCursor render_size, const TextContentCallbacks& callbacks, float delay, std::size_t& work);
		[[nodiscard]] bool FinishLine(const TextFormattingState& formatting, const TextProcessingOptions& options, TextCursor render_size);
		void Retime(const TextContentRequest& request, float delay);
		void RebuildPlan();
		std::deque<TextContentElement> m_Current;
		std::vector<TextContentLine> m_History;
		std::u16string m_Processed;
		std::u16string m_RubyText;
		std::u16string m_PendingText;
		std::int32_t m_RubyCountdown = 0;
		TextRenderPlan m_Plan;
		TextBounds m_Bounds;
		TextCursor m_Cursor;
		TextCursor m_RenderCursor;
		TextWrapState m_Wrap;
		TextRevealCursor m_Reveal;
		std::vector<TextRevealBoundary> m_Boundaries;
		std::uint32_t m_SegmentCount = 0;
		std::size_t m_ElementCount = 0;
		std::uint32_t m_RenderedElementCount = 0;
		// Native core+268 retains the greatest completed reveal-segment end.
		float m_RenderDelay = 0;
		// Retained native core+B8; parser nesting is local to each Render and Clear resets this value.
		float m_LineStart = 0;
		bool m_Overflowed = false;
		bool m_Operating = false;
		bool m_Clearing = false;
	};

} // namespace sgre::text
