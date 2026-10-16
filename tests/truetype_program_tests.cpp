#include "sgre/text/truetype_program.hpp"

#include "fixtures/truetype_program_native.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Operation>
static void RequireFailure(Operation operation, sgre::text::TrueTypeProgramFailure failure) {
	try {
		operation();
	} catch (const sgre::text::TrueTypeProgramError& error) {
		Require(error.Failure() == failure, "TrueType malformed program reported a different native failure");

		return;
	}

	throw std::runtime_error("TrueType malformed program was accepted");
}

template <typename Operation>
static void RequireUnsupported(Operation operation, std::string_view message) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(message) != std::string_view::npos, "TrueType unsupported program lost its precise boundary");

		return;
	}

	throw std::runtime_error("TrueType unrecovered interpreter operation was accepted");
}

static sgre::text::TrueTypeDefinitionLimits Limits(std::uint16_t functions = 8, std::uint16_t stack = 0) {
	sgre::text::TrueTypeDefinitionLimits result;
	result.MaxFunctionDefinitions = functions;
	result.MaxStackElements = stack;

	return result;
}

static void TestAllNativeInstructionLengthsAndInlineData() {
	for (std::uint32_t opcode = 0; opcode < 256; ++opcode) {
		std::vector<std::uint8_t> bytes(19, 0xa5);
		bytes[2] = static_cast<std::uint8_t>(opcode);
		const auto native = sgre::text::fixture::kTrueTypeNativeInstructionLengths[opcode];
		if (native < 0) {
			bytes[3] = 3;
		}

		const auto instruction = sgre::text::DecodeTrueTypeInstruction(bytes, 2);
		const auto length = native < 0 ? std::uint32_t(2 - 3 * native) : static_cast<std::uint32_t>(native);
		Require(instruction.Offset == 2 && instruction.Opcode == opcode && instruction.ByteCount == length, "TrueType decoder differs from the independent original256-entry instruction length table");
		Require(instruction.IsPush() == (opcode == 0x40 || opcode == 0x41 || (opcode >= 0xb0 && opcode <= 0xbf)), "TrueType lexical decoder classified a different PUSH family");
		if (instruction.IsPush()) {
			Require(instruction.PushData.data() >= bytes.data() + 3 && instruction.PushData.data() + instruction.PushData.size() == bytes.data() + 2 + length, "TrueType PUSH data does not borrow its exact inline range");
		}
	}
}

static void TestPushSignedWordsUnsignedBytesAndEmptyCounts() {
	const std::array<std::uint8_t, 7> byte_values{0x40, 5, 0, 127, 128, 255, 45};
	const auto bytes = sgre::text::DecodeTrueTypeInstruction(byte_values, 0);
	Require(bytes.PushValue(2) == 128 && bytes.PushValue(3) == 255 && bytes.PushValue(4) == 45, "PUSH bytes must zero extend and retain apparent opcode values as data");
	const std::array<std::uint8_t, 10> word_values{0x41, 4, 0, 0, 0x7f, 0xff, 0x80, 0, 0xff, 0xff};
	const auto words = sgre::text::DecodeTrueTypeInstruction(word_values, 0);
	Require(words.PushValue(0) == 0 && words.PushValue(1) == 32767 && words.PushValue(2) == -32768 && words.PushValue(3) == -1, "PUSH words must sign extend actual big-endian16-bit operands");
	const std::array<std::uint8_t, 4> no_values{0x40, 0, 0x41, 0};
	const sgre::text::TrueTypeFontProgram empty_pushes(no_values, Limits());
	Require(empty_pushes.Stack().empty() && empty_pushes.ExecutedInstructions() == 2, "NPUSH count0 consumes only its opcode and count");
}

static void TestDefinitionSkipsPushDataAndUnexecutedBody() {
	const std::array<std::uint8_t, 10> bytes{0xb0, 5, 0x2c, 0x40, 3, 0x2c, 0x2d, 0x89, 0x7f, 0x2d};
	const sgre::text::TrueTypeFontProgram program(bytes, Limits());
	Require(program.Definitions().size() == 1 && program.ExecutedInstructions() == 2 && program.Stack().empty(), "FDEF must declare its body without executing its instructions");
	const auto& definition = program.Definitions().front();
	Require(definition.ProgramRange == 1 && definition.Identifier == 5 && definition.StartOffset == 3 && definition.EndOffset == 9 && definition.Active == 1 && definition.Auxiliary == 0, "FDEF must retain actual range/start/ENDF/id/active/auxiliary words");
	Require(program.Program().data() == bytes.data() && program.Program().size() == bytes.size(), "definition owner must borrow exact immutable fpgm bytes");
	Require(program.FindFunction(5) == &definition && program.FindFunction(0) == nullptr, "sparse function lookup must use actual stored identifiers");
}

