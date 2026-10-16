#include "sgre/script/squirrel_bytecode.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

using ByteVector = std::vector<std::uint8_t>;

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view message) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).starts_with("Squirrel bytecode at byte "), "parse error lacks byte context");
		return;
	}

	throw std::runtime_error(std::string(message));
}

static void AppendWord(ByteVector& bytes, std::uint32_t value) {
	for (std::uint32_t index = 0; index < 4; ++index) {
		bytes.push_back(static_cast<std::uint8_t>(value >> (index * 8)));
	}
}

static void AppendString(ByteVector& bytes, std::string_view text) {
	AppendWord(bytes, 0x08000010);
	AppendWord(bytes, static_cast<std::uint32_t>(text.size()));
	bytes.insert(bytes.end(), text.begin(), text.end());
}

static void AppendPart(ByteVector& bytes) {
	AppendWord(bytes, 0x50415254);
}

static ByteVector MakeFunction(std::size_t nested_depth, bool rich) {
	ByteVector bytes;
	AppendPart(bytes);
	AppendString(bytes, "fixture.nut");
	AppendString(bytes, "main");
	AppendPart(bytes);
	AppendWord(bytes, rich ? 4 : 0);
	AppendWord(bytes, 1);
	AppendWord(bytes, rich ? 1 : 0);
	AppendWord(bytes, rich ? 1 : 0);
	AppendWord(bytes, rich ? 1 : 0);
	AppendWord(bytes, rich ? 1 : 0);
	AppendWord(bytes, 1);
	AppendWord(bytes, nested_depth == 0 ? 0 : 1);
	AppendPart(bytes);
	if (rich) {
		AppendWord(bytes, 0x01000001);
		AppendWord(bytes, 0x05000002);
		AppendWord(bytes, 0x80000000);
		AppendWord(bytes, 0x05000004);
		AppendWord(bytes, 0x3fc00000);
		AppendString(bytes, std::string_view("a\0b", 3));
	}

	AppendPart(bytes);
	AppendString(bytes, "this");
	AppendPart(bytes);
	if (rich) {
		AppendWord(bytes, 1);
		AppendWord(bytes, 0x05000002);
		AppendWord(bytes, 2);
		AppendString(bytes, "outer");
	}

	AppendPart(bytes);
	if (rich) {
		AppendString(bytes, "local");
		AppendWord(bytes, 3);
		AppendWord(bytes, 0);
		AppendWord(bytes, 1);
	}

	AppendPart(bytes);
	if (rich) {
		AppendWord(bytes, 17);
		AppendWord(bytes, 0);
	}

	AppendPart(bytes);
	if (rich) {
		AppendWord(bytes, 3);
	}

	AppendPart(bytes);
	AppendWord(bytes, 0xfffffffe);
	bytes.insert(bytes.end(), {0x11, 0x22, 0x33, 0x44});
	AppendPart(bytes);
	if (nested_depth != 0) {
		const auto nested = MakeFunction(nested_depth - 1, false);
		bytes.insert(bytes.end(), nested.begin(), nested.end());
	}

	AppendWord(bytes, 4);
	bytes.push_back(rich ? 1 : 0);
	bytes.push_back(rich ? 1 : 0);

	return bytes;
}

static ByteVector MakeProgram(std::size_t nested_depth = 1, bool rich = true) {
	ByteVector bytes = {0xfa, 0xfa, 0x52, 0x49, 0x51, 0x53, 1, 0, 0, 0};
	const auto function = MakeFunction(nested_depth, rich);
	bytes.insert(bytes.end(), function.begin(), function.end());
	AppendWord(bytes, 0x5441494c);

	return bytes;
}

