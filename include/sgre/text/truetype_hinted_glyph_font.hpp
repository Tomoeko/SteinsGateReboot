#pragma once

#include "sgre/text/truetype_glyph_font.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace sgre::text {

	struct TrueTypeHintedGlyph {
		std::uint16_t GlyphIndex = 0;
		std::uint16_t PixelSize = 0;
		std::vector<resource::TrueTypePoint> Points;
		// Retains native touch/scan bits separately from quadratic curve kind.
		std::vector<std::uint8_t> Tags;
		std::vector<std::uint16_t> ContourEnds;
		resource::TrueTypeBounds ControlBounds;
		std::array<std::array<std::int32_t, 2>, 4> Phantoms{};
		TrueTypeGlyphMetrics Metrics;
		std::uint32_t OutlineFlags = 0;
	};

	class TrueTypeHintedGlyphFont {
	public:
		// The immutable SFNT and its bytes outlive this immovable owner. Each font
		// retains its own definitions, size GS, CVT, storage and twilight arrays.
		explicit TrueTypeHintedGlyphFont(const resource::SfntFont& font);
		~TrueTypeHintedGlyphFont();
		TrueTypeHintedGlyphFont(const TrueTypeHintedGlyphFont&) = delete;
		TrueTypeHintedGlyphFont& operator=(const TrueTypeHintedGlyphFont&) = delete;
		TrueTypeHintedGlyphFont(TrueTypeHintedGlyphFont&&) = delete;
		TrueTypeHintedGlyphFont& operator=(TrueTypeHintedGlyphFont&&) = delete;

		// Invalidates prepared size state even when no hinted glyph is requested
		// between two size changes. Preparation remains lazy until PrepareGlyph.
		void SetPixelSize(std::int32_t pixel_size);
		// An applied outer font size invalidates native preparation even when the
		// next glyph returns to the previous size. This does not admit that size.
		void InvalidateSize();
		// Default native version40, horizontal flags8, positive integral 72-dpi
		// sizes. Unsupported programs/transforms remain explicit errors.
		[[nodiscard]] TrueTypeHintedGlyph PrepareGlyph(std::uint32_t glyph_index, std::int32_t pixel_size);

	private:
		struct Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::text
