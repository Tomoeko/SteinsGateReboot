#pragma once

#include "sgre/text/blob_font_face.hpp"
#include "sgre/text/glyph_definition_blob.hpp"
#include "sgre/text/layout_lookup_accelerator.hpp"

#include <optional>
#include <span>
#include <vector>

namespace sgre::text {

	// Face-owned initial layout cache. Later auxiliary table caches remain unsupported.
	class BlobFontLayoutCache {
	public:
		class ConstructionKey {
		private:
			friend class BlobFontFace;
			ConstructionKey() = default;
		};
		explicit BlobFontLayoutCache(ConstructionKey, BlobFontFace& face);
		BlobFontLayoutCache(const BlobFontLayoutCache&) = delete;
		BlobFontLayoutCache& operator=(const BlobFontLayoutCache&) = delete;
		BlobFontLayoutCache(BlobFontLayoutCache&&) = delete;
		BlobFontLayoutCache& operator=(BlobFontLayoutCache&&) = delete;
		[[nodiscard]] const GlyphDefinitionBlob& Definition() const;
		[[nodiscard]] const GlyphSubstitutionBlob& Substitution() const;
		[[nodiscard]] const GlyphPositioningBlob& Positioning() const;
		[[nodiscard]] std::span<const LayoutLookupAccelerator> SubstitutionLookups() const;
		[[nodiscard]] std::span<const LayoutLookupAccelerator> PositioningLookups() const;

	private:
		friend class BlobFontFace;
		[[nodiscard]] bool IsComplete() const;
		template <typename Blob>
		struct AdmittedFaceTable {
			FontFaceTableBlob Pin;
			std::optional<Blob> Admitted;
		};
		// Reverse member destruction follows native arrays, then GDEF/GSUB/GPOS blob release.
		AdmittedFaceTable<GlyphPositioningBlob> m_Positioning;
		AdmittedFaceTable<GlyphSubstitutionBlob> m_Substitution;
		AdmittedFaceTable<GlyphDefinitionBlob> m_Definition;
		std::vector<LayoutLookupAccelerator> m_PositioningLookups;
		std::vector<LayoutLookupAccelerator> m_SubstitutionLookups;
		bool m_Complete = false;
	};

} // namespace sgre::text
