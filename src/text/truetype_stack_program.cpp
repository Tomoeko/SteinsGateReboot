#include "sgre/text/truetype_stack_program.hpp"

#include "sgre/text/truetype_control_program.hpp"
#include "sgre/text/truetype_program.hpp"

#include <algorithm>
#include <array>
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

	static constexpr std::size_t kControlCallCapacity = 32;
	static constexpr std::uint32_t kMaximumControlScans = 4000000;

	struct TrueTypeControlCall {
		TrueTypeProgramRange CallerRange = TrueTypeProgramRange::Preparation;
		std::uint32_t ReturnOffset = 0;
		std::int32_t Remaining = 0;
		const TrueTypeFunctionDefinition* Definition = nullptr;
	};

	static std::span<const std::uint8_t> ControlCode(const std::array<std::span<const std::uint8_t>, 3>& ranges, TrueTypeProgramRange range, std::uint32_t offset) {
		const auto index = static_cast<std::uint32_t>(range);
		if (index < 1 || index > ranges.size()) {
			throw TrueTypeProgramError(TrueTypeProgramFailure::InvalidArgument, offset, "code range is outside the original three slots");
		}
		if (ranges[index - 1].data() == nullptr) {
			throw TrueTypeProgramError(TrueTypeProgramFailure::MissingCodeRange, offset, "code range has no original program owner");
		}

		return ranges[index - 1];
	}

	static std::size_t ControlOperands(TrueTypeStackProgramResult& operands, std::size_t count, bool strict, std::uint32_t offset) {
		if (operands.Stack.size() >= count) {
			return operands.Stack.size() - count;
		}
		if (strict) {
			throw TrueTypeProgramError(TrueTypeProgramFailure::StackUnderflow, offset, "control instruction lacks its complete fixed operand group");
		}

		operands.Stack.assign(count, 0);

		return 0;
	}

	static std::uint32_t SkipControlBranch(std::span<const std::uint8_t> code, const TrueTypeInstruction& origin, bool stop_at_else, TrueTypeControlProgramResult& result) {
		auto offset = origin.Offset + origin.ByteCount;
		std::uint32_t depth = 1;
		while (true) {
			if (++result.ScannedInstructions > kMaximumControlScans) {
				throw std::runtime_error("TrueType control program exceeds its bounded lexical scan work");
			}

			const auto instruction = DecodeTrueTypeInstruction(code, offset);
			if (instruction.Opcode == 0x58) {
				++depth;
			} else if (instruction.Opcode == 0x59) {
				--depth;
				if (depth == 0) {
					return offset + instruction.ByteCount;
				}
			} else if (stop_at_else && instruction.Opcode == 0x1b && depth == 1) {
				return offset + instruction.ByteCount;
			}

			offset += instruction.ByteCount;
		}
	}

	static std::uint32_t RelativeControlJump(std::uint32_t offset, std::int32_t distance, std::size_t retained_operands, std::span<const TrueTypeControlCall> calls) {
		const auto target = offset + static_cast<std::uint32_t>(distance);
		if (distance == 0 && retained_operands == 0) {
			throw TrueTypeProgramError(TrueTypeProgramFailure::InvalidArgument, target, "zero relative jump has no retained preceding operands");
		}
		if (WordValue(target) < 0 || (!calls.empty() && target > calls.back().Definition->EndOffset)) {
			throw TrueTypeProgramError(TrueTypeProgramFailure::InvalidArgument, target, "relative jump escapes the signed code or current function end");
		}

		return target;
	}

	TrueTypeControlProgramResult ExecuteTrueTypeControlProgram(const TrueTypeFontProgram& font, TrueTypeProgramCode code, TrueTypeProgramRange entry, bool strict_stack_underflow, std::span<const std::int32_t> initial_stack) {
		const std::array ranges{font.Program(), code.Preparation, code.Glyph};
		for (const auto range : ranges) {
			if (range.size() > kMaximumStackProgramBytes) {
				throw std::runtime_error("TrueType control code range exceeds its bounded preparation size");
			}
		}

		auto current = ControlCode(ranges, entry, 0);
		if (current.empty()) {
			throw std::runtime_error("TrueType control execution requires nonempty entry code");
		}

		TrueTypeControlProgramResult result;
		result.Operands.StackCapacity = font.StackCapacity();
		if (initial_stack.size() > result.Operands.StackCapacity) {
			throw TrueTypeProgramError(TrueTypeProgramFailure::StackOverflow, 0, "initial control operands exceed the actual font stack capacity");
		}

		result.Operands.Stack.reserve(result.Operands.StackCapacity);
		result.Operands.Stack.assign(initial_stack.begin(), initial_stack.end());
		TrueTypeStackLimits stack_limits;
		stack_limits.MaxStackElements = static_cast<std::uint16_t>(font.StackCapacity() - kExtraStackCells);
		stack_limits.StrictStackUnderflow = strict_stack_underflow;
		std::array<TrueTypeControlCall, kControlCallCapacity> calls;
		std::size_t call_depth = 0;
		auto range = entry;
		std::uint32_t offset = 0;
		while (offset < current.size()) {
			const auto instruction = DecodeTrueTypeInstruction(current, offset);
			auto next_offset = offset + instruction.ByteCount;
			if (instruction.IsPush()) {
				if (instruction.PushCount > result.Operands.StackCapacity - result.Operands.Stack.size()) {
					throw TrueTypeProgramError(TrueTypeProgramFailure::StackOverflow, offset, "PUSH exceeds the actual font operand capacity");
				}

				for (std::size_t index = 0; index < instruction.PushCount; ++index) {
					result.Operands.Stack.push_back(instruction.PushValue(index));
				}
			} else if (instruction.Opcode == 0x2a || instruction.Opcode == 0x2b) {
				const bool loop = instruction.Opcode == 0x2a;
				const auto arguments = ControlOperands(result.Operands, loop ? 2 : 1, strict_stack_underflow, offset);
				const auto identifier = result.Operands.Stack[arguments + (loop ? 1 : 0)];
				const auto* definition = font.FindFunction(static_cast<std::uint32_t>(identifier));
				if (!definition) {
					throw TrueTypeProgramError(TrueTypeProgramFailure::InvalidReference, offset, "CALL has no active original function definition");
				}
				if (call_depth == calls.size()) {
					throw TrueTypeProgramError(TrueTypeProgramFailure::StackOverflow, offset, "CALL exceeds the original32-frame capacity");
				}

				const auto count = loop ? result.Operands.Stack[arguments] : 1;
				result.Operands.Stack.resize(arguments);
				++result.CallInstructions;
				if (count > 0) {
					auto& call = calls[call_depth++];
					call.CallerRange = range;
					call.ReturnOffset = next_offset;
					call.Remaining = count;
					call.Definition = definition;
					result.MaximumCallDepth = std::max(result.MaximumCallDepth, static_cast<std::uint32_t>(call_depth));
					range = static_cast<TrueTypeProgramRange>(definition->ProgramRange);
					current = ControlCode(ranges, range, offset);
					next_offset = definition->StartOffset;
				}
			} else if (instruction.Opcode == 0x2d) {
				if (call_depth == 0) {
					throw TrueTypeProgramError(TrueTypeProgramFailure::EndWithoutCall, offset, "ENDF has no original return record");
				}

				auto& call = calls[call_depth - 1];
				if (--call.Remaining > 0) {
					next_offset = call.Definition->StartOffset;
				} else {
					range = call.CallerRange;
					current = ControlCode(ranges, range, offset);
					next_offset = call.ReturnOffset;
					--call_depth;
				}
			} else if (instruction.Opcode == 0x58) {
				const auto arguments = ControlOperands(result.Operands, 1, strict_stack_underflow, offset);
				const bool condition = result.Operands.Stack[arguments] != 0;
				result.Operands.Stack.resize(arguments);
				if (!condition) {
					next_offset = SkipControlBranch(current, instruction, true, result);
				}
			} else if (instruction.Opcode == 0x1b) {
				next_offset = SkipControlBranch(current, instruction, false, result);
			} else if (instruction.Opcode == 0x59) {
				// Original EIF is an unconditional no-op; there is no branch-frame owner.
			} else if (instruction.Opcode == 0x1c || instruction.Opcode == 0x78 || instruction.Opcode == 0x79) {
				const bool conditional = instruction.Opcode != 0x1c;
				const auto arguments = ControlOperands(result.Operands, conditional ? 2 : 1, strict_stack_underflow, offset);
				const auto distance = result.Operands.Stack[arguments];
				const bool taken = !conditional || (result.Operands.Stack[arguments + 1] != 0) == (instruction.Opcode == 0x78);
				result.Operands.Stack.resize(arguments);
				if (taken) {
					next_offset = RelativeControlJump(offset, distance, arguments, std::span(calls).first(call_depth));
				}
			} else {
				ExecuteFixed(instruction, stack_limits, result.Operands);
			}

			offset = next_offset;
			if (++result.Operands.ExecutedInstructions > kMaximumStackInstructions) {
				throw TrueTypeProgramError(TrueTypeProgramFailure::InstructionLimit, offset, "executed control instruction count exceeds the original limit");
			}
		}

		if (call_depth != 0) {
			throw TrueTypeProgramError(TrueTypeProgramFailure::CodeOverflow, offset, "code range ended before the original function return");
		}

		result.FinalRange = range;
		result.FinalOffset = offset;

		return result;
	}

} // namespace sgre::text
