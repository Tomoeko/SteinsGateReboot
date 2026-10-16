#include "sgre/text/truetype_program.hpp"

#include <algorithm>
#include <limits>
#include <string>

namespace sgre::text {

	static constexpr std::size_t kMaximumProgramBytes = 1024 * 1024;
	static constexpr std::uint32_t kMaximumExecutedInstructions = 1000000;
	static constexpr std::uint32_t kNativeExtraStackCells = 32;

	TrueTypeProgramError::TrueTypeProgramError(TrueTypeProgramFailure failure, std::uint32_t offset, std::string_view detail) : std::runtime_error("TrueType program error " + std::to_string(static_cast<std::uint32_t>(failure)) + " at offset " + std::to_string(offset) + ": " + std::string(detail)), m_Failure(failure), m_Offset(offset) {
	}

	TrueTypeProgramFailure TrueTypeProgramError::Failure() const {
		return m_Failure;
	}

	std::uint32_t TrueTypeProgramError::Offset() const {
		return m_Offset;
	}

	bool TrueTypeInstruction::IsPush() const {
		return Opcode == 0x40 || Opcode == 0x41 || (Opcode >= 0xb0 && Opcode <= 0xbf);
	}

	std::int32_t TrueTypeInstruction::PushValue(std::size_t index) const {
		const std::size_t width = PushWords ? 2 : 1;
		if (!IsPush() || index >= PushCount || PushData.size() != std::size_t(PushCount) * width) {
			throw TrueTypeProgramError(TrueTypeProgramFailure::CodeOverflow, Offset, "PUSH operand is outside its decoded inline data");
		}

		const auto first = std::uint32_t(PushData[index * width]);
		if (!PushWords) {
			return static_cast<std::int32_t>(first);
		}

		const auto value = first * 256 + PushData[index * width + 1];

		return value < 0x8000 ? static_cast<std::int32_t>(value) : static_cast<std::int32_t>(value) - 65536;
	}

	TrueTypeInstruction DecodeTrueTypeInstruction(std::span<const std::uint8_t> program, std::uint32_t offset) {
		if (program.size() > std::size_t(std::numeric_limits<std::int32_t>::max()) || offset >= program.size()) {
			throw TrueTypeProgramError(TrueTypeProgramFailure::CodeOverflow, offset, "instruction start is outside its signed native code extent");
		}

		TrueTypeInstruction instruction;
		instruction.Offset = offset;
		instruction.Opcode = program[offset];
		instruction.ByteCount = 1;
		std::size_t data_offset = std::size_t(offset) + 1;
		if (instruction.Opcode == 0x40 || instruction.Opcode == 0x41) {
			if (data_offset == program.size()) {
				throw TrueTypeProgramError(TrueTypeProgramFailure::CodeOverflow, offset, "NPUSH is missing its count byte");
			}

			instruction.PushCount = program[data_offset++];
			instruction.PushWords = instruction.Opcode == 0x41;
		} else if (instruction.Opcode >= 0xb0 && instruction.Opcode <= 0xbf) {
			instruction.PushCount = static_cast<std::uint16_t>((instruction.Opcode & 7) + 1);
			instruction.PushWords = instruction.Opcode >= 0xb8;
		} else {
			return instruction;
		}

		const std::size_t data_bytes = std::size_t(instruction.PushCount) * (instruction.PushWords ? 2 : 1);
		if (data_bytes > program.size() - data_offset) {
			throw TrueTypeProgramError(TrueTypeProgramFailure::CodeOverflow, offset, "PUSH inline data exceeds its code extent");
		}

		instruction.PushData = program.subspan(data_offset, data_bytes);
		instruction.ByteCount = static_cast<std::uint32_t>(data_offset - offset + data_bytes);

		return instruction;
	}

	TrueTypeDefinitionLimits ReadTrueTypeDefinitionLimits(std::span<const std::uint8_t> maxp) {
		if (maxp.size() < 32 || maxp[0] != 0 || maxp[1] != 1 || maxp[2] != 0 || maxp[3] != 0) {
			throw std::runtime_error("TrueType program requires its complete version1 maxp table");
		}

		TrueTypeDefinitionLimits limits;
		limits.MaxFunctionDefinitions = static_cast<std::uint16_t>(std::uint32_t(maxp[20]) * 256 + maxp[21]);
		limits.MaxStackElements = static_cast<std::uint16_t>(std::uint32_t(maxp[24]) * 256 + maxp[25]);

		return limits;
	}

