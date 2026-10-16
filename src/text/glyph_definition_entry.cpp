#include "sgre/text/recursive_decomposition.hpp"

#include "sgre/text/glyph_definition_blob.hpp"

#include <stdexcept>

namespace sgre::text {

	void UnicodeNormalizationBuffer::InitializeGlyphDefinitionProperties(const GlyphDefinitionBlob& definition, NormalizedGlyphContent content) {
		if (content != NormalizedGlyphContent::GlyphIndices) {
			throw std::runtime_error("GDEF record entry requires the caller's published glyph-index content");
		}

		for (auto& glyph : std::span<SubstitutionGlyph>(m_Storage[m_InputStorage]).first(m_State.InputCount)) {
			glyph.GlyphProperties = definition.GlyphProperties(glyph.GlyphIndex);
			glyph.PrivateState = 0;
		}
		if (!definition.HasGlyphClasses()) {
			for (auto& glyph : std::span<SubstitutionGlyph>(m_Storage[m_InputStorage]).first(m_State.InputCount)) {
				const auto mark = (glyph.CharacterState & 0x1fU) == 12 && ((glyph.CharacterState & 0x20U) == 0 || (glyph.GlyphProperties & 0x20U) != 0);
				glyph.GlyphProperties = mark ? 8 : 2;
			}
		}
	}

} // namespace sgre::text
