#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace sgre::text {

	struct TrueTypeStackLimits {
		std::uint16_t MaxStackElements = 0;
		bool StrictStackUnderflow = false;
	};

	struct TrueTypeStackProgramResult {
		std::vector<std::int32_t> Stack;
		std::uint32_t StackCapacity = 0;
		std::uint32_t ExecutedInstructions = 0;
	};

	// Executes only recovered PUSH, stack, comparison, boolean and arithmetic
	// instructions. Inputs borrow for this call; the returned stack owns its data.
	// Control flow, function calls, storage, CVT and glyph geometry remain gated.
	[[nodiscard]] TrueTypeStackProgramResult ExecuteTrueTypeStackProgram(std::span<const std::uint8_t> program, TrueTypeStackLimits limits, std::span<const std::int32_t> initial_stack = {});

} // namespace sgre::text