static void TestStorageOrderRedefinitionAndDenseLookup() {
	const std::array<std::uint8_t, 14> bytes{0xb0, 9, 0x2c, 0x2d, 0xb0, 1, 0x2c, 0x2d, 0xb0, 9, 0x2c, 0xb0, 0x2d, 0x2d};
	const sgre::text::TrueTypeFontProgram program(bytes, Limits(2));
	Require(program.Definitions().size() == 2 && program.FunctionCapacity() == 2 && program.MaximumFunctionIdentifier() == 9 && program.ExecutedInstructions() == 6, "redefinition at full capacity must reuse its existing record");
	Require(program.Definitions()[0].Identifier == 9 && program.Definitions()[0].StartOffset == 11 && program.Definitions()[0].EndOffset == 13 && program.Definitions()[1].Identifier == 1, "redefinition must preserve storage order and replace exact body bounds");
	const std::array<std::uint8_t, 8> dense_bytes{0xb0, 0, 0x2c, 0x2d, 0xb0, 1, 0x2c, 0x2d};
	const sgre::text::TrueTypeFontProgram dense(dense_bytes, Limits());
	Require(dense.FindFunction(0) == &dense.Definitions()[0] && dense.FindFunction(1) == &dense.Definitions()[1] && dense.FindFunction(2) == nullptr, "native dense-index lookup must remain equivalent to exact identifier lookup");
	const std::array<std::uint8_t, 5> largest_literal{0xb8, 0x7f, 0xff, 0x2c, 0x2d};
	const sgre::text::TrueTypeFontProgram high(largest_literal, Limits());
	Require(high.MaximumFunctionIdentifier() == 32767 && high.FindFunction(32767) != nullptr && high.FindFunction(65535) == nullptr, "positive signed-word IDs must retain sparse lookup without ID-sized allocation");
}

static void TestMaxpLimitsNativeStackExtraAndRetainedOperands() {
	std::array<std::uint8_t, 32> maxp{};
	maxp[1] = 1;
	maxp[20] = 1;
	maxp[21] = 60;
	maxp[24] = 0x29;
	maxp[25] = 0x20;
	const auto limits = sgre::text::ReadTrueTypeDefinitionLimits(maxp);
	Require(limits.MaxFunctionDefinitions == 316 && limits.MaxStackElements == 10528 && !limits.StrictStackUnderflow, "maxp fields must decode actual big-endian definition/stack limits");
	const sgre::text::TrueTypeFontProgram empty({}, limits);
	Require(empty.StackCapacity() == 10560 && empty.FunctionCapacity() == 316 && empty.Definitions().empty() && empty.MaximumFunctionIdentifier() == 0, "fresh native context adds32 stack cells without creating definitions");
	const std::array<std::uint8_t, 5> preserved{0xb1, 7, 3, 0x2c, 0x2d};
	const sgre::text::TrueTypeFontProgram program(preserved, Limits());
	Require(program.Stack().size() == 1 && program.Stack().front() == 7 && program.Definitions().front().Identifier == 3, "FDEF must pop only its top operand while retaining earlier stack values");
	RequireUnsupported([&maxp]() {
		static_cast<void>(sgre::text::ReadTrueTypeDefinitionLimits(std::span(maxp).first(31)));
	},
	    "complete version1 maxp");
	maxp[1] = 0;
	RequireUnsupported([&maxp]() {
		static_cast<void>(sgre::text::ReadTrueTypeDefinitionLimits(maxp));
	},
	    "complete version1 maxp");
}

