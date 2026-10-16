#pragma once

#include "sgre/text/glyph_positioning_blob.hpp"
#include "sgre/text/glyph_substitution_blob.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace sgre::text {

	struct LayoutLookupAccelerator {
		std::uint32_t GlyphsShift4 = 0;
		std::uint32_t GlyphsShift0 = 0;
		std::uint32_t GlyphsShift9 = 0;
	};

	[[nodiscard]] std::uint32_t SubstitutionLookupCount(const GlyphSubstitutionBlob& blob);
	[[nodiscard]] std::uint32_t PositioningLookupCount(const GlyphPositioningBlob& blob);
	// The exact admitted slot count is required; every supplied record is reset before filling.
	void InitializeSubstitutionLookupAccelerators(const GlyphSubstitutionBlob& blob, std::span<LayoutLookupAccelerator> accelerators);
	void InitializePositioningLookupAccelerators(const GlyphPositioningBlob& blob, std::span<LayoutLookupAccelerator> accelerators);

	// Admission and immutable byte lifetime precede construction. The returned records own values only.
	[[nodiscard]] std::vector<LayoutLookupAccelerator> BuildSubstitutionLookupAccelerators(const GlyphSubstitutionBlob& blob);
	[[nodiscard]] std::vector<LayoutLookupAccelerator> BuildPositioningLookupAccelerators(const GlyphPositioningBlob& blob);

} // namespace sgre::text
