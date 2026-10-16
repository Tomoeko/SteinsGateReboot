#include "sgre/text/truetype_stack_program.hpp"

#include "sgre/text/truetype_program.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>

namespace sgre::text {

	static constexpr std::size_t kMaximumStackProgramBytes = 1024 * 1024;
	static constexpr std::uint32_t kMaximumStackInstructions = 1000000;
	static constexpr std::uint32_t kExtraStackCells = 32;

	struct StackEffect {
		std::uint8_t Pop = 0;
		std::uint8_t Push = 0;
	};

	static StackEffect ReadStackEffect(const TrueTypeInstruction& instruction) {
		switch (instruction.Opcode) {
		case 0x20:
			return {1, 2};
		case 0x21:
		case 0x26:
			return {1, 0};
		case 0x22:
			return {0, 0};
		case 0x23:
			return {2, 2};
		case 0x24:
			return {0, 1};
		case 0x25:
		case 0x5c:
		case 0x64:
		case 0x65:
		case 0x66:
		case 0x67:
			return {1, 1};
		case 0x50:
		case 0x51:
		case 0x52:
		case 0x53:
		case 0x54:
		case 0x55:
		case 0x5a:
		case 0x5b:
		case 0x60:
		case 0x61:
		case 0x62:
		case 0x63:
		case 0x8b:
		case 0x8c:
			return {2, 1};
		case 0x8a:
			return {3, 3};
		default:
			throw std::runtime_error("TrueType stack opcode " + std::to_string(instruction.Opcode) + " at offset " + std::to_string(instruction.Offset) + " requires recovered interpreter execution");
		}
	}

	static std::int32_t WordValue(std::uint32_t word) {
		return std::bit_cast<std::int32_t>(word);
	}

	static std::int32_t NegateWord(std::int32_t value) {
		return WordValue(0u - static_cast<std::uint32_t>(value));
	}

	static std::int32_t AbsoluteWord(std::int32_t value) {
		return value < 0 ? NegateWord(value) : value;
	}

	static std::int32_t MultiplyDivide(std::int32_t first, std::int32_t second, std::int32_t divisor, bool rounded) {
		// Original Win64 helpers retain signed32 INT_MIN through NEG/CMOVS,
		// sign-extend it, then use modulo64 IMUL and unsigned DIV. Keep those bits.
		const auto first_magnitude = static_cast<std::uint64_t>(static_cast<std::int64_t>(AbsoluteWord(first)));
		const auto second_magnitude = static_cast<std::uint64_t>(static_cast<std::int64_t>(AbsoluteWord(second)));
		const auto denominator = static_cast<std::uint64_t>(static_cast<std::int64_t>(AbsoluteWord(divisor)));
		auto numerator = first_magnitude * second_magnitude;
		if (rounded) {
			numerator += denominator >> 1;
		}

		const auto quotient = static_cast<std::uint32_t>(numerator / denominator);
		const bool negative = (first < 0) != ((second < 0) != (divisor < 0));

		return WordValue(negative ? 0u - quotient : quotient);
	}

	static void CopyIndexed(std::vector<std::int32_t>& stack, std::size_t arguments, bool strict, std::uint32_t offset) {
		const auto index = stack[arguments];
		if (index <= 0 || static_cast<std::uint32_t>(index) > arguments) {
			if (strict) {
				throw TrueTypeProgramError(TrueTypeProgramFailure::InvalidReference, offset, "CINDEX is outside the preceding operand stack");
			}

			stack[arguments] = 0;

			return;
		}

		stack[arguments] = stack[arguments - static_cast<std::uint32_t>(index)];
	}

	static void MoveIndexed(std::vector<std::int32_t>& stack, std::size_t arguments, bool strict, std::uint32_t offset) {
		const auto index = stack[arguments];
		if (index <= 0 || static_cast<std::uint32_t>(index) > arguments) {
			if (strict) {
				throw TrueTypeProgramError(TrueTypeProgramFailure::InvalidReference, offset, "MINDEX is outside the preceding operand stack");
			}

			return;
		}

		const auto selected = arguments - static_cast<std::uint32_t>(index);
		const auto value = stack[selected];
		for (auto cell = selected; cell + 1 < arguments; ++cell) {
			stack[cell] = stack[cell + 1];
		}

		stack[arguments - 1] = value;
	}