static void TestStackAndDefinitionFailures() {
	const std::array<std::uint8_t, 2> implicit_zero{0x2c, 0x2d};
	const sgre::text::TrueTypeFontProgram nonstrict(implicit_zero, Limits());
	Require(nonstrict.FindFunction(0) != nullptr && nonstrict.Stack().empty(), "native non-pedantic FDEF underflow must use its actual zero-filled operand");
	auto strict = Limits();
	strict.StrictStackUnderflow = true;
	RequireFailure([&implicit_zero, strict]() {
		static_cast<void>(sgre::text::TrueTypeFontProgram(implicit_zero, strict));
	},
	    sgre::text::TrueTypeProgramFailure::StackUnderflow);
	std::vector<std::uint8_t> too_many(35, 0);
	too_many[0] = 0x40;
	too_many[1] = 33;
	RequireFailure([&too_many]() {
		static_cast<void>(sgre::text::TrueTypeFontProgram(too_many, Limits()));
	},
	    sgre::text::TrueTypeProgramFailure::StackOverflow);
	RequireFailure([&implicit_zero]() {
		static_cast<void>(sgre::text::TrueTypeFontProgram(implicit_zero, Limits(0)));
	},
	    sgre::text::TrueTypeProgramFailure::InvalidDefinition);
	const std::array<std::uint8_t, 5> negative_id{0xb8, 0xff, 0xff, 0x2c, 0x2d};
	RequireFailure([&negative_id]() {
		static_cast<void>(sgre::text::TrueTypeFontProgram(negative_id, Limits()));
	},
	    sgre::text::TrueTypeProgramFailure::InvalidDefinition);
	const std::array<std::uint8_t, 1> top_end{0x2d};
	RequireFailure([&top_end]() {
		static_cast<void>(sgre::text::TrueTypeFontProgram(top_end, Limits()));
	},
	    sgre::text::TrueTypeProgramFailure::EndWithoutCall);
}

static void TestMalformedInlineAndDefinitionBounds() {
	for (const auto& bytes : std::vector<std::vector<std::uint8_t>>{{0x40}, {0x41, 2, 0, 0, 0}, {0xb8, 0}, {0xb7, 1, 2}}) {
		RequireFailure([&bytes]() {
			static_cast<void>(sgre::text::DecodeTrueTypeInstruction(bytes, 0));
		},
		    sgre::text::TrueTypeProgramFailure::CodeOverflow);
	}
	for (const auto& bytes : std::vector<std::vector<std::uint8_t>>{{0xb0, 1, 0x2c}, {0xb0, 1, 0x2c, 0x40, 2, 0x2d}, {0xb0, 1, 0x2c, 0xb8, 0x2d}}) {
		RequireFailure([&bytes]() {
			static_cast<void>(sgre::text::TrueTypeFontProgram(bytes, Limits()));
		},
		    sgre::text::TrueTypeProgramFailure::CodeOverflow);
	}
	for (const auto nested : {std::uint8_t(0x2c), std::uint8_t(0x89)}) {
		const std::array<std::uint8_t, 5> bytes{0xb0, 1, 0x2c, nested, 0x2d};
		RequireFailure([&bytes]() {
			static_cast<void>(sgre::text::TrueTypeFontProgram(bytes, Limits()));
		},
		    sgre::text::TrueTypeProgramFailure::NestedDefinition);
	}
	RequireFailure([]() {
		static_cast<void>(sgre::text::DecodeTrueTypeInstruction({}, 0));
	},
	    sgre::text::TrueTypeProgramFailure::CodeOverflow);
}

static void TestUnrecoveredExecutionAndPreparationBound() {
	for (const auto opcode : {std::uint8_t(0x00), std::uint8_t(0x2b), std::uint8_t(0x60), std::uint8_t(0x89)}) {
		const std::array<std::uint8_t, 1> bytes{opcode};
		RequireUnsupported([&bytes]() {
			static_cast<void>(sgre::text::TrueTypeFontProgram(bytes, Limits()));
		},
		    "requires recovered interpreter execution");
	}
	const std::vector<std::uint8_t> too_large(1024 * 1024 + 1);
	RequireUnsupported([&too_large]() {
		static_cast<void>(sgre::text::TrueTypeFontProgram(too_large, Limits()));
	},
	    "bounded preparation size");
}

int main() {
	try {
		TestAllNativeInstructionLengthsAndInlineData();
		TestPushSignedWordsUnsignedBytesAndEmptyCounts();
		TestDefinitionSkipsPushDataAndUnexecutedBody();
		TestStorageOrderRedefinitionAndDenseLookup();
		TestMaxpLimitsNativeStackExtraAndRetainedOperands();
		TestStackAndDefinitionFailures();
		TestMalformedInlineAndDefinitionBounds();
		TestUnrecoveredExecutionAndPreparationBound();
		std::cout << "TrueType program:8 groups passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
