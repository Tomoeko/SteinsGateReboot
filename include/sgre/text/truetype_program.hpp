#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace sgre::text {

	enum class TrueTypeProgramFailure : std::uint32_t {
		StackUnderflow = 0x81,
		StackOverflow = 0x82,
		CodeOverflow = 0x83,
		InvalidArgument = 0x84,
		DivideByZero = 0x85,
		InvalidReference = 0x86,
		EndWithoutCall = 0x88,
		NestedDefinition = 0x89,
		MissingCodeRange = 0x8a,
		InstructionLimit = 0x8b,
		InvalidDefinition = 0x8c
	};

	class TrueTypeProgramError : public std::runtime_error {
	public:
		TrueTypeProgramError(TrueTypeProgramFailure failure, std::uint32_t offset, std::string_view detail);
		[[nodiscard]] TrueTypeProgramFailure Failure() const;
		[[nodiscard]] std::uint32_t Offset() const;

	private:
		TrueTypeProgramFailure m_Failure;
		std::uint32_t m_Offset;
	};

	struct TrueTypeInstruction {
		std::uint32_t Offset = 0;
		std::uint32_t ByteCount = 0;
		std::uint8_t Opcode = 0;
		std::uint16_t PushCount = 0;
		bool PushWords = false;
		// The immutable original program outlives this view.
		std::span<const std::uint8_t> PushData;
		[[nodiscard]] bool IsPush() const;
		[[nodiscard]] std::int32_t PushValue(std::size_t index) const;
	};

	// Decodes length and inline PUSH operands; it does not execute an instruction.
	[[nodiscard]] TrueTypeInstruction DecodeTrueTypeInstruction(std::span<const std::uint8_t> program, std::uint32_t offset);

	struct TrueTypeDefinitionLimits {
		std::uint16_t MaxStackElements = 0;
		std::uint16_t MaxFunctionDefinitions = 0;
		// Native non-pedantic execution fills missing fixed operands with zero.
		bool StrictStackUnderflow = false;
	};

	[[nodiscard]] TrueTypeDefinitionLimits ReadTrueTypeDefinitionLimits(std::span<const std::uint8_t> maxp);

	struct TrueTypeFunctionDefinition {
		std::uint32_t ProgramRange = 1;
		std::uint32_t StartOffset = 0;
		// Points at the actual ENDF opcode, excluding it from the body view.
		std::uint32_t EndOffset = 0;
		std::uint32_t Identifier = 0;
		std::uint16_t Active = 0;
		std::uint32_t Auxiliary = 0;
		bool operator==(const TrueTypeFunctionDefinition&) const = default;
	};

	class TrueTypeFontProgram {
	public:
		// Executes the recovered PUSH/FDEF declaration domain. The immutable fpgm
		// bytes outlive this owner and all views returned from it.
		TrueTypeFontProgram(std::span<const std::uint8_t> program, TrueTypeDefinitionLimits limits);
		[[nodiscard]] std::span<const std::uint8_t> Program() const;
		[[nodiscard]] std::span<const TrueTypeFunctionDefinition> Definitions() const;
		[[nodiscard]] std::span<const std::int32_t> Stack() const;
		[[nodiscard]] std::uint32_t StackCapacity() const;
		[[nodiscard]] std::uint32_t FunctionCapacity() const;
		[[nodiscard]] std::uint32_t MaximumFunctionIdentifier() const;
		[[nodiscard]] std::uint32_t ExecutedInstructions() const;
		[[nodiscard]] const TrueTypeFunctionDefinition* FindFunction(std::uint32_t identifier) const;

	private:
		void Push(const TrueTypeInstruction& instruction);
		[[nodiscard]] std::uint32_t RegisterDefinition(const TrueTypeInstruction& instruction);
		std::span<const std::uint8_t> m_Program;
		TrueTypeDefinitionLimits m_Limits;
		std::vector<TrueTypeFunctionDefinition> m_Definitions;
		std::vector<std::int32_t> m_Stack;
		std::uint32_t m_StackCapacity = 0;
		std::uint32_t m_MaximumIdentifier = 0;
		std::uint32_t m_ExecutedInstructions = 0;
	};

} // namespace sgre::text
