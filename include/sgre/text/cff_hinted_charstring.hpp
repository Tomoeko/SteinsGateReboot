#pragma once

#include "sgre/text/cff_contour_owner.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace sgre::text {

	struct CffCharstringPrograms {
		std::span<const std::uint8_t> Glyph;
		std::span<const std::span<const std::uint8_t>> LocalSubroutines;
		std::span<const std::span<const std::uint8_t>> GlobalSubroutines;
	};

	struct CffHintedCharstringConfiguration {
		CffContourConfiguration Contour;
		std::int32_t DefaultWidthFontUnits = 0;
		std::int32_t NominalWidthFontUnits = 0;
	};

	struct CffHintedCharstring {
		std::vector<CffContourCallback> Callbacks;
		CffCharstringWidth Width;
		CffFixedPoint InterpreterLogicalPoint;
		CffContourOwnerState FinalState;
	};

	// Programs borrow their immutable font owner for this preparation call only.
	// Executes observed Type2 operators into native fixed geometry; final builder units/rasterization remain separate.
	[[nodiscard]] CffHintedCharstring ExecuteCffHintedCharstring(const CffCharstringPrograms& programs, const CffHintedCharstringConfiguration& configuration);

} // namespace sgre::text
