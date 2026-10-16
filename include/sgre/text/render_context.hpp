#pragma once

#include "sgre/text/block_geometry.hpp"
#include "sgre/text/content_owner.hpp"
#include "sgre/text/face_names.hpp"
#include "sgre/text/glyph_atlas_owner.hpp"
#include "sgre/text/layout.hpp"
#include "sgre/text/property_settings.hpp"

#include <cstdint>

namespace sgre::text {

	struct TextRenderContextState {
		TextCursor Coordinate;
		TextCursor OffsetPosition;
		TextCursor ClipOffset;
		TextCursor ScrollOffset;
		TextCursor Size;
		TextCursor RenderSize;
		TextBounds ClippingBounds;
		float TimeScale = 1;
		float FontScale = 1;
		std::uint32_t ShownElementCount = 0;
		std::int32_t ClipMode = 0;
		bool Vertical = false;
	};

	class TextRenderContext {
	public:
		// The platform backend outlives this actual text task and its lazy atlas.
		explicit TextRenderContext(scene::MutableTextureBackend& backend, bool filtering = false);
		[[nodiscard]] const TextRenderContextState& State() const noexcept;
		[[nodiscard]] TextGlyphAtlasOwner& Atlas() noexcept;
		[[nodiscard]] const TextGlyphAtlasOwner& Atlas() const noexcept;
		[[nodiscard]] const TextFormattingState& Formatting() const noexcept;
		[[nodiscard]] const TextFaceNames& FaceNames() const noexcept;
		// Native03f510 updates defaults only; current font/layout state is reset by separate operations.
		void ApplyDefaults(const TextPropertyReader& reader);
		void ApplyFontProperties(const TextPropertyReader& reader, const TextFontResetCallbacks& callbacks);
		void ResetFont(const TextFontResetCallbacks& callbacks);
		void ResetStyle();
		// Native036030 on the actual empty content owner. Populated parser/list retirement remains separate.
		void ClearEmpty(const TextFontResetCallbacks& callbacks);
		// Stores the core request before native clear/font callbacks; fixed-view Size changes only at FinalizeEmpty.
		void SetRenderSize(TextCursor size, const TextFontResetCallbacks& callbacks);
		[[nodiscard]] const TextProcessingOptions& Options() const noexcept;
		// Applies only recovered option storage. This does not parse text, reset fonts or allocate its requested texture size.
		void ApplyOptions(const TextPropertyReader& reader);
		void SetVertical(bool enabled) noexcept;
		void SetTimeScale(float scale) noexcept;
		void SetFontScale(float scale) noexcept;
		void SetShownElementCount(std::uint32_t count) noexcept;
		void SetClipMode(std::int32_t mode) noexcept;
		void SetCoordinate(TextCursor coordinate);
		void SetClipOffset(TextCursor offset);
		void SetOffsetPosition(TextCursor offset) noexcept;
		// Native done() empty finalized-list finalization; populated text application is a separate unrecovered owner operation.
		void FinalizeEmpty();
		[[nodiscard]] bool Render(std::span<const std::uint16_t> input, const TextContentRequest& request, const TextContentCallbacks& callbacks);
		void Finalize();
		[[nodiscard]] const TextContentOwner& Content() const noexcept;

	private:
		void RecomputeClipping();
		TextRenderContextState m_State;
		TextFaceNames m_FaceNames;
		TextFormattingState m_Formatting;
		TextProcessingOptions m_Options;
		TextGlyphAtlasOwner m_Atlas;
		TextContentOwner m_Content;
	};

} // namespace sgre::text
