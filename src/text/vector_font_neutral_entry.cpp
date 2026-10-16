#include "sgre/text/vector_font_normalization_font.hpp"

#include "sgre/text/blob_font_layout_cache.hpp"

#include <stdexcept>

namespace sgre::text {

	NormalizedGlyphContent VectorFontNormalizationFont::PrepareNeutralGlyphs(UnicodeNormalizationBuffer& buffer, const VectorFontFeatureMap& features, NeutralGlyphPreparationContext context, std::span<const VectorFontFeatureRequest> requests) {
		if (!m_Face.PrepareLayout()) {
			throw std::runtime_error("normalization font layout cache failed; native GDEF entry is outside the admitted domain");
		}
		context.FallbackMarkPositioning = !m_Face.HasPositioning();
		const auto content = PrepareNeutralGlyphIndices(buffer, m_Font, features, context, requests);
		buffer.InitializeGlyphDefinitionProperties(m_Face.LayoutCache()->Definition(), content);

		return content;
	}

} // namespace sgre::text
