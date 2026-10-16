#include "sgre/text/single_substitution.hpp"

#include <stdexcept>

namespace sgre::text {

	static constexpr std::size_t kMaximumSingleSubstitutionGlyphs = 65536;

	static void ValidateSingleSubstitution(const resource::OpenTypeLayoutFont& layout, const VectorFontScheduledLookup& lookup, std::span<const SubstitutionGlyph> glyphs) {
		if (layout.Kind() != resource::OpenTypeLayoutKind::Substitution || lookup.Index >= layout.Lookups().size()) {
			throw std::runtime_error("direct single substitution requires an actual GSUB lookup index");
		}
		if (!layout.CanApplySingleSubstitution(lookup.Index)) {
			throw std::runtime_error("direct single substitution requires a flags-zero type-one lookup without feature variations");
		}
		if (glyphs.size() > kMaximumSingleSubstitutionGlyphs) {
			throw std::runtime_error("direct single substitution exceeds its glyph count limit");
		}

		// Complete input/replacement preflight avoids publishing a partial operation for malformed input.
		for (const auto& glyph : glyphs) {
			static_cast<void>(layout.GlyphClass(glyph.GlyphIndex));
			if ((glyph.Mask & lookup.Mask) != 0) {
				static_cast<void>(layout.ApplySingleSubstitution(lookup.Index, glyph.GlyphIndex));
			}
		}
	}

	bool ApplyDirectSingleSubstitution(const resource::OpenTypeLayoutFont& layout, const VectorFontScheduledLookup& lookup, std::span<SubstitutionGlyph> glyphs) {
		ValidateSingleSubstitution(layout, lookup, glyphs);
		bool any_applied = false;
		for (auto& glyph : glyphs) {
			if ((glyph.Mask & lookup.Mask) == 0) {
				continue;
			}

			const auto replacement = layout.ApplySingleSubstitution(lookup.Index, glyph.GlyphIndex);
			if (!replacement) {
				continue;
			}

			if (layout.HasGlyphClasses()) {
				glyph.GlyphProperties = static_cast<std::uint16_t>((glyph.GlyphProperties & 0x60U) | layout.GlyphProperties(*replacement) | 0x10U);
			}
			glyph.GlyphIndex = *replacement;
			any_applied = true;
		}

		return any_applied;
	}

} // namespace sgre::text
