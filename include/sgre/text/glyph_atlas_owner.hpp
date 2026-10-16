#pragma once

#include "sgre/scene/mutable_texture_owner.hpp"
#include "sgre/text/cff_glyph_atlas.hpp"

#include <cstdint>
#include <memory>
#include <optional>

namespace sgre::text {

	struct TextGlyphAtlasState {
		GlyphAtlasPacking Packing;
		std::optional<scene::MutableTextureState> Texture;
		bool Filtering = false;
		bool Closed = false;
	};

	class TextGlyphAtlasOwner {
	public:
		// The backend outlives this text context. Every cached font interface retires before this owner.
		explicit TextGlyphAtlasOwner(scene::MutableTextureBackend& backend);
		~TextGlyphAtlasOwner();
		TextGlyphAtlasOwner(const TextGlyphAtlasOwner&) = delete;
		TextGlyphAtlasOwner& operator=(const TextGlyphAtlasOwner&) = delete;
		TextGlyphAtlasOwner(TextGlyphAtlasOwner&&) = delete;
		TextGlyphAtlasOwner& operator=(TextGlyphAtlasOwner&&) = delete;

		[[nodiscard]] TextGlyphAtlasState State() const;
		void SetFiltering(bool enabled);
		// Lazy fixed 1024-square BGRA atlas. Full mappings return no region; this does not grow pages.
		[[nodiscard]] std::optional<CffAtlasGlyph> PrepareDefaultCffGlyph(const CffPreparedGlyph& glyph);
		// Borrows fully prepared coverage; all contour expansion/raster allocation precedes the atlas map.
		[[nodiscard]] std::optional<CffAtlasGlyph> PrepareCffRaster(const PreparedCffGlyphRaster& raster);
		// Upload/binding remains explicit; preparation never performs a GPU bind or readback.
		[[nodiscard]] bool Bind(std::uint32_t slot, scene::MutableTextureStage stage);
		[[nodiscard]] const scene::MutableTextureResource* Resource() const;
		// Synchronous renderer borrow; cached regions and this owner enclose the containing draw.
		[[nodiscard]] scene::MutableTextureOwner& Texture();
		void Close();

	private:
		friend class TextRenderContext;
		// Only the containing Clear handshake may reuse packing after its private font regions retire.
		void ResetPacking();
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::text
