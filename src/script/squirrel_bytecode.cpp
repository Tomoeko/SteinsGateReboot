#include "sgre/script/squirrel_bytecode.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace sgre::script {
	static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);

	static constexpr std::uint32_t kStreamSignature = 0x53514952;
	static constexpr std::uint32_t kPartSignature = 0x50415254;
	static constexpr std::uint32_t kTailSignature = 0x5441494c;
	static constexpr std::size_t kMaxInputBytes = 64 * 1024 * 1024;
	static constexpr std::size_t kMaxStringBytes = 16 * 1024 * 1024;
	static constexpr std::size_t kMaxTotalStringBytes = 32 * 1024 * 1024;
	static constexpr std::size_t kMaxRecords = 1024 * 1024;
	static constexpr std::size_t kMaxFunctions = 65536;
	static constexpr std::size_t kMaxFunctionDepth = 64;
	static constexpr std::uint8_t kMaxOpcode = 0x3c;

	enum class SerializedType : std::uint32_t {
		Null = 0x01000001,
		Integer = 0x05000002,
		Float = 0x05000004,
		String = 0x08000010
	};

	class BytecodeReader {
	public:
		explicit BytecodeReader(std::span<const std::uint8_t> bytes) : m_Bytes(bytes) {
			if (bytes.size() > kMaxInputBytes) {
				Fail("input exceeds the 64 MiB bytecode limit");
			}
		}

		[[nodiscard]] BytecodeProgram Read() {
			if (ReadByte() != 0xfa || ReadByte() != 0xfa) {
				Fail("invalid bytecode stream tag");
			}

			RequireSignature(kStreamSignature, "stream");
			if (ReadWord() != 1) {
				Fail("unsupported character width");
			}

			BytecodeProgram program;
			program.MainFunction = ReadFunction(0);
			RequireSignature(kTailSignature, "tail");
			if (m_Offset != m_Bytes.size()) {
				Fail("trailing bytes after closure");
			}

			program.SerializedSize = m_Offset;

			return program;
		}

	private:
		[[noreturn]] void Fail(std::string_view reason) const {
			throw std::runtime_error("Squirrel bytecode at byte " + std::to_string(m_Offset) + ": " + std::string(reason));
		}

		[[nodiscard]] std::span<const std::uint8_t> ReadRange(std::size_t count) {
			if (count > m_Bytes.size() - m_Offset) {
				Fail("truncated field");
			}

			const auto range = m_Bytes.subspan(m_Offset, count);
			m_Offset += count;

			return range;
		}

		[[nodiscard]] std::uint8_t ReadByte() {
			return ReadRange(1)[0];
		}

		[[nodiscard]] std::uint32_t ReadWord() {
			const auto bytes = ReadRange(4);

			return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8) | (static_cast<std::uint32_t>(bytes[2]) << 16) | (static_cast<std::uint32_t>(bytes[3]) << 24);
		}

		[[nodiscard]] std::size_t ReadCount() {
			const std::uint32_t count = ReadWord();
			if (count > kMaxRecords || count > kMaxRecords - m_RecordCount) {
				Fail("record count exceeds the recovery limit");
			}

			m_RecordCount += count;

			return count;
		}

		void RequireSignature(std::uint32_t expected, std::string_view name) {
			if (ReadWord() != expected) {
				Fail("invalid " + std::string(name) + " signature");
			}
		}

		void RequireAvailableRecords(std::size_t count, std::size_t minimum_bytes) const {
			if (count > (m_Bytes.size() - m_Offset) / minimum_bytes) {
				Fail("record count exceeds remaining bytes");
			}
		}

		[[nodiscard]] SquirrelLiteral ReadLiteral() {
			const auto type = static_cast<SerializedType>(ReadWord());
			if (type == SerializedType::Null) {
				return std::monostate{};
			}

			if (type == SerializedType::Integer) {
				return std::bit_cast<std::int32_t>(ReadWord());
			}

			if (type == SerializedType::Float) {
				return std::bit_cast<float>(ReadWord());
			}

			if (type != SerializedType::String) {
				Fail("unsupported serialized object type");
			}

			const std::uint32_t length = ReadWord();
			if (length > kMaxStringBytes || length > kMaxTotalStringBytes - m_StringBytes) {
				Fail("string length exceeds the recovery limit");
			}

			const auto bytes = ReadRange(length);
			m_StringBytes += length;

			return std::string(bytes.begin(), bytes.end());
		}

		[[nodiscard]] std::vector<SquirrelLiteral> ReadLiterals(std::size_t count) {
			RequireSignature(kPartSignature, "literal section");
			RequireAvailableRecords(count, 4);
			std::vector<SquirrelLiteral> literals;
			literals.reserve(count);
			for (std::size_t index = 0; index < count; ++index) {
				literals.push_back(ReadLiteral());
			}

			return literals;
		}

		[[nodiscard]] std::vector<SquirrelOuterBinding> ReadOuterBindings(std::size_t count) {
			RequireSignature(kPartSignature, "outer binding section");
			RequireAvailableRecords(count, 12);
			std::vector<SquirrelOuterBinding> bindings;
			bindings.reserve(count);
			for (std::size_t index = 0; index < count; ++index) {
				SquirrelOuterBinding binding;
				binding.Type = ReadWord();
				binding.Source = ReadLiteral();
				binding.Name = ReadLiteral();
				bindings.push_back(std::move(binding));
			}

			return bindings;
		}

		[[nodiscard]] std::vector<SquirrelLocalVariable> ReadLocalVariables(std::size_t count) {
			RequireSignature(kPartSignature, "local variable section");
			RequireAvailableRecords(count, 16);
			std::vector<SquirrelLocalVariable> variables;
			variables.reserve(count);
			for (std::size_t index = 0; index < count; ++index) {
				SquirrelLocalVariable variable;
				variable.Name = ReadLiteral();
				variable.Position = ReadWord();
				variable.StartInstruction = ReadWord();
				variable.EndInstruction = ReadWord();
				variables.push_back(std::move(variable));
			}

			return variables;
		}

		[[nodiscard]] std::vector<SquirrelLineInformation> ReadLineInformation(std::size_t count) {
			RequireSignature(kPartSignature, "line information section");
			RequireAvailableRecords(count, 8);
			std::vector<SquirrelLineInformation> lines;
			lines.reserve(count);
			for (std::size_t index = 0; index < count; ++index) {
				SquirrelLineInformation line;
				line.Line = ReadWord();
				line.Instruction = ReadWord();
				lines.push_back(line);
			}

			return lines;
		}

		[[nodiscard]] std::vector<std::uint32_t> ReadDefaultParameters(std::size_t count) {
			RequireSignature(kPartSignature, "default parameter section");
			RequireAvailableRecords(count, 4);
			std::vector<std::uint32_t> parameters;
			parameters.reserve(count);
			for (std::size_t index = 0; index < count; ++index) {
				parameters.push_back(ReadWord());
			}

			return parameters;
		}

		[[nodiscard]] std::vector<SquirrelInstruction> ReadInstructions(std::size_t count) {
			RequireSignature(kPartSignature, "instruction section");
			RequireAvailableRecords(count, 8);
			std::vector<SquirrelInstruction> instructions;
			instructions.reserve(count);
			for (std::size_t index = 0; index < count; ++index) {
				SquirrelInstruction instruction;
				instruction.Argument1 = std::bit_cast<std::int32_t>(ReadWord());
				instruction.Opcode = ReadByte();
				instruction.Argument0 = ReadByte();
				instruction.Argument2 = ReadByte();
				instruction.Argument3 = ReadByte();
				if (instruction.Opcode > kMaxOpcode) {
					Fail("unsupported instruction opcode");
				}

				instructions.push_back(instruction);
			}

			return instructions;
		}

		[[nodiscard]] bool ReadFlag() {
			const std::uint8_t flag = ReadByte();
			if (flag > 1) {
				Fail("invalid boolean flag");
			}

			return flag == 1;
		}

		[[nodiscard]] SquirrelFunction ReadFunction(std::size_t depth) {
			if (depth > kMaxFunctionDepth || m_FunctionCount == kMaxFunctions) {
				Fail("function nesting/count exceeds the recovery limit");
			}

			++m_FunctionCount;
			RequireSignature(kPartSignature, "function");
			SquirrelFunction function;
			function.SourceName = ReadLiteral();
			function.Name = ReadLiteral();
			RequireSignature(kPartSignature, "function counts");
			std::array<std::size_t, 8> counts{};
			for (std::size_t index = 0; index < counts.size(); ++index) {
				counts[index] = ReadCount();
			}

			// Section ordering and widths are recovered from the native loader at VA 0x14024d4d0; see docs/recovery/runtime.md.
			function.Literals = ReadLiterals(counts[0]);
			function.Parameters = ReadLiterals(counts[1]);
			function.OuterBindings = ReadOuterBindings(counts[2]);
			function.LocalVariables = ReadLocalVariables(counts[3]);
			function.LineInformation = ReadLineInformation(counts[4]);
			function.DefaultParameters = ReadDefaultParameters(counts[5]);
			function.Instructions = ReadInstructions(counts[6]);
			RequireSignature(kPartSignature, "nested function section");
			RequireAvailableRecords(counts[7], 86);
			function.Functions.reserve(counts[7]);
			for (std::size_t index = 0; index < counts[7]; ++index) {
				function.Functions.push_back(ReadFunction(depth + 1));
			}

			function.StackSize = ReadWord();
			function.IsGenerator = ReadFlag();
			function.HasVariableParameters = ReadFlag();

			return function;
		}

		std::span<const std::uint8_t> m_Bytes;
		std::size_t m_Offset = 0;
		std::size_t m_RecordCount = 0;
		std::size_t m_FunctionCount = 0;
		std::size_t m_StringBytes = 0;
	};

	BytecodeProgram ReadSquirrelBytecode(std::span<const std::uint8_t> bytes) {
		BytecodeReader reader(bytes);

		return reader.Read();
	}
} // namespace sgre::script
