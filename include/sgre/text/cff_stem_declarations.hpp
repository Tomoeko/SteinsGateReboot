#pragma once

#include "sgre/text/cff_stem_map.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace sgre::text {

	enum class CffCharstringNumberKind : std::uint8_t {
		Fixed16Dot16,
		Fraction2Dot30,
		Integer
	};

	struct CffCharstringNumber {
		std::int32_t Value = 0;
		CffCharstringNumberKind Kind = CffCharstringNumberKind::Fixed16Dot16;
	};

	struct CffCharstringWidth {
		std::int32_t Width16Dot16 = 0;
		bool Read = false;
	};

	struct CffDeclaredStemBuffer {
		std::array<CffStemHint, kCffStemHintLimit> Stems{};
		std::size_t Count = 0;
	};

	struct CffStemDeclarationConfiguration {
		std::int32_t NominalWidthFontUnits = 0;
		std::int32_t Origin16Dot16 = 0;
		bool AdvanceOnly = false;
	};

	struct CffStemDeclarationResult {
		std::size_t RemainingOperandCount = 0;
	};

	// Preserves native 32-bit number conversion, including fraction rounding and integer shift wrapping.
	[[nodiscard]] std::int32_t ConvertCffCharstringNumberTo16Dot16(CffCharstringNumber number);
	// Width is shared by both declaration axes; the stem buffer belongs to one glyph and one axis.
	// Borrowed operands are not changed. The result reports the original stack-clear/retain branch.
	[[nodiscard]] CffStemDeclarationResult AppendCffDeclaredStems(CffDeclaredStemBuffer& stems, CffCharstringWidth& width, std::span<const CffCharstringNumber> operands, CffStemDeclarationConfiguration configuration = {});

} // namespace sgre::text