static void TestOwnedStructure() {
	const auto bytes = MakeProgram();
	const auto program = sgre::script::ReadSquirrelBytecode(bytes);
	const auto& function = program.MainFunction;
	Require(program.SerializedSize == bytes.size(), "serialized extent differs");
	Require(std::get<std::string>(function.SourceName) == "fixture.nut" && std::get<std::string>(function.Name) == "main", "function names differ");
	Require(function.Literals.size() == 4 && std::holds_alternative<std::monostate>(function.Literals[0]), "null literal differs");
	Require(std::get<std::int32_t>(function.Literals[1]) == std::bit_cast<std::int32_t>(std::uint32_t{0x80000000}), "integer bits differ");
	Require(std::get<float>(function.Literals[2]) == 1.5f && std::get<std::string>(function.Literals[3]) == std::string("a\0b", 3), "float/string literals differ");
	Require(function.Parameters.size() == 1 && std::get<std::string>(function.Parameters[0]) == "this", "parameter differs");
	Require(function.OuterBindings.size() == 1 && function.OuterBindings[0].Type == 1 && std::get<std::int32_t>(function.OuterBindings[0].Source) == 2 && std::get<std::string>(function.OuterBindings[0].Name) == "outer", "outer binding differs");
	Require(function.LocalVariables.size() == 1 && function.LocalVariables[0].Position == 3 && function.LocalVariables[0].StartInstruction == 0 && function.LocalVariables[0].EndInstruction == 1, "local variable differs");
	Require(function.LineInformation.size() == 1 && function.LineInformation[0].Line == 17 && function.LineInformation[0].Instruction == 0, "line information differs");
	Require(function.DefaultParameters == std::vector<std::uint32_t>({3}), "default parameter differs");
	Require(function.Instructions.size() == 1 && function.Instructions[0].Argument1 == -2 && function.Instructions[0].Opcode == 0x11 && function.Instructions[0].Argument0 == 0x22 && function.Instructions[0].Argument2 == 0x33 && function.Instructions[0].Argument3 == 0x44, "instruction bytes differ");
	Require(function.Functions.size() == 1 && function.Functions[0].Functions.empty(), "nested prototype differs");
	Require(function.StackSize == 4 && function.IsGenerator && function.HasVariableParameters, "function flags differ");
}

static void TestTruncationAndSignatures() {
	const auto bytes = MakeProgram();
	for (std::size_t size = 0; size < bytes.size(); ++size) {
		RequireFailure([&]() {
			static_cast<void>(sgre::script::ReadSquirrelBytecode(std::span<const std::uint8_t>(bytes).first(size)));
		},
		    "truncated program accepted");
	}

	for (const std::size_t offset : {std::size_t{0}, std::size_t{2}, std::size_t{6}, std::size_t{10}, bytes.size() - 4}) {
		auto invalid = bytes;
		invalid[offset] ^= 1;
		RequireFailure([&]() {
			static_cast<void>(sgre::script::ReadSquirrelBytecode(invalid));
		},
		    "invalid signature/width accepted");
	}

	auto trailing = bytes;
	trailing.push_back(0);
	RequireFailure([&]() {
		static_cast<void>(sgre::script::ReadSquirrelBytecode(trailing));
	},
	    "trailing bytes accepted");
	auto invalid_flag = bytes;
	invalid_flag[invalid_flag.size() - 5] = 2;
	RequireFailure([&]() {
		static_cast<void>(sgre::script::ReadSquirrelBytecode(invalid_flag));
	},
	    "invalid flag accepted");
}

static void TestLimitsAndObjectTypes() {
	const auto bytes = MakeProgram(0, false);
	// Counts begin after the independently encoded header, two names, and section marker.
	const std::size_t count_offset = 10 + 4 + 8 + 11 + 8 + 4 + 4;
	auto negative_count = bytes;
	for (std::size_t index = 0; index < 4; ++index) {
		negative_count[count_offset + index] = 0xff;
	}

	RequireFailure([&]() {
		static_cast<void>(sgre::script::ReadSquirrelBytecode(negative_count));
	},
	    "negative/oversized count accepted");
	auto unavailable_count = bytes;
	unavailable_count[count_offset] = 0xff;
	RequireFailure([&]() {
		static_cast<void>(sgre::script::ReadSquirrelBytecode(unavailable_count));
	},
	    "count exceeding remaining bytes accepted");
	auto negative_string_length = bytes;
	for (std::size_t index = 0; index < 4; ++index) {
		negative_string_length[18 + index] = 0xff;
	}

	RequireFailure([&]() {
		static_cast<void>(sgre::script::ReadSquirrelBytecode(negative_string_length));
	},
	    "negative/oversized string accepted");
	auto unsupported_type = bytes;
	unsupported_type[14] = 0x20;
	RequireFailure([&]() {
		static_cast<void>(sgre::script::ReadSquirrelBytecode(unsupported_type));
	},
	    "unsupported object type accepted");
	auto unsupported_opcode = bytes;
	unsupported_opcode[unsupported_opcode.size() - 18] = 0xff;
	RequireFailure([&]() {
		static_cast<void>(sgre::script::ReadSquirrelBytecode(unsupported_opcode));
	},
	    "unsupported instruction opcode accepted");
	const auto deep = MakeProgram(65, false);
	RequireFailure([&]() {
		static_cast<void>(sgre::script::ReadSquirrelBytecode(deep));
	},
	    "function recursion limit ignored");
	const auto boundary = MakeProgram(64, false);
	static_cast<void>(sgre::script::ReadSquirrelBytecode(boundary));
}

int main() {
	try {
		TestOwnedStructure();
		TestTruncationAndSignatures();
		TestLimitsAndObjectTypes();
		std::cout << "Squirrel bytecode tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << "Squirrel bytecode tests failed: " << error.what() << '\n';

		return 1;
	}
}
