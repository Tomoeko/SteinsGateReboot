#include "sgre/text/render_context.hpp"

namespace sgre::text {

	TextRenderContext::TextRenderContext(scene::MutableTextureBackend& backend, bool filtering)
	    : m_Atlas(backend) {
		m_Atlas.SetFiltering(filtering);
	}

	const TextRenderContextState& TextRenderContext::State() const noexcept {
		return m_State;
	}

	TextGlyphAtlasOwner& TextRenderContext::Atlas() noexcept {
		return m_Atlas;
	}

	const TextGlyphAtlasOwner& TextRenderContext::Atlas() const noexcept {
		return m_Atlas;
	}

	const TextFormattingState& TextRenderContext::Formatting() const noexcept {
		return m_Formatting;
	}

	const TextFaceNames& TextRenderContext::FaceNames() const noexcept {
		return m_FaceNames;
	}

	void TextRenderContext::ApplyDefaults(const TextPropertyReader& reader) {
		auto owned_reader = reader;
		owned_reader.InternFace = [this](std::span<const std::uint16_t> name) {
			return m_FaceNames.InternCodeUnits(name);
		};
		ApplyTextDefaultProperties(m_Formatting.Defaults, owned_reader);
	}

	void TextRenderContext::ApplyFontProperties(const TextPropertyReader& reader, const TextFontResetCallbacks& callbacks) {
		auto owned_reader = reader;
		owned_reader.InternFace = [this](std::span<const std::uint16_t> name) {
			return m_FaceNames.InternCodeUnits(name);
		};
		ApplyTextFontProperties(m_Formatting, owned_reader, callbacks);
	}

	void TextRenderContext::ResetFont(const TextFontResetCallbacks& callbacks) {
		ResetTextFontDefaults(m_Formatting, callbacks);
	}

	void TextRenderContext::ResetStyle() {
		ResetTextGeometryDefaults(m_Formatting);
	}

	void TextRenderContext::ClearEmpty(const TextFontResetCallbacks& callbacks) {
		// Native02ef50 retires the current line first, then resets fonts/style while old history is still observable.
		m_Content.BeginClear({m_Formatting.Vertical ? m_State.RenderSize.X : 0.0f, 0});
		try {
			ResetFont(callbacks);
			ResetTextGeometryDefaults(m_Formatting);
		} catch (...) {
			m_Content.CancelClear();
			throw;
		}
		m_Content.FinishClear();
		// Native036030 clears these only after font callbacks; coordinate and clip offset persist.
		m_State.ClippingBounds = {1, 1, -1, -1};
		m_State.ShownElementCount = 0;
		m_State.OffsetPosition = {};
		m_State.ScrollOffset = {};
		if (!m_AtlasCleared) {
			if (callbacks.ClearPreparedFonts) {
				callbacks.ClearPreparedFonts();
			}
			m_Atlas.ResetPacking();
			// Commit after the private live-owner guard; no callback separates packing stores from this flag.
			m_AtlasCleared = true;
		}
	}

	void TextRenderContext::SetRenderSize(TextCursor size, const TextFontResetCallbacks& callbacks) {
		m_State.RenderSize = size;
		ClearEmpty(callbacks);
	}

	const TextProcessingOptions& TextRenderContext::Options() const noexcept {
		return m_Options;
	}

	void TextRenderContext::ApplyOptions(const TextPropertyReader& reader) {
		auto ordered_reader = reader;
		if (reader.ContainsRaw) {
			ordered_reader.ContainsRaw = [&](std::string_view key) {
				// Keep the facade's view current before any later property callback can observe the native flag.
				m_State.Vertical = m_Formatting.Vertical;

				return reader.ContainsRaw(key);
			};
		}

		try {
			ApplyTextOptionProperties(m_Formatting, m_Options, ordered_reader);
		} catch (...) {
			m_State.Vertical = m_Formatting.Vertical;
			throw;
		}

		m_State.Vertical = m_Formatting.Vertical;
	}

	void TextRenderContext::SetVertical(bool enabled) noexcept {
		m_Formatting.Vertical = enabled;
		m_State.Vertical = enabled;
	}

