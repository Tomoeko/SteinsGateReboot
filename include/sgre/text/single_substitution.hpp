#pragma once

#include "sgre/resource/opentype_layout.hpp"
#include "sgre/text/shaping_lookup_plan.hpp"

#include <cstdint>
#include <span>

namespace sgre::text {

	struct SubstitutionGlyph {
		std::uint32_t GlyphIndex = 0;
		std::uint32_t Mask = 0;
		std::uint32_t Cluster = 0;
		std::uint16_t GlyphProperties = 0;
		// Original bytes +0xe/+0xf and CharacterState's upper word remain opaque; preprocessing owns its low word.
		std::uint16_t PrivateState = 0;
		std::uint32_t CharacterState = 0;
		bool operator==(const SubstitutionGlyph&) const = default;
	};

	// Borrows the caller's initialized records and applies one flags-zero resolved single lookup in place.
	// True means a covered replacement was applied, including a replacement with the same glyph index.
	[[nodiscard]] bool ApplyDirectSingleSubstitution(const resource::OpenTypeLayoutFont& layout, const VectorFontScheduledLookup& lookup, std::span<SubstitutionGlyph> glyphs);

} // namespace sgre::text
