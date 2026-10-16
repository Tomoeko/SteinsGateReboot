#include "sgre/script/source_compiler.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace sgre::script {
	static constexpr std::size_t kMaximumHexadecimalDigits = 8;
	static constexpr std::size_t kMaximumSourceBytes = 1024 * 1024;
	static constexpr std::size_t kMaximumCompilerDepth = 64;
	static constexpr std::size_t kMaximumCompilerInstructions = 16384;
	static constexpr std::size_t kMaximumCompilerFunctions = 4096;
	static constexpr std::size_t kMaximumCompilerLiterals = 4096;
	static constexpr std::size_t kMaximumCompilerRegisters = 255;

	enum class SourceTokenKind {
		End,
		Identifier,
		String,
		Integer,
		LeftParenthesis,
		RightParenthesis,
		LeftBrace,
		RightBrace,
		LeftBracket,
		RightBracket,
		Dot,
		Comma,
		Semicolon,
		Equal,
		NotEqual,
		Less,
		Greater,
		LessEqual,
		GreaterEqual,
		Not,
		And,
		Or,
		Question,
		Colon,
		Root
	};

	struct SourceToken {
		SourceTokenKind Kind = SourceTokenKind::End;
		std::string Text;
		std::size_t Offset = 0;
		bool NewlineBefore = false;
	};

	static bool SourceHexDigit(char value) {
		return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') || (value >= 'A' && value <= 'F');
	}

	static bool SourceIdentifierStart(char value) {
		return value == '_' || (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z');
	}

	static bool SourceIdentifierContinue(char value) {
		return SourceIdentifierStart(value) || (value >= '0' && value <= '9');
	}

	static bool SourceKeyword(std::string_view value) {
		static constexpr std::array<std::string_view, 39> keywords{"while", "do", "if", "else", "break", "continue", "return", "null", "function", "local", "for", "foreach", "in", "typeof", "delegate", "delete", "try", "catch", "throw", "clone", "yield", "resume", "switch", "case", "default", "this", "parent", "class", "extends", "constructor", "instanceof", "vargc", "vargv", "true", "false", "static", "enum", "const", "base"};
		return std::find(keywords.begin(), keywords.end(), value) != keywords.end();
	}

	class SourceLexer {
	public:
		SourceLexer(std::string_view source, std::string_view name)
		    : m_Source(source), m_Name(name) {
		}

		[[noreturn]] void Fail(std::size_t offset, std::string_view reason) const {
			const auto bounded = std::min(offset, m_Source.size());
			const auto line = static_cast<std::size_t>(std::count(m_Source.begin(), m_Source.begin() + static_cast<std::ptrdiff_t>(bounded), '\n')) + 1;
			throw std::runtime_error(std::string(m_Name) + ":" + std::to_string(line) + ": source compiler at byte " + std::to_string(offset) + ": " + std::string(reason));
		}

		SourceToken Next() {
			SourceToken token;
			token.NewlineBefore = SkipWhitespace();
			token.Offset = m_Position;
			if (m_Position == m_Source.size()) {
				return token;
			}
			const auto value = m_Source[m_Position++];
			if (SourceIdentifierStart(value)) {
				token.Kind = SourceTokenKind::Identifier;
				while (m_Position < m_Source.size() && SourceIdentifierContinue(m_Source[m_Position])) {
					++m_Position;
				}
				token.Text = m_Source.substr(token.Offset, m_Position - token.Offset);
				return token;
			}
			if (value >= '0' && value <= '9') {
				token.Kind = SourceTokenKind::Integer;
				if (value == '0' && m_Position < m_Source.size() && (m_Source[m_Position] == 'x' || m_Source[m_Position] == 'X')) {
					const auto digits_begin = ++m_Position;
					while (m_Position < m_Source.size() && SourceHexDigit(m_Source[m_Position])) {
						++m_Position;
					}
					if (m_Position - digits_begin > kMaximumHexadecimalDigits) {
						Fail(token.Offset, "too many digits for a hexadecimal integer");
					}
				} else {
					while (m_Position < m_Source.size() && m_Source[m_Position] >= '0' && m_Source[m_Position] <= '9') {
						++m_Position;
					}
				}
				token.Text = m_Source.substr(token.Offset, m_Position - token.Offset);
				return token;
			}
			if (value == '"') {
				token.Kind = SourceTokenKind::String;
				token.Text = ReadString(token.Offset);
				return token;
			}

			switch (value) {
			case '(':
				token.Kind = SourceTokenKind::LeftParenthesis;
				break;
			case ')':
				token.Kind = SourceTokenKind::RightParenthesis;
				break;
			case '{':
				token.Kind = SourceTokenKind::LeftBrace;
				break;
			case '}':
				token.Kind = SourceTokenKind::RightBrace;
				break;
			case '[':
				token.Kind = SourceTokenKind::LeftBracket;
				break;
			case ']':
				token.Kind = SourceTokenKind::RightBracket;
				break;
			case '.':
				token.Kind = SourceTokenKind::Dot;
				break;
			case ',':
				token.Kind = SourceTokenKind::Comma;
				break;
			case ';':
				token.Kind = SourceTokenKind::Semicolon;
				break;
			case '=':
				if (m_Position == m_Source.size() || m_Source[m_Position++] != '=') {
					Fail(token.Offset, "unsupported operator in recovered expression grammar");
				}
				token.Kind = SourceTokenKind::Equal;
				break;
			case '<':
			case '>':
				token.Kind = value == '<' ? SourceTokenKind::Less : SourceTokenKind::Greater;
				if (m_Position < m_Source.size() && m_Source[m_Position] == '=') {
					++m_Position;
					token.Kind = value == '<' ? SourceTokenKind::LessEqual : SourceTokenKind::GreaterEqual;
				}
				break;
			case '!':
				token.Kind = SourceTokenKind::Not;
				if (m_Position < m_Source.size() && m_Source[m_Position] == '=') {
					++m_Position;
					token.Kind = SourceTokenKind::NotEqual;
				}
				break;
			case '&':
			case '|':
				if (m_Position == m_Source.size() || m_Source[m_Position++] != value) {
					Fail(token.Offset, "unsupported bitwise operator in recovered expression grammar");
				}
				token.Kind = value == '&' ? SourceTokenKind::And : SourceTokenKind::Or;
				break;
			case '?':
				token.Kind = SourceTokenKind::Question;
				break;
			case ':':
				token.Kind = SourceTokenKind::Colon;
				if (m_Position < m_Source.size() && m_Source[m_Position] == ':') {
					++m_Position;
					token.Kind = SourceTokenKind::Root;
				}
				break;
			default:
				Fail(token.Offset, "unsupported token in recovered expression grammar");
			}
			return token;
		}

	private:
		bool SkipWhitespace() {
			bool newline = false;
			while (m_Position < m_Source.size()) {
				const auto value = m_Source[m_Position];
				if (value == ' ' || value == '\t' || value == '\r' || value == '\n') {
					newline = newline || value == '\n';
					++m_Position;
					continue;
				}
				if (m_Source.substr(m_Position, 2) == "//") {
					const auto end = m_Source.find('\n', m_Position + 2);
					newline = newline || end != std::string_view::npos;
					m_Position = end == std::string_view::npos ? m_Source.size() : end + 1;
					continue;
				}
				if (m_Source.substr(m_Position, 2) == "/*") {
					const auto end = m_Source.find("*/", m_Position + 2);
					if (end == std::string_view::npos) {
						Fail(m_Position, "unterminated block comment");
					}
					m_Position = end + 2;
					continue;
				}
				break;
			}
			return newline;
		}

		std::string ReadString(std::size_t start) {
			std::string result;
			while (m_Position < m_Source.size()) {
				auto value = m_Source[m_Position++];
				if (value == '"') {
					return result;
				}
				if (value == '\n' || value == '\r') {
					Fail(start, "newline in string literal");
				}
				if (value == '\\') {
					if (m_Position == m_Source.size()) {
						Fail(start, "unterminated string escape");
					}
					value = m_Source[m_Position++];
					switch (value) {
					case 'a':
						value = '\a';
						break;
					case 'b':
						value = '\b';
						break;
					case 'f':
						value = '\f';
						break;
					case 'n':
						value = '\n';
						break;
					case 'r':
						value = '\r';
						break;
					case 't':
						value = '\t';
						break;
					case 'v':
						value = '\v';
						break;
					case '0':
						value = '\0';
						break;
					case '\\':
					case '"':
					case '\'':
						break;
					default:
						Fail(m_Position - 1, "unsupported string escape in recovered grammar");
					}
				}
				result.push_back(value);
			}
			Fail(start, "unterminated string literal");
		}

		std::string_view m_Source;
		std::string_view m_Name;
		std::size_t m_Position = 0;
	};

	struct SourceFunctionState {
		SquirrelFunction Function;
		SourceFunctionState* Parent = nullptr;
		std::vector<std::string> Locals{"this"};
		std::size_t NextRegister = 1;
	};

	struct SourceOperand {
		std::uint8_t Register = 0;
		bool IsSlot = false;
		bool LiteralKey = false;
		std::int32_t Key = 0;
	};

	class SourceCompiler {
	public:
		SourceCompiler(SquirrelVm& vm, std::string_view source, std::string_view name)
		    : m_Vm(vm), m_Lexer(source, name), m_Name(name), m_Token(m_Lexer.Next()) {
		}

		BytecodeProgram Compile() {
			BytecodeProgram result;
			SourceFunctionState main;
			main.Function.Name = std::string("main");
			main.Function.SourceName = m_Name;
			main.Function.Parameters.emplace_back(std::string("this"));
			main.Function.HasVariableParameters = true;
			main.Function.StackSize = 1;
			CompileStatements(main, SourceTokenKind::End);
			result.MainFunction = std::move(main.Function);
			return result;
		}

	private:
		[[noreturn]] void Fail(std::string_view message) const {
			m_Lexer.Fail(m_Token.Offset, message);
		}

		bool Accept(SourceTokenKind kind) {
			if (m_Token.Kind != kind) {
				return false;
			}
			m_Token = m_Lexer.Next();
			return true;
		}

		void Require(SourceTokenKind kind, std::string_view reason) {
			if (!Accept(kind)) {
				Fail(reason);
			}
		}

		std::uint8_t Allocate(SourceFunctionState& state, std::size_t count = 1) {
			if (state.NextRegister > kMaximumCompilerRegisters || count > kMaximumCompilerRegisters - state.NextRegister) {
				Fail("register limit exceeded");
			}
			const auto first = static_cast<std::uint8_t>(state.NextRegister);
			state.NextRegister += count;
			state.Function.StackSize = std::max(state.Function.StackSize, static_cast<std::uint32_t>(state.NextRegister));
			return first;
		}

		void Emit(SourceFunctionState& state, std::uint8_t opcode, std::uint8_t destination = 0, std::int32_t argument1 = 0, std::uint8_t argument2 = 0, std::uint8_t argument3 = 0) {
			if (++m_InstructionCount > kMaximumCompilerInstructions) {
				Fail("instruction limit exceeded");
			}
			SquirrelInstruction instruction;
			instruction.Opcode = opcode;
			instruction.Argument0 = destination;
			instruction.Argument1 = argument1;
			instruction.Argument2 = argument2;
			instruction.Argument3 = argument3;
			state.Function.Instructions.push_back(instruction);
		}

		std::int32_t Literal(SourceFunctionState& state, SquirrelLiteral value) {
			const auto found = std::find(state.Function.Literals.begin(), state.Function.Literals.end(), value);
			if (found != state.Function.Literals.end()) {
				return static_cast<std::int32_t>(found - state.Function.Literals.begin());
			}
			if (state.Function.Literals.size() == kMaximumCompilerLiterals) {
				Fail("literal limit exceeded");
			}
			const auto index = static_cast<std::int32_t>(state.Function.Literals.size());
			state.Function.Literals.push_back(std::move(value));
			return index;
		}

		std::uint8_t Materialize(SourceFunctionState& state, SourceOperand operand) {
			if (!operand.IsSlot) {
				return operand.Register;
			}
			const auto destination = Allocate(state);
			if (operand.LiteralKey) {
				Emit(state, 9, destination, operand.Key, operand.Register);
			} else {
				Emit(state, 14, destination, operand.Register, static_cast<std::uint8_t>(operand.Key));
			}
			return destination;
		}

		SourceOperand LoadConstant(SourceFunctionState& state, const SquirrelValue& value) {
			SourceOperand operand;
			operand.Register = Allocate(state);
			if (const auto* integer = std::get_if<std::int32_t>(&value)) {
				Emit(state, 2, operand.Register, *integer);
			} else if (const auto* boolean = std::get_if<bool>(&value)) {
				Emit(state, 22, operand.Register, *boolean ? 1 : 0);
			} else if (const auto* text = TryStringText(value)) {
				Emit(state, 1, operand.Register, Literal(state, *text));
			} else if (const auto* number = std::get_if<float>(&value)) {
				Emit(state, 1, operand.Register, Literal(state, *number));
			} else if (std::holds_alternative<std::monostate>(value)) {
				Emit(state, 20, operand.Register, 1);
			} else {
				Fail("constant-table value requires unrecovered object constant semantics");
			}
			return operand;
		}

		SourceOperand Identifier(SourceFunctionState& state, std::string name) {
			const auto local = std::find(state.Locals.begin(), state.Locals.end(), name);
			if (local != state.Locals.end()) {
				SourceOperand result;
				result.Register = static_cast<std::uint8_t>(local - state.Locals.begin());
				return result;
			}
			if (SourceKeyword(name)) {
				Fail("unsupported keyword in recovered expression grammar");
			}
			if (const auto constant = m_Vm.FindRawSlot(m_Vm.ConstantTable(), SquirrelString(name))) {
				if (m_Vm.Type(*constant) != SquirrelValueType::Table) {
					return LoadConstant(state, *constant);
				}
				// Native25fcab resolves one enum member before emitting its primitive literal.
				Require(SourceTokenKind::Dot, "constant table requires '.' and member identifier");
				if (m_Token.Kind != SourceTokenKind::Identifier || (SourceKeyword(m_Token.Text) && m_Token.Text != "constructor")) {
					Fail("constant member requires identifier");
				}
				const auto member_name = m_Token.Text;
				m_Token = m_Lexer.Next();
				const auto member = m_Vm.FindRawSlot(*constant, SquirrelString(member_name));
				if (!member) {
					Fail("invalid constant [" + name + "." + member_name + "]");
				}
				return LoadConstant(state, *member);
			}
			SourceOperand result;
			result.IsSlot = true;
			result.LiteralKey = true;
			result.Key = Literal(state, std::move(name));
			return result;
		}

		SourceOperand Function(SourceFunctionState& parent) {
			if (++m_FunctionCount > kMaximumCompilerFunctions) {
				Fail("function limit exceeded");
			}
			SourceFunctionState child;
			child.Parent = &parent;
			child.Function.SourceName = m_Name;
			child.Function.Parameters.emplace_back(std::string("this"));
			Require(SourceTokenKind::LeftParenthesis, "anonymous function requires parameter list");
			if (m_Token.Kind != SourceTokenKind::RightParenthesis) {
				do {
					if (m_Token.Kind != SourceTokenKind::Identifier || SourceKeyword(m_Token.Text) || std::find(child.Locals.begin(), child.Locals.end(), m_Token.Text) != child.Locals.end()) {
						Fail("function requires unique non-keyword parameter names");
					}
					child.Function.Parameters.emplace_back(m_Token.Text);
					child.Locals.push_back(m_Token.Text);
					static_cast<void>(Allocate(child));
					m_Token = m_Lexer.Next();
				} while (Accept(SourceTokenKind::Comma));
			}
			Require(SourceTokenKind::RightParenthesis, "function parameter list requires closing parenthesis");
			Require(SourceTokenKind::LeftBrace, "function body requires opening brace");
			child.Function.StackSize = std::max(child.Function.StackSize, static_cast<std::uint32_t>(child.NextRegister));
			CompileStatements(child, SourceTokenKind::RightBrace);
			Require(SourceTokenKind::RightBrace, "function body requires closing brace");
			const auto child_index = static_cast<std::int32_t>(parent.Function.Functions.size());
			parent.Function.Functions.push_back(std::move(child.Function));
			SourceOperand result;
			result.Register = Allocate(parent);
			Emit(parent, 48, result.Register, child_index);
			return result;
		}

		SourceOperand Primary(SourceFunctionState& state) {
			if (Accept(SourceTokenKind::Root)) {
				if (m_Token.Kind != SourceTokenKind::Identifier || SourceKeyword(m_Token.Text)) {
					Fail("root access requires identifier");
				}
				SourceOperand result;
				result.Register = Allocate(state);
				Emit(state, 21, result.Register);
				result.IsSlot = true;
				result.LiteralKey = true;
				result.Key = Literal(state, m_Token.Text);
				m_Token = m_Lexer.Next();
				return result;
			}
			if (Accept(SourceTokenKind::LeftParenthesis)) {
				SourceOperand result;
				result.Register = Materialize(state, CommaExpression(state));
				Require(SourceTokenKind::RightParenthesis, "expression requires closing parenthesis");
				return result;
			}
			const auto token = m_Token;
			if (Accept(SourceTokenKind::String)) {
				return LoadConstant(state, SquirrelString(token.Text));
			}
			if (Accept(SourceTokenKind::Integer)) {
				if (token.Text.size() >= 2 && token.Text.front() == '0' && (token.Text[1] == 'x' || token.Text[1] == 'X')) {
					std::uint32_t bits = 0;
					const auto digits = std::string_view(token.Text).substr(2);
					if (!digits.empty()) {
						const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), bits, 16);
						if (parsed.ec != std::errc{} || parsed.ptr != digits.data() + digits.size()) {
							Fail("hexadecimal integer exceeds its original 32-bit word");
						}
					}
					// Original ReadNumber accumulates a DWORD; Primary stores negative words in the literal pool.
					const auto value = std::bit_cast<std::int32_t>(bits);
					if (value < 0) {
						SourceOperand result;
						result.Register = Allocate(state);
						Emit(state, 1, result.Register, Literal(state, value));

						return result;
					}

					return LoadConstant(state, value);
				}
				std::int32_t value = 0;
				const auto parsed = std::from_chars(token.Text.data(), token.Text.data() + token.Text.size(), value);
				if (parsed.ec != std::errc{} || parsed.ptr != token.Text.data() + token.Text.size() || (token.Text.size() > 1 && token.Text.front() == '0')) {
					Fail("integer literal outside recovered decimal int32 domain");
				}
				return LoadConstant(state, value);
			}
			if (Accept(SourceTokenKind::Identifier)) {
				if (token.Text == "function") {
					return Function(state);
				}
				if (token.Text == "null") {
					return LoadConstant(state, {});
				}
				if (token.Text == "true" || token.Text == "false") {
					return LoadConstant(state, token.Text == "true");
				}
				return Identifier(state, token.Text);
			}
			Fail("expected expression in recovered grammar");
		}

		SourceOperand Call(SourceFunctionState& state, SourceOperand target) {
			const auto callable = Allocate(state);
			const auto receiver = Allocate(state);
			if (target.IsSlot) {
				Emit(state, target.LiteralKey ? 8 : 7, callable, target.Key, target.Register, receiver);
			} else {
				Emit(state, 10, callable, target.Register);
				Emit(state, 10, receiver, 0);
			}
			std::vector<std::uint8_t> arguments;
			if (m_Token.Kind != SourceTokenKind::RightParenthesis) {
				do {
					arguments.push_back(Materialize(state, Expression(state)));
				} while (Accept(SourceTokenKind::Comma));
			}
			Require(SourceTokenKind::RightParenthesis, "call requires closing parenthesis");
			const auto first = Allocate(state, arguments.size() + 1);
			Emit(state, 10, first, receiver);
			for (std::size_t index = 0; index < arguments.size(); ++index) {
				Emit(state, 10, static_cast<std::uint8_t>(static_cast<std::size_t>(first) + index + 1), arguments[index]);
			}
			SourceOperand result;
			result.Register = Allocate(state);
			Emit(state, 6, result.Register, callable, first, static_cast<std::uint8_t>(arguments.size() + 1));
			return result;
		}

		SourceOperand Postfix(SourceFunctionState& state) {
			auto operand = Primary(state);
			while (true) {
				if (Accept(SourceTokenKind::Dot)) {
					if (m_Token.Kind != SourceTokenKind::Identifier || SourceKeyword(m_Token.Text)) {
						Fail("member access requires identifier");
					}
					const auto object = Materialize(state, operand);
					operand.Register = object;
					operand.IsSlot = true;
					operand.LiteralKey = true;
					operand.Key = Literal(state, m_Token.Text);
					m_Token = m_Lexer.Next();
				} else if (m_Token.Kind == SourceTokenKind::LeftBracket) {
					if (m_Token.NewlineBefore) {
						Fail("newline cannot precede indexed dereference");
					}
					static_cast<void>(Accept(SourceTokenKind::LeftBracket));
					const auto object = Materialize(state, operand);
					const auto key = Materialize(state, Expression(state));
					Require(SourceTokenKind::RightBracket, "indexed access requires closing bracket");
					operand.Register = object;
					operand.IsSlot = true;
					operand.LiteralKey = false;
					operand.Key = key;
				} else if (Accept(SourceTokenKind::LeftParenthesis)) {
					operand = Call(state, operand);
				} else {
					return operand;
				}
			}
		}

		void EnterExpressionDepth() {
			if (++m_Depth > kMaximumCompilerDepth) {
				Fail("expression/function depth limit exceeded");
			}
		}

		void PatchBranch(SourceFunctionState& state, std::size_t branch) {
			state.Function.Instructions[branch].Argument1 = static_cast<std::int32_t>(state.Function.Instructions.size() - branch - 1);
		}

		SourceOperand Unary(SourceFunctionState& state) {
			if (!Accept(SourceTokenKind::Not)) {
				return Postfix(state);
			}
			EnterExpressionDepth();
			const auto value = Materialize(state, Unary(state));
			--m_Depth;
			SourceOperand result;
			result.Register = Allocate(state);
			Emit(state, 46, result.Register, value);
			return result;
		}

		SourceOperand Comparison(SourceFunctionState& state) {
			auto left = Unary(state);
			while (m_Token.Kind == SourceTokenKind::Equal || m_Token.Kind == SourceTokenKind::NotEqual || m_Token.Kind == SourceTokenKind::Less || m_Token.Kind == SourceTokenKind::Greater || m_Token.Kind == SourceTokenKind::LessEqual || m_Token.Kind == SourceTokenKind::GreaterEqual) {
				const auto operation = m_Token.Kind;
				m_Token = m_Lexer.Next();
				const auto left_value = Materialize(state, left);
				const auto right_value = Materialize(state, Unary(state));
				left = {};
				left.Register = Allocate(state);
				if (operation == SourceTokenKind::Equal || operation == SourceTokenKind::NotEqual) {
					Emit(state, operation == SourceTokenKind::Equal ? 15 : 16, left.Register, right_value, left_value);
				} else {
					std::uint8_t relation = 0;
					if (operation == SourceTokenKind::Less) {
						relation = 3;
					} else if (operation == SourceTokenKind::LessEqual) {
						relation = 4;
					} else if (operation == SourceTokenKind::GreaterEqual) {
						relation = 2;
					}
					Emit(state, 40, left.Register, right_value, left_value, relation);
				}
			}
			return left;
		}

		SourceOperand LogicalAnd(SourceFunctionState& state) {
			const auto left = Comparison(state);
			if (!Accept(SourceTokenKind::And)) {
				return left;
			}
			const auto left_value = Materialize(state, left);
			SourceOperand result;
			result.Register = Allocate(state);
			const auto branch = state.Function.Instructions.size();
			Emit(state, 43, result.Register, 0, left_value);
			EnterExpressionDepth();
			const auto right_value = Materialize(state, LogicalAnd(state));
			--m_Depth;
			Emit(state, 10, result.Register, right_value);
			PatchBranch(state, branch);
			return result;
		}

		SourceOperand LogicalOr(SourceFunctionState& state) {
			const auto left = LogicalAnd(state);
			if (!Accept(SourceTokenKind::Or)) {
				return left;
			}
			const auto left_value = Materialize(state, left);
			SourceOperand result;
			result.Register = Allocate(state);
			const auto branch = state.Function.Instructions.size();
			Emit(state, 44, result.Register, 0, left_value);
			EnterExpressionDepth();
			const auto right_value = Materialize(state, LogicalOr(state));
			--m_Depth;
			Emit(state, 10, result.Register, right_value);
			PatchBranch(state, branch);
			return result;
		}

		SourceOperand Expression(SourceFunctionState& state) {
			EnterExpressionDepth();
			const auto condition = LogicalOr(state);
			if (!Accept(SourceTokenKind::Question)) {
				--m_Depth;
				return condition;
			}
			const auto condition_value = Materialize(state, condition);
			SourceOperand result;
			result.Register = Allocate(state);
			const auto false_branch = state.Function.Instructions.size();
			Emit(state, 26, condition_value);
			const auto true_value = Materialize(state, Expression(state));
			Emit(state, 10, result.Register, true_value);
			const auto end_branch = state.Function.Instructions.size();
			Emit(state, 24);
			Require(SourceTokenKind::Colon, "conditional expression requires ':'");
			PatchBranch(state, false_branch);
			const auto false_value = Materialize(state, Expression(state));
			Emit(state, 10, result.Register, false_value);
			PatchBranch(state, end_branch);
			--m_Depth;
			return result;
		}

		SourceOperand CommaExpression(SourceFunctionState& state) {
			const auto first_temporary = state.NextRegister;
			auto result = Expression(state);
			while (Accept(SourceTokenKind::Comma)) {
				static_cast<void>(Materialize(state, result));
				// Enclosing arguments and locals remain live below this expression's watermark.
				state.NextRegister = first_temporary;
				result = Expression(state);
			}

			return result;
		}

		void CompileStatements(SourceFunctionState& state, SourceTokenKind ending) {
			while (m_Token.Kind != ending) {
				if (m_Token.Kind == SourceTokenKind::End) {
					Fail("unterminated function body");
				}
				if (Accept(SourceTokenKind::Semicolon)) {
					continue;
				}
				const bool returning = m_Token.Kind == SourceTokenKind::Identifier && m_Token.Text == "return";
				if (returning) {
					m_Token = m_Lexer.Next();
				}
				if (returning && m_Token.NewlineBefore) {
					Emit(state, 19, 255);
					state.NextRegister = state.Locals.size();
					continue;
				}
				if (returning && (m_Token.Kind == ending || m_Token.Kind == SourceTokenKind::Semicolon)) {
					Emit(state, 19, 255);
				} else {
					const auto result = Materialize(state, CommaExpression(state));
					if (returning) {
						if (state.Parent != nullptr && !state.Function.Instructions.empty() && state.Function.Instructions.back().Opcode == 6 && state.Function.Instructions.back().Argument0 == result) {
							state.Function.Instructions.back().Opcode = 5;
						}
						Emit(state, 19, 1, result);
					}
				}
				if (!Accept(SourceTokenKind::Semicolon) && m_Token.Kind != ending) {
					Fail("unsupported statement continuation in recovered grammar");
				}
				state.NextRegister = state.Locals.size();
			}
			Emit(state, 19, 255);
		}

		SquirrelVm& m_Vm;
		SourceLexer m_Lexer;
		std::string m_Name;
		SourceToken m_Token;
		std::size_t m_Depth = 0;
		std::size_t m_InstructionCount = 0;
		std::size_t m_FunctionCount = 0;
	};

	BytecodeProgram CompileSquirrelSource(SquirrelVm& actual_vm, std::string_view source, std::string_view source_name) {
		if (source.size() > kMaximumSourceBytes) {
			throw std::runtime_error("Squirrel source compiler source byte limit exceeded");
		}
		const auto prefix = source.substr(0, source.find('\0'));
		const auto name = source_name.substr(0, source_name.find('\0'));
		SourceCompiler compiler(actual_vm, prefix, name);
		return compiler.Compile();
	}

	SquirrelNativeResult CompileSquirrelString(SquirrelVm& actual_vm, std::span<const SquirrelValue> arguments) {
		if (arguments.size() < 2 || !TryStringText(arguments[1]) || (arguments.size() > 2 && !TryStringText(arguments[2]))) {
			throw std::runtime_error("compilestring requires source and optional source-name strings");
		}
		const auto& source = StringText(arguments[1]);
		const std::string_view name = arguments.size() > 2 ? std::string_view(StringText(arguments[2])) : std::string_view("unnamedbuffer");
		auto program = CompileSquirrelSource(actual_vm, source, name);
		return SquirrelNativeResult::ReturnValue(actual_vm.CreateClosure(std::move(program)));
	}
} // namespace sgre::script
