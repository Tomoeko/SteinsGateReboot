#include "sgre/script/source_expression.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <variant>

namespace sgre::script {
	static bool IdentifierStart(unsigned char value) {
		return value == '_' || (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z');
	}

	static bool IdentifierContinue(unsigned char value) {
		return IdentifierStart(value) || (value >= '0' && value <= '9');
	}

	static bool ExpressionSpace(char value) {
		return value == ' ' || value == '\t' || value == '\r' || value == '\n';
	}

	static bool UnsupportedKeyword(std::string_view name) {
		static constexpr std::array<std::string_view, 38> keywords{"while", "do", "if", "else", "break", "continue", "return", "null", "function", "local", "for", "foreach", "in", "typeof", "delegate", "delete", "try", "catch", "throw", "clone", "yield", "resume", "switch", "case", "default", "this", "parent", "class", "extends", "constructor", "instanceof", "vargc", "vargv", "true", "false", "static", "enum", "const"};
		if (name == "this") {
			return false;
		}
		for (const auto keyword : keywords) {
			if (name == keyword) {
				return true;
			}
		}

		return false;
	}

	static SquirrelInstruction ExpressionInstruction(std::uint8_t opcode, std::uint8_t destination, std::int32_t argument1 = 0, std::uint8_t argument2 = 0) {
		SquirrelInstruction instruction;
		instruction.Opcode = opcode;
		instruction.Argument0 = destination;
		instruction.Argument1 = argument1;
		instruction.Argument2 = argument2;

		return instruction;
	}

	std::optional<BytecodeProgram> TryCompileSquirrelNameExpression(SquirrelVm& actual_vm, std::string_view expression) {
		if (expression.empty() || expression.size() > 200) {
			return std::nullopt;
		}
		while (!expression.empty() && ExpressionSpace(expression.front())) {
			expression.remove_prefix(1);
		}
		while (!expression.empty() && ExpressionSpace(expression.back())) {
			expression.remove_suffix(1);
		}
		if (expression.empty() || !IdentifierStart(static_cast<unsigned char>(expression.front())) || UnsupportedKeyword(expression)) {
			return std::nullopt;
		}
		for (const auto value : expression) {
			if (!IdentifierContinue(static_cast<unsigned char>(value))) {
				return std::nullopt;
			}
		}
		if (expression != "this" && actual_vm.FindRawSlot(actual_vm.ConstantTable(), SquirrelString(std::string(expression)))) {
			// Constant-table literal/type emission is a separate native compiler branch.
			return std::nullopt;
		}

		BytecodeProgram program;
		auto& function = program.MainFunction;
		function.SourceName = std::string("execscript");
		function.Name = std::string("main");
		function.Parameters.emplace_back(std::string("this"));
		function.StackSize = expression == "this" ? 1 : 2;
		function.HasVariableParameters = true;
		if (expression == "this") {
			function.Instructions.push_back(ExpressionInstruction(19, 1, 0));
		} else {
			function.Literals.emplace_back(std::string(expression));
			function.Instructions.push_back(ExpressionInstruction(9, 1, 0, 0));
			function.Instructions.push_back(ExpressionInstruction(19, 1, 1));
		}
		function.Instructions.push_back(ExpressionInstruction(19, 255));

		return program;
	}
} // namespace sgre::script
