#include "sgre/text/nominal_glyph.hpp"

namespace sgre::text {

	std::optional<std::uint32_t> QueryVectorFontNominalGlyph(const resource::SfntFont& font, std::uint32_t codepoint) {
		const auto glyph = font.MapCharacter(codepoint);
		if (glyph == 0) {
			return std::nullopt;
		}

		return glyph;
	}

} // namespace sgre::text
