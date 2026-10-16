#pragma once

#include "sgre/text/truetype_program.hpp"
#include "sgre/text/truetype_stack_program.hpp"

#include <cstdint>
#include <span>

namespace sgre::text {

	enum class TrueTypeProgramRange : std::uint32_t {
		Font = 1,
		Preparation = 2,
		Glyph = 3
	};

	struct TrueTypeProgramCode {
		// These immutable views and the font-definition owner outlive execution.
		std::span<const std::uint8_t> Preparation;
		std::span<const std::uint8_t> Glyph;
	};

	struct TrueTypeControlProgramResult {
		TrueTypeStackProgramResult Operands;
		TrueTypeProgramRange FinalRange = TrueTypeProgramRange::Preparation;
		std::uint32_t FinalOffset = 0;
		std::uint32_t MaximumCallDepth = 0;
		std::uint32_t CallInstructions = 0;
		std::uint32_t ScannedInstructions = 0;
	};

	// Adds CALL/LOOPCALL/ENDF and lexical/relative branches to the recovered stack
	// kernels. Definitions refer to their original complete fpgm range. CVT,
	// storage, graphics-state and point operations still require recovery.
	[[nodiscard]] TrueTypeControlProgramResult ExecuteTrueTypeControlProgram(const TrueTypeFontProgram& font, TrueTypeProgramCode code, TrueTypeProgramRange entry, bool strict_stack_underflow = false, std::span<const std::int32_t> initial_stack = {});

} // namespace sgre::text
