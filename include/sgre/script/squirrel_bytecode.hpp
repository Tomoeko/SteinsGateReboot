#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace sgre::script {
	using SquirrelLiteral = std::variant<std::monostate, std::int32_t, float, std::string>;

	struct SquirrelInstruction {
		std::int32_t Argument1 = 0;
		std::uint8_t Opcode = 0;
		std::uint8_t Argument0 = 0;
		std::uint8_t Argument2 = 0;
		std::uint8_t Argument3 = 0;
	};

	struct SquirrelOuterBinding {
		std::uint32_t Type = 0;
		SquirrelLiteral Source;
		SquirrelLiteral Name;
	};

	struct SquirrelLocalVariable {
		SquirrelLiteral Name;
		std::uint32_t Position = 0;
		std::uint32_t StartInstruction = 0;
		std::uint32_t EndInstruction = 0;
	};

	struct SquirrelLineInformation {
		std::uint32_t Line = 0;
		std::uint32_t Instruction = 0;
	};

	struct SquirrelFunction {
		SquirrelLiteral SourceName;
		SquirrelLiteral Name;
		std::vector<SquirrelLiteral> Literals;
		std::vector<SquirrelLiteral> Parameters;
		std::vector<SquirrelOuterBinding> OuterBindings;
		std::vector<SquirrelLocalVariable> LocalVariables;
		std::vector<SquirrelLineInformation> LineInformation;
		std::vector<std::uint32_t> DefaultParameters;
		std::vector<SquirrelInstruction> Instructions;
		std::vector<SquirrelFunction> Functions;
		std::uint32_t StackSize = 0;
		bool IsGenerator = false;
		bool HasVariableParameters = false;
	};

	struct BytecodeProgram {
		SquirrelFunction MainFunction;
		std::size_t SerializedSize = 0;
	};

	// Reads the observed little-endian Squirrel 2.2.4 format with 8-bit characters and 32-bit integers/floats. It does not execute bytecode.
	[[nodiscard]] BytecodeProgram ReadSquirrelBytecode(std::span<const std::uint8_t> bytes);
} // namespace sgre::script