	void TextRenderContext::SetTimeScale(float scale) noexcept {
		m_State.TimeScale = scale;
	}

	void TextRenderContext::SetFontScale(float scale) noexcept {
		m_State.FontScale = scale;
		m_Formatting.FontScale = scale;
	}

	void TextRenderContext::SetShownElementCount(std::uint32_t count) noexcept {
		m_State.ShownElementCount = count;
	}

	void TextRenderContext::SetClipMode(std::int32_t mode) noexcept {
		// The native setter stores only; coordinate/clip-offset changes or update recompute the clip later.
		m_State.ClipMode = mode;
	}

	void TextRenderContext::SetCoordinate(TextCursor coordinate) {
		m_State.Coordinate = coordinate;
		RecomputeClipping();
	}

	void TextRenderContext::SetClipOffset(TextCursor offset) {
		m_State.ClipOffset = offset;
		RecomputeClipping();
	}

	void TextRenderContext::SetOffsetPosition(TextCursor offset) noexcept {
		m_State.OffsetPosition = offset;
	}

	void TextRenderContext::RecomputeClipping() {
		if (m_State.ClipMode != 1 && m_State.ClipMode != 2) {
			m_State.ClippingBounds = {1, 1, -1, -1};

			return;
		}

		const auto x = m_State.ClipOffset.X + m_State.Coordinate.X;
		const auto y = m_State.ClipOffset.Y + m_State.Coordinate.Y;
		const auto bounds = m_Content.Bounds();
		m_State.ClippingBounds.Left = m_State.ClipMode == 1 ? x + bounds.Left : x;
		m_State.ClippingBounds.Top = m_State.ClipMode == 1 ? y + bounds.Top : y;
		m_State.ClippingBounds.Right = m_State.ClipMode == 1 ? x + bounds.Right : m_State.Size.X + x;
		m_State.ClippingBounds.Bottom = m_State.Size.Y + y;
	}

	void TextRenderContext::FinalizeEmpty() {
		m_Content.CaptureRenderCursor();
		m_State.ShownElementCount = 0;
		// Native0360c0 uses measured empty bounds only for ordered-zero requested axes.
		m_State.Size = {m_State.RenderSize.X != 0 ? m_State.RenderSize.X : 0.0f, m_State.RenderSize.Y != 0 ? m_State.RenderSize.Y : 0.0f};
		RecomputeClipping();
		// Original 0360c0 computes the clip before clearing these three independent coordinate pairs.
		m_State.OffsetPosition = {};
		m_State.ClipOffset = {};
		m_State.ScrollOffset = {};
	}

	bool TextRenderContext::Render(std::span<const std::uint16_t> input, const TextContentRequest& request, const TextContentCallbacks& callbacks) {
		// Native036c20 rearms Clear before parsing, including empty input and later parser failures.
		m_AtlasCleared = false;
		return m_Content.Render(input, m_Formatting, m_Options, m_State.RenderSize, request, callbacks);
	}

	const TextContentOwner& TextRenderContext::Content() const noexcept {
		return m_Content;
	}

	void TextRenderContext::Finalize() {
		if (m_Content.Lines().empty() && m_Content.CurrentLine().empty() && m_Content.RenderedElementCount() == 0) {
			FinalizeEmpty();
			return;
		}
		m_Content.Finalize(m_Formatting, m_Options, m_State.RenderSize);
		const auto bounds = m_Content.Bounds();
		// Native0360c0 copies core+254 into task+3cc, including the incoming element of an overflow failure.
		m_State.ShownElementCount = m_Content.RenderedElementCount();
		m_State.Size = {m_State.RenderSize.X != 0 ? m_State.RenderSize.X : bounds.Right - bounds.Left, m_State.RenderSize.Y != 0 ? m_State.RenderSize.Y : bounds.Bottom - bounds.Top};
		RecomputeClipping();
		m_State.OffsetPosition = {};
		m_State.ClipOffset = {};
		m_State.ScrollOffset = {};
	}

} // namespace sgre::text
