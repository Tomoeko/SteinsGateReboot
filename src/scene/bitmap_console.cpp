#include "sgre/scene/bitmap_console.hpp"

#include "sgre/text/encoding.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace sgre::scene {

	static void RequireFiniteConsole(float value) {
		if (!std::isfinite(value)) {
			throw std::runtime_error("bitmap Console requires finite state and frame arithmetic");
		}
	}

	static std::uint32_t MultiplyConsoleColor(std::uint32_t left, std::uint32_t right) noexcept {
		std::uint32_t result = 0;
		for (std::uint32_t shift = 0; shift != 32; shift += 8) {
			const auto product = ((left >> shift) & 255) * ((right >> shift) & 255);
			result |= ((product + 1 + (product >> 8)) >> 8) << shift;
		}

		return result;
	}

	BitmapConsole::BitmapConsole(std::shared_ptr<const resource::BitmapFont> font, resource::GlyphSize extent, std::size_t maximum_elements) : m_Font(std::move(font)), m_MaximumElements(maximum_elements) {
		if (!m_Font || maximum_elements == 0 || maximum_elements > 65536) {
			throw std::runtime_error("bitmap Console requires actual font metadata and bounded element storage");
		}
		RequireFiniteConsole(extent.Width);
		RequireFiniteConsole(extent.Height);
		RequireFiniteConsole(m_Font->MaxWidth);
		RequireFiniteConsole(m_Font->MaxHeight);
		m_State.Extent = extent;
		m_State.HyphenWidth = resource::MeasureBitmapGlyph(m_Font->FindGlyphCodeUnit(0x2d), 1, 1).Width;
		m_Displayed.reserve(maximum_elements);
	}

	void BitmapConsole::Print(std::string_view utf8) {
		utf8 = utf8.substr(0, utf8.find('\0'));
		const auto units = text::DecodeTextCodeUnits(utf8);
		// Console and Indicator share original533620/5336e0; Console color is applied later at consumption.
		auto elements = text::PrepareIndicatorText(units, 0xffffffff, m_State.RecognizeTags);
		if (elements.size() > m_MaximumElements - m_Pending.size()) {
			throw std::runtime_error("bitmap Console pending text exceeds its prepared element bound");
		}
		for (auto& element : elements) {
			m_Pending.push_back(std::move(element));
		}
		m_State.Running = true;
	}

	void BitmapConsole::RequireCanAppend() const {
		if (m_Displayed.size() == m_MaximumElements) {
			throw std::runtime_error("bitmap Console displayed text exceeds its prepared element bound");
		}
	}

	void BitmapConsole::RequireForwardLayout(const text::IndicatorTextElement& element) const {
		if (m_State.Spacing.Width < 0 || m_Font->MaxWidth < 0 || m_Font->MaxHeight < 0) {
			throw std::runtime_error("bitmap Console negative cell/spacing wrapping requires its recovered layout owner");
		}
		const auto captured_scale = element.Scale * m_State.FontScale;
		RequireFiniteConsole(captured_scale);
		if (element.Scale < 0 || captured_scale < 0) {
			throw std::runtime_error("bitmap Console negative element scale requires its recovered wrapping owner");
		}
	}

	void BitmapConsole::WrapDefaultLine(const resource::GlyphSize& cell) {
		const auto width = (cell.Width + m_State.Cursor.Width) + m_State.ProhibitionWidth;
		RequireFiniteConsole(width);
		if (width <= m_State.Extent.Width) {
			return;
		}

		// Native52f657..52f8b7 reduces to this branch while both global membership sets remain empty.
		// One newline is emitted even for a wider-than-view glyph at X=0; the glyph is not scaled or retried.
		m_State.Cursor.Width = 0;
		m_State.Cursor.Height = m_State.LineBottom + m_State.Spacing.Height;
		RequireFiniteConsole(m_State.Cursor.Height);
		m_State.PreviousCodeUnit = 0;
		text::IndicatorTextElement newline;
		newline.CodeUnits.push_back(0x0a);
		AppendNewline(std::move(newline));
	}

	void BitmapConsole::AppendNewline(text::IndicatorTextElement&& element) {
		RequireCanAppend();
		element.Color = 0xffffffff;
		element.Scale = 1;
		m_Displayed.push_back(std::move(element));
	}

	void BitmapConsole::Update(float actual_frame_delta, const std::function<bool(const text::IndicatorTextElement&)>& filter) {
		if (m_Updating) {
			throw std::runtime_error("bitmap Console update cannot reenter its actual element consumption");
		}
		RequireFiniteConsole(actual_frame_delta);
		if (m_State.ScrollRemaining > 0) {
			throw std::runtime_error("bitmap Console scrolling requires its actual line retirement owner");
		}
		m_State.AdvanceBudget -= actual_frame_delta * m_State.FontSpeed;
		RequireFiniteConsole(m_State.AdvanceBudget);
		std::size_t remaining_work = 1048576;
		m_Updating = true;
		try {
			while (m_State.FontSpeed == 0 || !(0 <= m_State.AdvanceBudget)) {
				if (remaining_work == 0) {
					throw std::runtime_error("bitmap Console exceeds its bounded update work");
				}
				--remaining_work;
				if (m_Pending.empty()) {
					m_State.Running = false;
					break;
				}
				auto element = std::move(m_Pending.front());
				m_Pending.pop_front();
				if (filter && filter(element)) {
					continue;
				}
				if (element.CodeUnits.empty()) {
					continue;
				}
				if (element.IsLineBreak()) {
					if (m_State.Cursor.Width != 0) {
						m_State.PreviousCodeUnit = 0;
						m_State.Cursor.Width = 0;
						m_State.Cursor.Height = m_State.LineBottom + m_State.Spacing.Height;
						RequireFiniteConsole(m_State.Cursor.Height);
						AppendNewline(std::move(element));
					}
					continue;
				}
				if (element.CodeUnits.size() == 1 && element.CodeUnits.front() == 0x0c) {
					m_State.PreviousCodeUnit = 0;
					m_State.Running = false;
					break;
				}
				if (element.CodeUnits.front() < 0x20) {
					continue;
				}
				RequireForwardLayout(element);
				element.Color = MultiplyConsoleColor(m_State.FontColor, element.Color);
				element.Scale *= m_State.FontScale;
				RequireFiniteConsole(element.Scale);
				// Native534de0 preserves R8=element through the font virtual+10 call; PSBFont resolves its first glyph nominal metrics.
				const auto cell = resource::MeasureBitmapGlyph(m_Font->FindGlyphCodeUnit(element.CodeUnits.front()), element.Scale, element.Scale);
				RequireFiniteConsole(cell.Width);
				RequireFiniteConsole(cell.Height);
				WrapDefaultLine(cell);
				const auto bottom = cell.Height + m_State.Cursor.Height;
				RequireFiniteConsole(bottom);
				if (!(bottom <= m_State.LineBottom)) {
					m_State.LineBottom = bottom;
				}
				if (bottom > m_State.Extent.Height) {
					m_Pending.push_front(std::move(element));
					if (m_State.ScrollSpeed != 0) {
						m_State.ScrollRemaining = m_State.LineBottom - m_State.Extent.Height;
						m_State.AdvanceBudget = 0;
					} else {
						m_State.Running = false;
					}
					break;
				}
				if (m_State.EraseLeftEndSpace && element.CodeUnits.size() == 1 && element.CodeUnits.front() == 0x20 && m_State.Cursor.Width == 0) {
					m_State.PreviousCodeUnit = 0x20;
					continue;
				}
				RequireCanAppend();
				m_State.PreviousCodeUnit = element.CodeUnits.front();
				m_Displayed.push_back(std::move(element));
				m_State.Drawn.Width = std::max(m_State.Cursor.Width + cell.Width, m_State.Drawn.Width);
				m_State.Drawn.Height = std::max(bottom, m_State.Drawn.Height);
				m_State.Cursor.Width = cell.Width + m_State.Spacing.Width + m_State.Cursor.Width;
				m_State.AdvanceBudget = cell.Width + m_State.AdvanceBudget;
				RequireFiniteConsole(m_State.Cursor.Width);
				RequireFiniteConsole(m_State.AdvanceBudget);
			}
			m_Updating = false;
		} catch (...) {
			m_Updating = false;
			throw;
		}
	}

	void BitmapConsole::PushPending(text::IndicatorTextElement element) {
		if (m_Pending.size() == m_MaximumElements) {
			throw std::runtime_error("bitmap Console pending text exceeds its prepared element bound");
		}

		m_Pending.push_front(std::move(element));
	}

	text::IndicatorTextElement BitmapConsole::PopPending() {
		if (m_Pending.empty()) {
			throw std::runtime_error("console is empty");
		}

		auto element = std::move(m_Pending.front());
		m_Pending.pop_front();

		return element;
	}

	void BitmapConsole::SetExtent(resource::GlyphSize extent) {
		RequireFiniteConsole(extent.Width);
		RequireFiniteConsole(extent.Height);
		m_State.Extent = extent;
	}

	void BitmapConsole::ClearDisplayed() {
		m_State.Cursor = {};
		m_State.Drawn = {};
		m_State.LineBottom = 0;
		m_State.AdvanceBudget = 0;
		m_State.ScrollRemaining = 0;
		m_State.PreviousCodeUnit = 0;
		m_State.Running = true;
		m_Displayed.clear();
	}

	void BitmapConsole::MakeEmpty() {
		m_State.PreviousCodeUnit = 0;
		m_Pending.clear();
	}

	void BitmapConsole::StorePage() {
		if (m_Pages.size() == m_MaximumElements || m_Displayed.size() > m_MaximumElements - m_PageElements) {
			throw std::runtime_error("bitmap Console stored pages exceed their total prepared element bound");
		}
		m_Pages.push_back(m_Displayed);
		m_PageElements += m_Displayed.size();
	}

	void BitmapConsole::RestorePage(std::size_t index) {
		if (index >= m_Pages.size()) {
			throw std::runtime_error("bitmap Console requested page exceeds its actual stored pages");
		}
		// Native52fd80 replaces only the displayed list; cursor, pending text and task state are retained.
		m_Displayed = m_Pages[index];
	}

	void BitmapConsole::SetFontSpeed(float speed) {
		RequireFiniteConsole(speed);
		if (speed != m_State.FontSpeed) {
			m_State.FontSpeed = speed;
			m_State.AdvanceBudget = 0;
		}
	}

	void BitmapConsole::SetScrollSpeed(float speed) {
		RequireFiniteConsole(speed);
		m_State.ScrollSpeed = speed;
	}

	void BitmapConsole::SetProhibitionWidth(float width) {
		RequireFiniteConsole(width);
		m_State.ProhibitionWidth = width;
	}

	void BitmapConsole::SetFontScale(float scale) {
		RequireFiniteConsole(scale);
		m_State.FontScale = scale;
	}

	void BitmapConsole::SetFontColor(std::uint32_t color) noexcept {
		m_State.FontColor = color;
	}
	void BitmapConsole::SetOpacity(std::uint32_t opacity) noexcept {
		m_State.Opacity = opacity;
	}
	void BitmapConsole::SetSpacing(resource::GlyphSize spacing) {
		RequireFiniteConsole(spacing.Width);
		RequireFiniteConsole(spacing.Height);
		m_State.Spacing = spacing;
	}
	void BitmapConsole::SetAlignment(std::int32_t alignment) noexcept {
		m_State.Alignment = alignment;
	}
	void BitmapConsole::SetRecognizeTags(bool enabled) noexcept {
		m_State.RecognizeTags = enabled;
	}
	void BitmapConsole::SetEraseLeftEndSpace(bool enabled) noexcept {
		m_State.EraseLeftEndSpace = enabled;
	}
	void BitmapConsole::SetWarnOverflow(bool enabled) noexcept {
		m_State.WarnOverflow = enabled;
	}
	void BitmapConsole::SetDrawAtIntegerCoordinates(bool enabled) noexcept {
		m_State.DrawAtIntegerCoordinates = enabled;
	}
	const BitmapConsoleState& BitmapConsole::State() const noexcept {
		return m_State;
	}
	std::span<const text::IndicatorTextElement> BitmapConsole::Displayed() const noexcept {
		return m_Displayed;
	}
	std::size_t BitmapConsole::PendingCount() const noexcept {
		return m_Pending.size();
	}
	std::size_t BitmapConsole::PageCount() const noexcept {
		return m_Pages.size();
	}

	resource::GlyphSize BitmapConsole::AlignedCursor() const noexcept {
		auto cursor = m_State.Cursor;
		if (m_State.Alignment == 1) {
			cursor.Width = (m_State.Extent.Width - cursor.Width) * 0.5f + cursor.Width;
			cursor.Height += 0.0f;
		} else if (m_State.Alignment == 2) {
			cursor.Width = m_State.Extent.Width - cursor.Width + cursor.Width;
			cursor.Height += 0.0f;
		}

		return cursor;
	}

} // namespace sgre::scene