	static void ExecuteFixed(const TrueTypeInstruction& instruction, TrueTypeStackLimits limits, TrueTypeStackProgramResult& result) {
		const auto effect = ReadStackEffect(instruction);
		const auto old_top = result.Stack.size();
		const bool underflow = old_top < effect.Pop;
		if (underflow && limits.StrictStackUnderflow) {
			throw TrueTypeProgramError(TrueTypeProgramFailure::StackUnderflow, instruction.Offset, "fixed instruction lacks its complete operand group");
		}

		const auto arguments = underflow ? std::size_t(0) : old_top - effect.Pop;
		const auto new_top = arguments + effect.Push;
		if (new_top > result.StackCapacity) {
			throw TrueTypeProgramError(TrueTypeProgramFailure::StackOverflow, instruction.Offset, "fixed instruction exceeds the actual operand stack capacity");
		}

		result.Stack.resize(std::max({old_top, new_top, std::size_t(effect.Pop)}));
		if (underflow) {
			// Native non-pedantic preflight clears the entire missing fixed group,
			// including any partial operands already present at stack index zero.
			std::fill_n(result.Stack.begin(), effect.Pop, 0);
		}

		auto& stack = result.Stack;
		switch (instruction.Opcode) {
		case 0x20:
			stack[arguments + 1] = stack[arguments];
			break;
		case 0x21:
			break;
		case 0x22:
			stack.clear();

			return;
		case 0x23:
			std::swap(stack[arguments], stack[arguments + 1]);
			break;
		case 0x24:
			stack[arguments] = static_cast<std::int32_t>(old_top);
			break;
		case 0x25:
			CopyIndexed(stack, arguments, limits.StrictStackUnderflow, instruction.Offset);
			break;
		case 0x26:
			MoveIndexed(stack, arguments, limits.StrictStackUnderflow, instruction.Offset);
			break;
		case 0x50:
			stack[arguments] = stack[arguments] < stack[arguments + 1];
			break;
		case 0x51:
			stack[arguments] = stack[arguments] <= stack[arguments + 1];
			break;
		case 0x52:
			stack[arguments] = stack[arguments] > stack[arguments + 1];
			break;
		case 0x53:
			stack[arguments] = stack[arguments] >= stack[arguments + 1];
			break;
		case 0x54:
			stack[arguments] = stack[arguments] == stack[arguments + 1];
			break;
		case 0x55:
			stack[arguments] = stack[arguments] != stack[arguments + 1];
			break;
		case 0x5a:
			stack[arguments] = stack[arguments] != 0 && stack[arguments + 1] != 0;
			break;
		case 0x5b:
			stack[arguments] = stack[arguments] != 0 || stack[arguments + 1] != 0;
			break;
		case 0x5c:
			stack[arguments] = stack[arguments] == 0;
			break;
		case 0x60:
			stack[arguments] = WordValue(static_cast<std::uint32_t>(stack[arguments]) + static_cast<std::uint32_t>(stack[arguments + 1]));
			break;
		case 0x61:
			stack[arguments] = WordValue(static_cast<std::uint32_t>(stack[arguments]) - static_cast<std::uint32_t>(stack[arguments + 1]));
			break;
		case 0x62:
			if (stack[arguments + 1] == 0) {
				throw TrueTypeProgramError(TrueTypeProgramFailure::DivideByZero, instruction.Offset, "DIV has a zero denominator");
			}

			stack[arguments] = MultiplyDivide(stack[arguments], 64, stack[arguments + 1], false);
			break;
		case 0x63:
			stack[arguments] = MultiplyDivide(stack[arguments], stack[arguments + 1], 64, true);
			break;
		case 0x64:
			stack[arguments] = AbsoluteWord(stack[arguments]);
			break;
		case 0x65:
			stack[arguments] = NegateWord(stack[arguments]);
			break;
		case 0x66:
			stack[arguments] = WordValue(static_cast<std::uint32_t>(stack[arguments]) & 0xffffffc0u);
			break;
		case 0x67:
			stack[arguments] = WordValue((static_cast<std::uint32_t>(stack[arguments]) + 63u) & 0xffffffc0u);
			break;
		case 0x8a:
			std::rotate(stack.begin() + static_cast<std::ptrdiff_t>(arguments), stack.begin() + static_cast<std::ptrdiff_t>(arguments + 1), stack.begin() + static_cast<std::ptrdiff_t>(arguments + 3));
			break;
		case 0x8b:
			stack[arguments] = std::max(stack[arguments], stack[arguments + 1]);
			break;
		case 0x8c:
			stack[arguments] = std::min(stack[arguments], stack[arguments + 1]);
			break;
		default:
			throw std::logic_error("TrueType admitted stack effect has no instruction kernel");
		}

		stack.resize(new_top);
	}

	TrueTypeStackProgramResult ExecuteTrueTypeStackProgram(std::span<const std::uint8_t> program, TrueTypeStackLimits limits, std::span<const std::int32_t> initial_stack) {
		if (program.size() > kMaximumStackProgramBytes) {
			throw std::runtime_error("TrueType stack program exceeds its bounded preparation size");
		}

		TrueTypeStackProgramResult result;
		result.StackCapacity = std::uint32_t(limits.MaxStackElements) + kExtraStackCells;
		if (initial_stack.size() > result.StackCapacity) {
			throw TrueTypeProgramError(TrueTypeProgramFailure::StackOverflow, 0, "initial operands exceed the actual stack capacity");
		}

		result.Stack.reserve(result.StackCapacity);
		result.Stack.assign(initial_stack.begin(), initial_stack.end());
		std::uint32_t offset = 0;
		while (offset < program.size()) {
			const auto instruction = DecodeTrueTypeInstruction(program, offset);
			if (instruction.IsPush()) {
				if (instruction.PushCount > result.StackCapacity - result.Stack.size()) {
					throw TrueTypeProgramError(TrueTypeProgramFailure::StackOverflow, offset, "PUSH exceeds the actual operand stack capacity");
				}

				for (std::size_t index = 0; index < instruction.PushCount; ++index) {
					result.Stack.push_back(instruction.PushValue(index));
				}
			} else if (instruction.Opcode == 0x2d) {
				throw TrueTypeProgramError(TrueTypeProgramFailure::EndWithoutCall, offset, "ENDF has no actual call frame");
			} else {
				ExecuteFixed(instruction, limits, result);
			}

			++result.ExecutedInstructions;
			if (result.ExecutedInstructions > kMaximumStackInstructions) {
				throw TrueTypeProgramError(TrueTypeProgramFailure::InstructionLimit, offset, "executed instruction count exceeds the original limit");
			}

			offset += instruction.ByteCount;
		}

		return result;
	}

} // namespace sgre::text
