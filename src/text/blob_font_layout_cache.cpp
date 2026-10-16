#include "sgre/text/blob_font_layout_cache.hpp"

#include <new>

namespace sgre::text {

	template <typename Blob>
	static void AdmitFaceTable(BlobFontFace& face, std::uint32_t tag, FontFaceTableBlob& pin, std::optional<Blob>& blob) {
		pin = face.ReferenceTable(tag);
		blob.emplace(pin.Bytes(), pin.IsImmutable());
		if (blob->Bytes().empty() || blob->Bytes().data() != pin.Bytes().data()) {
			pin = {};
		}
	}

	BlobFontLayoutCache::BlobFontLayoutCache(ConstructionKey, BlobFontFace& face) {
		AdmitFaceTable(face, 0x47444546, m_Definition.Pin, m_Definition.Admitted);
		AdmitFaceTable(face, 0x47535542, m_Substitution.Pin, m_Substitution.Admitted);
		AdmitFaceTable(face, 0x47504f53, m_Positioning.Pin, m_Positioning.Admitted);
		m_Definition.Admitted->ApplyLayoutSuppression(m_Substitution.Admitted->Length(), m_Positioning.Admitted->Length());

		const auto substitution_count = SubstitutionLookupCount(*m_Substitution.Admitted);
		const auto positioning_count = PositioningLookupCount(*m_Positioning.Admitted);
		bool substitution_allocated = false;
		bool positioning_allocated = false;
		try {
			m_SubstitutionLookups.resize(substitution_count);
			substitution_allocated = true;
		} catch (const std::bad_alloc&) {
		}
		try {
			m_PositioningLookups.resize(positioning_count);
			positioning_allocated = true;
		} catch (const std::bad_alloc&) {
		}
		if (!substitution_allocated || !positioning_allocated) {
			return;
		}

		InitializeSubstitutionLookupAccelerators(*m_Substitution.Admitted, m_SubstitutionLookups);
		InitializePositioningLookupAccelerators(*m_Positioning.Admitted, m_PositioningLookups);
		m_Complete = true;
	}

	bool BlobFontLayoutCache::IsComplete() const {
		return m_Complete;
	}

	const GlyphDefinitionBlob& BlobFontLayoutCache::Definition() const {
		return *m_Definition.Admitted;
	}

	const GlyphSubstitutionBlob& BlobFontLayoutCache::Substitution() const {
		return *m_Substitution.Admitted;
	}

	const GlyphPositioningBlob& BlobFontLayoutCache::Positioning() const {
		return *m_Positioning.Admitted;
	}

	std::span<const LayoutLookupAccelerator> BlobFontLayoutCache::SubstitutionLookups() const {
		return m_SubstitutionLookups;
	}

	std::span<const LayoutLookupAccelerator> BlobFontLayoutCache::PositioningLookups() const {
		return m_PositioningLookups;
	}

} // namespace sgre::text
