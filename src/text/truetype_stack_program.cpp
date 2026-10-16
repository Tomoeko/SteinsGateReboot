#include "sgre/text/truetype_stack_program.hpp"

#include "sgre/text/truetype_control_program.hpp"
#include "sgre/text/truetype_program.hpp"
#include "sgre/text/truetype_state_program.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <optional>
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

	static std::int32_t AddWords(std::int32_t first, std::int32_t second) {
		return WordValue(static_cast<std::uint32_t>(first) + static_cast<std::uint32_t>(second));
	}

	static std::int32_t SubtractWords(std::int32_t first, std::int32_t second) {
		return WordValue(static_cast<std::uint32_t>(first) - static_cast<std::uint32_t>(second));
	}

	static std::int32_t MultiplyFixed(std::int32_t first, std::int32_t second) {
		const auto product = std::int64_t(first) * second;
		const auto rounded = product - (product < 0 ? 1 : 0) + 0x8000;
		const auto quotient = rounded / 0x10000 - (rounded < 0 && rounded % 0x10000 != 0 ? 1 : 0);

		return WordValue(static_cast<std::uint32_t>(quotient));
	}

	static std::optional<StackEffect> StateStackEffect(std::uint8_t opcode) {
		if (opcode <= 0x05 || opcode == 0x0e || opcode == 0x18 || opcode == 0x19 || opcode == 0x3d || opcode == 0x4d || opcode == 0x4e || opcode == 0x7a || opcode == 0x7c || opcode == 0x7d) {
			return StackEffect{0, 0};
		}
		if (opcode == 0x0c || opcode == 0x0d) {
			return StackEffect{0, 2};
		}
		if ((opcode >= 0x10 && opcode <= 0x17) || opcode == 0x1a || (opcode >= 0x1d && opcode <= 0x1f) || opcode == 0x5e || opcode == 0x5f || opcode == 0x76 || opcode == 0x77 || opcode == 0x85 || opcode == 0x8d) {
			return StackEffect{1, 0};
		}
		if (opcode == 0x42 || opcode == 0x44 || opcode == 0x70 || opcode == 0x8e) {
			return StackEffect{2, 0};
		}
		if (opcode == 0x43 || opcode == 0x45 || (opcode >= 0x68 && opcode <= 0x6f) || opcode == 0x88) {
			return StackEffect{1, 1};
		}
		if (opcode == 0x4b || opcode == 0x4c) {
			return StackEffect{0, 1};
		}

		return std::nullopt;
	}

	static std::int32_t RoundStateDistance(const TrueTypeGraphicsState& graphics, std::int32_t distance, std::int32_t compensation) {
		const bool negative = distance < 0;
		const auto adjusted = negative ? SubtractWords(compensation, distance) : AddWords(compensation, distance);
		std::int32_t rounded = 0;
		switch (graphics.Round) {
		case TrueTypeRoundMode::HalfGrid:
			rounded = WordValue(static_cast<std::uint32_t>(adjusted) & 0xffffffc0u);
			if (negative) {
				rounded = SubtractWords(-32, rounded);

				return rounded > 0 ? -32 : rounded;
			}

			return rounded >= -32 ? AddWords(rounded, 32) : 32;
		case TrueTypeRoundMode::Grid:
			rounded = WordValue(static_cast<std::uint32_t>(AddWords(adjusted, 32)) & 0xffffffc0u);
			break;
		case TrueTypeRoundMode::DoubleGrid:
			rounded = WordValue(static_cast<std::uint32_t>(AddWords(adjusted, 16)) & 0xffffffe0u);
			break;
		case TrueTypeRoundMode::DownToGrid:
			rounded = WordValue(static_cast<std::uint32_t>(adjusted) & 0xffffffc0u);
			break;
		case TrueTypeRoundMode::UpToGrid:
			rounded = WordValue(static_cast<std::uint32_t>(AddWords(adjusted, 63)) & 0xffffffc0u);
			break;
		case TrueTypeRoundMode::Off:
			rounded = adjusted;
			break;
		case TrueTypeRoundMode::Super:
		case TrueTypeRoundMode::Super45:
			if (graphics.RoundPeriod <= 0) {
				throw std::runtime_error("TrueType super-round state requires a positive original period");
			}

			rounded = AddWords(adjusted, SubtractWords(graphics.RoundThreshold, graphics.RoundPhase));
			if (graphics.Round == TrueTypeRoundMode::Super) {
				rounded = WordValue(static_cast<std::uint32_t>(rounded) & static_cast<std::uint32_t>(NegateWord(graphics.RoundPeriod)));
			} else {
				rounded = SubtractWords(rounded, rounded % graphics.RoundPeriod);
			}

			rounded = AddWords(rounded, graphics.RoundPhase);
			if (negative) {
				rounded = NegateWord(rounded);

				return rounded > 0 ? NegateWord(graphics.RoundPhase) : rounded;
			}

			return rounded < 0 ? graphics.RoundPhase : rounded;
		}

		rounded = negative ? NegateWord(rounded) : rounded;

		return negative ? std::min(rounded, 0) : std::max(rounded, 0);
	}

	static void SetSuperRound(TrueTypeGraphicsState& graphics, std::int32_t selector, bool diagonal) {
		const auto word = static_cast<std::uint32_t>(selector);
		std::int32_t period = diagonal ? 0x2d41 : 0x4000;
		if ((word & 0xc0) == 0) {
			period = diagonal ? 0x16a0 : 0x2000;
		} else if ((word & 0xc0) == 0x80) {
			period = diagonal ? 0x5a82 : 0x8000;
		}

		const auto phase = period * static_cast<std::int32_t>((word >> 4) & 3) / 4;
		const auto threshold = (word & 15) == 0 ? period - 1 : (static_cast<std::int32_t>(word & 15) - 4) * period / 8;
		graphics.RoundPeriod = period / 256;
		graphics.RoundPhase = phase / 256;
		graphics.RoundThreshold = threshold / 256 - (threshold < 0 && threshold % 256 != 0 ? 1 : 0);
		graphics.Round = diagonal ? TrueTypeRoundMode::Super45 : TrueTypeRoundMode::Super;
	}

	static void RequireEqualPpem(const TrueTypeInterpreterMetrics& metrics) {
		if (metrics.XPpem != metrics.YPpem) {
			throw std::runtime_error("TrueType state execution requires recovered anisotropic CVT and ppem projection");
		}
	}

	static bool StateReferenceValid(std::int32_t index, std::size_t count, bool strict, std::uint32_t offset) {
		if (static_cast<std::uint32_t>(index) < count) {
			return true;
		}
		if (strict) {
			throw TrueTypeProgramError(TrueTypeProgramFailure::InvalidReference, offset, "state instruction index is outside its actual owner");
		}

		return false;
	}

	static void SelectStateZone(TrueTypeGraphicsState& graphics, std::uint8_t opcode, std::int32_t value, bool strict, std::uint32_t offset) {
		if (!StateReferenceValid(value, 2, strict, offset)) {
			return;
		}

		if (opcode == 0x16) {
			graphics.ZonePointers.fill(static_cast<std::uint16_t>(value));
		} else {
			graphics.ZonePointers[opcode - 0x13] = static_cast<std::uint16_t>(value);
		}
	}

	static std::int32_t StateInformation(const TrueTypeInterpreterMetrics& metrics, std::int32_t selector) {
		const auto word = static_cast<std::uint32_t>(selector);
		auto result = (word & 1) != 0 ? metrics.InterpreterVersion : 0u;
		if ((word & 2) != 0 && metrics.Rotated) {
			result |= 1u << 8;
		}
		if ((word & 4) != 0 && metrics.Stretched) {
			result |= 1u << 9;
		}
		if ((word & 8) != 0 && metrics.HasVariations) {
			result |= 1u << 10;
		}
		if ((word & 32) != 0 && metrics.Grayscale) {
			result |= 1u << 12;
		}
		if (metrics.InterpreterVersion == 40) {
			if ((word & 64) != 0) {
				result |= 1u << 13;
			}
			if ((word & 256) != 0 && metrics.Version40Flag372) {
				result |= 1u << 15;
			}
			if ((word & 1024) != 0) {
				result |= 1u << 17;
			}
			if ((word & 2048) != 0) {
				result |= 1u << 18;
			}
			if ((word & 4096) != 0 && metrics.Version40Flag376) {
				result |= 1u << 19;
			}
		}

		return WordValue(result);
	}

	static void SetScanControl(TrueTypeInterpreterState& state, std::int32_t selector) {
		const auto word = static_cast<std::uint32_t>(selector);
		const auto threshold = word & 255;
		if (threshold == 255) {
			state.Graphics.ScanControl = true;

			return;
		}
		if (threshold == 0) {
			state.Graphics.ScanControl = false;

			return;
		}

		const auto& metrics = state.Metrics;
		if (((word & 0x100) != 0 && metrics.CurrentPpem <= threshold) || ((word & 0x200) != 0 && metrics.Rotated) || ((word & 0x400) != 0 && metrics.Stretched)) {
			state.Graphics.ScanControl = true;
		}
		if (((word & 0x800) != 0 && metrics.CurrentPpem > threshold) || ((word & 0x1000) != 0 && metrics.Rotated) || ((word & 0x2000) != 0 && metrics.Stretched)) {
			state.Graphics.ScanControl = false;
		}
	}

	static void SetInstructionControl(TrueTypeInterpreterState& state, std::int32_t value, std::int32_t selector, bool strict, std::uint32_t offset) {
		if (selector < 1 || selector > 3) {
			if (strict) {
				throw TrueTypeProgramError(TrueTypeProgramFailure::InvalidReference, offset, "INSTCTRL selector is outside its original three flags");
			}

			return;
		}

		const auto flag = 1u << (selector - 1);
		if (value != 0 && static_cast<std::uint32_t>(value) != flag) {
			if (strict) {
				throw TrueTypeProgramError(TrueTypeProgramFailure::InvalidReference, offset, "INSTCTRL value is neither zero nor its selected flag");
			}

			return;
		}

		state.Graphics.InstructionControl = static_cast<std::uint8_t>((state.Graphics.InstructionControl & ~flag) | static_cast<std::uint32_t>(value));
		if (selector == 3 && state.Metrics.InterpreterVersion == 40) {
			state.BackwardCompatibility = value != 4;
		}
	}

	static void ExecuteStateKernel(const TrueTypeInstruction& instruction, std::span<std::int32_t> arguments, TrueTypeInterpreterState& state, bool strict) {
		auto& graphics = state.Graphics;
		const auto opcode = instruction.Opcode;
		if (opcode <= 0x05) {
			TrueTypeAxisVector vector;
			vector.X = (opcode & 1) != 0 ? 0x4000 : 0;
			vector.Y = (opcode & 1) != 0 ? 0 : 0x4000;
			if (opcode < 4) {
				graphics.Projection = vector;
				graphics.DualProjection = vector;
			}
			if ((opcode & 2) == 0) {
				graphics.Freedom = vector;
			}

			return;
		}
		if (opcode == 0x0c || opcode == 0x0d) {
			const auto& vector = opcode == 0x0c ? graphics.Projection : graphics.Freedom;
			arguments[0] = vector.X;
			arguments[1] = vector.Y;

			return;
		}
		if (opcode >= 0x10 && opcode <= 0x12) {
			graphics.ReferencePoints[opcode - 0x10] = static_cast<std::uint16_t>(arguments[0]);

			return;
		}
		if (opcode >= 0x13 && opcode <= 0x16) {
			SelectStateZone(graphics, opcode, arguments[0], strict, instruction.Offset);

			return;
		}
		if (opcode >= 0x68 && opcode <= 0x6f) {
			const auto compensation = state.Metrics.Compensation[opcode & 3];
			if (opcode < 0x6c) {
				arguments[0] = RoundStateDistance(graphics, arguments[0], compensation);
			} else {
				const bool negative = arguments[0] < 0;
				const auto adjusted = negative ? SubtractWords(arguments[0], compensation) : AddWords(arguments[0], compensation);
				arguments[0] = negative ? std::min(adjusted, 0) : std::max(adjusted, 0);
			}

			return;
		}

		switch (opcode) {
		case 0x0e:
			graphics.Freedom = graphics.Projection;
			break;
		case 0x17:
			if (arguments[0] < 0) {
				throw TrueTypeProgramError(TrueTypeProgramFailure::InvalidArgument, instruction.Offset, "SLOOP has a negative loop count");
			}

			graphics.Loop = arguments[0];
			break;
		case 0x18:
			graphics.Round = TrueTypeRoundMode::Grid;
			break;
		case 0x19:
			graphics.Round = TrueTypeRoundMode::HalfGrid;
			break;
		case 0x1a:
			graphics.MinimumDistance = arguments[0];
			break;
		case 0x1d:
			graphics.ControlValueCutIn = arguments[0];
			break;
		case 0x1e:
			graphics.SingleWidthCutIn = arguments[0];
			break;
		case 0x1f:
			graphics.SingleWidth = MultiplyFixed(arguments[0], state.Metrics.Scale16Dot16);
			break;
		case 0x3d:
			graphics.Round = TrueTypeRoundMode::DoubleGrid;
			break;
		case 0x42:
			if (StateReferenceValid(arguments[0], state.Storage.size(), strict, instruction.Offset)) {
				state.Storage[static_cast<std::uint32_t>(arguments[0])] = arguments[1];
			}

			break;
		case 0x43:
			arguments[0] = StateReferenceValid(arguments[0], state.Storage.size(), strict, instruction.Offset) ? state.Storage[static_cast<std::uint32_t>(arguments[0])] : 0;
			break;
		case 0x44:
		case 0x70:
			if (StateReferenceValid(arguments[0], state.ControlValues.size(), strict, instruction.Offset)) {
				if (opcode == 0x44) {
					RequireEqualPpem(state.Metrics);
				}

				state.ControlValues[static_cast<std::uint32_t>(arguments[0])] = opcode == 0x70 ? MultiplyFixed(arguments[1], state.Metrics.Scale16Dot16) : arguments[1];
			}

			break;
		case 0x45:
			if (StateReferenceValid(arguments[0], state.ControlValues.size(), strict, instruction.Offset)) {
				RequireEqualPpem(state.Metrics);
				arguments[0] = state.ControlValues[static_cast<std::uint32_t>(arguments[0])];
			} else {
				arguments[0] = 0;
			}

			break;
		case 0x4b:
			RequireEqualPpem(state.Metrics);
			arguments[0] = state.Metrics.CurrentPpem;
			break;
		case 0x4c:
			if (state.Metrics.InterpreterVersion == 35) {
				RequireEqualPpem(state.Metrics);
				arguments[0] = state.Metrics.CurrentPpem;
			} else {
				arguments[0] = state.Metrics.PointSize;
			}

			break;
		case 0x4d:
			graphics.AutoFlip = true;
			break;
		case 0x4e:
			graphics.AutoFlip = false;
			break;
		case 0x5e:
			graphics.DeltaBase = static_cast<std::uint16_t>(arguments[0]);
			break;
		case 0x5f:
			if (static_cast<std::uint32_t>(arguments[0]) > 6) {
				throw TrueTypeProgramError(TrueTypeProgramFailure::InvalidArgument, instruction.Offset, "SDS shift is outside the original zero-through-six range");
			}

			graphics.DeltaShift = static_cast<std::uint16_t>(arguments[0]);
			break;
		case 0x76:
		case 0x77:
			SetSuperRound(graphics, arguments[0], opcode == 0x77);
			break;
		case 0x7a:
			graphics.Round = TrueTypeRoundMode::Off;
			break;
		case 0x7c:
			graphics.Round = TrueTypeRoundMode::UpToGrid;
			break;
		case 0x7d:
			graphics.Round = TrueTypeRoundMode::DownToGrid;
			break;
		case 0x85:
			SetScanControl(state, arguments[0]);
			break;
		case 0x88:
			arguments[0] = StateInformation(state.Metrics, arguments[0]);
			break;
		case 0x8d:
			if (arguments[0] >= 0) {
				graphics.ScanType = arguments[0];
			}

			break;
		case 0x8e:
			SetInstructionControl(state, arguments[0], arguments[1], strict, instruction.Offset);
			break;
		default:
			throw std::logic_error("TrueType admitted state opcode has no instruction kernel");
		}
	}

	static bool ExecuteStateInstruction(const TrueTypeInstruction& instruction, TrueTypeStackLimits limits, TrueTypeStackProgramResult& operands, TrueTypeInterpreterState& state) {
		const auto effect = StateStackEffect(instruction.Opcode);
		if (!effect) {
			return false;
		}

		const auto old_top = operands.Stack.size();
		const bool underflow = old_top < effect->Pop;
		if (underflow && limits.StrictStackUnderflow) {
			throw TrueTypeProgramError(TrueTypeProgramFailure::StackUnderflow, instruction.Offset, "state instruction lacks its complete fixed operand group");
		}

		const auto arguments = underflow ? std::size_t(0) : old_top - effect->Pop;
		const auto new_top = arguments + effect->Push;
		if (new_top > operands.StackCapacity) {
			throw TrueTypeProgramError(TrueTypeProgramFailure::StackOverflow, instruction.Offset, "state instruction exceeds the actual operand capacity");
		}

		operands.Stack.resize(std::max({old_top, new_top, std::size_t(effect->Pop)}));
		if (underflow) {
			std::fill_n(operands.Stack.begin(), effect->Pop, 0);
		}

		ExecuteStateKernel(instruction, std::span(operands.Stack).subspan(arguments), state, limits.StrictStackUnderflow);
		operands.Stack.resize(new_top);

		return true;
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

	static TrueTypeControlProgramResult ExecuteControlProgram(const TrueTypeFontProgram& font, TrueTypeProgramCode code, TrueTypeProgramRange entry, bool strict_stack_underflow, std::span<const std::int32_t> initial_stack, TrueTypeInterpreterState* state) {
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
			} else if (state && ExecuteStateInstruction(instruction, stack_limits, result.Operands, *state)) {
				// State kernels share the original fixed-group preflight and control clock.
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

	TrueTypeControlProgramResult ExecuteTrueTypeControlProgram(const TrueTypeFontProgram& font, TrueTypeProgramCode code, TrueTypeProgramRange entry, bool strict_stack_underflow, std::span<const std::int32_t> initial_stack) {
		return ExecuteControlProgram(font, code, entry, strict_stack_underflow, initial_stack, nullptr);
	}

	TrueTypeControlProgramResult ExecuteTrueTypeStateProgram(const TrueTypeFontProgram& font, TrueTypeProgramCode code, TrueTypeProgramRange entry, TrueTypeInterpreterState& state, bool strict_stack_underflow, std::span<const std::int32_t> initial_stack) {
		if (state.Storage.size() > 0xffff || state.ControlValues.size() > kMaximumStackProgramBytes / sizeof(std::int32_t)) {
			throw std::runtime_error("TrueType state exceeds its bounded storage or CVT owner size");
		}
		if (static_cast<std::uint8_t>(state.Graphics.Round) > 7) {
			throw std::runtime_error("TrueType state requires an original rounding mode");
		}

		state.BackwardCompatibility = state.Metrics.InterpreterVersion == 40 && !state.Metrics.FaceIsTricky && (state.Graphics.InstructionControl & 4) == 0;

		return ExecuteControlProgram(font, code, entry, strict_stack_underflow, initial_stack, &state);
	}

} // namespace sgre::text
