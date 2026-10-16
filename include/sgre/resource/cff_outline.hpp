#pragma once

#include "sgre/resource/cff.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace sgre::resource {

	struct CffOutlinePoint {
		double X = 0;
		double Y = 0;
	};

	enum class CffOutlineCommandKind {
		MoveTo,
		LineTo,
		CubicTo,
		ClosePath
	};

	struct CffOutlineCommand {
		CffOutlineCommandKind Kind = CffOutlineCommandKind::ClosePath;
		// Move/line use point zero; a cubic uses its two controls and endpoint.
		std::array<CffOutlinePoint, 3> Points{};
	};

	enum class CffOutlineHintEventKind {
		DeclareHorizontalStems,
		DeclareVerticalStems,
		HintMask,
		CounterMask
	};

	struct CffOutlineStem {
		double StartFontUnits = 0;
		double EndFontUnits = 0;
	};

	struct CffOutlineHintEvent {
		CffOutlineHintEventKind Kind = CffOutlineHintEventKind::HintMask;
		// Number of raw outline commands already emitted at the original operator boundary.
		std::size_t CommandIndex = 0;
		std::vector<CffOutlineStem> Stems;
		std::array<std::uint8_t, 12> MaskBytes{};
		std::uint16_t HorizontalStemCount = 0;
		std::uint16_t VerticalStemCount = 0;
	};

	struct CffOutline {
		std::vector<CffOutlineCommand> Commands;
		double AdvanceWidth = 0;
		std::uint16_t StemCount = 0;
		std::vector<CffOutlineHintEvent> HintEvents;
	};

	// Executes observed Type 2 drawing/stem/mask/subroutine operators in raw design coordinates.
	// Font matrices, scaling, hinting, drawing-axis conversion and rasterization are separate phases.
	[[nodiscard]] CffOutline ReadCffGlyphOutline(const CffFont& font, std::uint32_t glyph_index);

} // namespace sgre::resource