	TrueTypeFontProgram::TrueTypeFontProgram(std::span<const std::uint8_t> program, TrueTypeDefinitionLimits limits) : m_Program(program), m_Limits(limits), m_StackCapacity(std::uint32_t(limits.MaxStackElements) + kNativeExtraStackCells) {
		if (program.size() > kMaximumProgramBytes) {
			throw std::runtime_error("TrueType font definition program exceeds its bounded preparation size");
		}

		m_Stack.reserve(m_StackCapacity);
		m_Definitions.reserve(limits.MaxFunctionDefinitions);
		std::uint32_t offset = 0;
		while (offset < m_Program.size()) {
			const auto instruction = DecodeTrueTypeInstruction(m_Program, offset);
			if (instruction.IsPush()) {
				Push(instruction);
				offset += instruction.ByteCount;
			} else if (instruction.Opcode == 0x2c) {
				offset = RegisterDefinition(instruction);
			} else if (instruction.Opcode == 0x2d) {
				throw TrueTypeProgramError(TrueTypeProgramFailure::EndWithoutCall, offset, "ENDF has no actual call frame");
			} else {
				throw std::runtime_error("TrueType font definition opcode " + std::to_string(instruction.Opcode) + " at offset " + std::to_string(offset) + " requires recovered interpreter execution");
			}

			++m_ExecutedInstructions;
			if (m_ExecutedInstructions > kMaximumExecutedInstructions) {
				throw TrueTypeProgramError(TrueTypeProgramFailure::InstructionLimit, instruction.Offset, "executed instruction count exceeds the original limit");
			}
		}
	}

	void TrueTypeFontProgram::Push(const TrueTypeInstruction& instruction) {
		if (instruction.PushCount > m_StackCapacity - m_Stack.size()) {
			throw TrueTypeProgramError(TrueTypeProgramFailure::StackOverflow, instruction.Offset, "PUSH exceeds the actual operand stack capacity");
		}

		for (std::size_t index = 0; index < instruction.PushCount; ++index) {
			m_Stack.push_back(instruction.PushValue(index));
		}
	}

	std::uint32_t TrueTypeFontProgram::RegisterDefinition(const TrueTypeInstruction& instruction) {
		std::int32_t identifier = 0;
		if (m_Stack.empty()) {
			if (m_Limits.StrictStackUnderflow) {
				throw TrueTypeProgramError(TrueTypeProgramFailure::StackUnderflow, instruction.Offset, "FDEF is missing its fixed operand");
			}
		} else {
			identifier = m_Stack.back();
			m_Stack.pop_back();
		}

		// Native searches the existing storage order before applying new capacity.
		auto found = std::find_if(m_Definitions.begin(), m_Definitions.end(), [identifier](const auto& definition) {
			return definition.Identifier == static_cast<std::uint32_t>(identifier);
		});
		if (found == m_Definitions.end()) {
			if (m_Definitions.size() == m_Limits.MaxFunctionDefinitions) {
				throw TrueTypeProgramError(TrueTypeProgramFailure::InvalidDefinition, instruction.Offset, "new function exceeds the actual definition capacity");
			}

			m_Definitions.emplace_back();
			found = m_Definitions.end() - 1;
		}
		if (identifier < 0 || identifier > 65535) {
			throw TrueTypeProgramError(TrueTypeProgramFailure::InvalidDefinition, instruction.Offset, "function identifier is outside unsigned16");
		}

		found->ProgramRange = 1;
		found->StartOffset = instruction.Offset + instruction.ByteCount;
		found->Identifier = static_cast<std::uint32_t>(identifier);
		found->Active = 1;
		found->Auxiliary = 0;
		m_MaximumIdentifier = std::max(m_MaximumIdentifier, found->Identifier);
		auto offset = found->StartOffset;
		while (offset < m_Program.size()) {
			const auto body = DecodeTrueTypeInstruction(m_Program, offset);
			if (body.Opcode == 0x2c || body.Opcode == 0x89) {
				throw TrueTypeProgramError(TrueTypeProgramFailure::NestedDefinition, offset, "FDEF body contains a nested definition");
			}
			if (body.Opcode == 0x2d) {
				found->EndOffset = offset;

				return offset + body.ByteCount;
			}

			offset += body.ByteCount;
		}

		throw TrueTypeProgramError(TrueTypeProgramFailure::CodeOverflow, offset, "FDEF body has no bounded ENDF");
	}

	std::span<const std::uint8_t> TrueTypeFontProgram::Program() const {
		return m_Program;
	}

	std::span<const TrueTypeFunctionDefinition> TrueTypeFontProgram::Definitions() const {
		return m_Definitions;
	}

	std::span<const std::int32_t> TrueTypeFontProgram::Stack() const {
		return m_Stack;
	}

	std::uint32_t TrueTypeFontProgram::StackCapacity() const {
		return m_StackCapacity;
	}

	std::uint32_t TrueTypeFontProgram::FunctionCapacity() const {
		return m_Limits.MaxFunctionDefinitions;
	}

	std::uint32_t TrueTypeFontProgram::MaximumFunctionIdentifier() const {
		return m_MaximumIdentifier;
	}

	std::uint32_t TrueTypeFontProgram::ExecutedInstructions() const {
		return m_ExecutedInstructions;
	}

	const TrueTypeFunctionDefinition* TrueTypeFontProgram::FindFunction(std::uint32_t identifier) const {
		if (identifier > m_MaximumIdentifier) {
			return nullptr;
		}

		if (m_MaximumIdentifier + 1 == m_Definitions.size() && identifier < m_Definitions.size()) {
			const auto& definition = m_Definitions[identifier];
			if (definition.Identifier == identifier) {
				return definition.Active != 0 ? &definition : nullptr;
			}
		}

		const auto found = std::find_if(m_Definitions.begin(), m_Definitions.end(), [identifier](const auto& definition) {
			return definition.Identifier == identifier;
		});

		return found != m_Definitions.end() && found->Active != 0 ? &*found : nullptr;
	}

} // namespace sgre::text
