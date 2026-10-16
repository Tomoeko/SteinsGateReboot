#include "sgre/text/truetype_control_program.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <initializer_list>
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

static sgre::text::TrueTypeFontProgram Define(std::span<const std::uint8_t> bytes) {
	sgre::text::TrueTypeDefinitionLimits limits;
	limits.MaxFunctionDefinitions = 64;

	return sgre::text::TrueTypeFontProgram(bytes, limits);
}

static void AppendDefinition(std::vector<std::uint8_t>& bytes, std::uint8_t identifier, std::initializer_list<std::uint8_t> body) {
	bytes.insert(bytes.end(), {0xb0, identifier, 0x2c});
	bytes.insert(bytes.end(), body);
	bytes.push_back(0x2d);
}

static sgre::text::TrueTypeControlProgramResult Run(const sgre::text::TrueTypeFontProgram& font, std::span<const std::uint8_t> glyph, std::initializer_list<std::int32_t> initial = {}, bool strict = false) {
	sgre::text::TrueTypeProgramCode code;
	code.Glyph = glyph;

	return sgre::text::ExecuteTrueTypeControlProgram(font, code, sgre::text::TrueTypeProgramRange::Glyph, strict, std::span(initial.begin(), initial.size()));
}

static void RequireStack(const sgre::text::TrueTypeControlProgramResult& result, std::initializer_list<std::int32_t> expected) {
	Require(std::ranges::equal(result.Operands.Stack, expected), "TrueType control stack differs from independent operand order");
}

template <typename Operation>
static void RequireFailure(Operation operation, sgre::text::TrueTypeProgramFailure failure, std::uint32_t offset) {
	try {
		operation();
	} catch (const sgre::text::TrueTypeProgramError& error) {
		Require(error.Failure() == failure && error.Offset() == offset, "TrueType control failure lost original error or current code offset");

		return;
	}

	throw std::runtime_error("malformed TrueType control program was accepted");
}

template <typename Operation>
static void RequireBoundary(Operation operation, std::string_view expected) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(expected) != std::string_view::npos, "TrueType control admission lost its precise remaining boundary");

		return;
	}

	throw std::runtime_error("unrecovered TrueType control input was accepted");
}

static void TestActualDefinitionCallsAndSharedOperandStack() {
	const std::vector<std::uint8_t> bytes{0xb0, 9, 0x2c, 0xb0, 1, 0x60, 0x2d};
	const auto font = Define(bytes);
	const std::array<std::uint8_t, 3> glyph{0xb0, 9, 0x2b};
	const auto result = Run(font, glyph, {7});
	RequireStack(result, {8});
	Require(result.MaximumCallDepth == 1 && result.CallInstructions == 1 && result.Operands.ExecutedInstructions == 5, "CALL must execute actual original body through its ENDF and restore the outer code range");
	Require(result.FinalRange == sgre::text::TrueTypeProgramRange::Glyph && result.FinalOffset == glyph.size(), "CALL must return to the original next instruction in its caller range");
	Require(font.Program().data() == bytes.data() && font.Stack().empty() && font.ExecutedInstructions() == 2, "control execution must not copy, mutate or re-execute the FDEF declaration owner");
}

static void TestLoopCountOrderAndValidatedNonpositiveLoops() {
	const std::vector<std::uint8_t> bytes{0xb0, 9, 0x2c, 0xb0, 1, 0x60, 0x2d};
	const auto font = Define(bytes);
	const std::array<std::uint8_t, 6> glyph{0xb1, 3, 9, 0x2a, 0xb0, 42};
	const auto result = Run(font, glyph, {7});
	RequireStack(result, {10, 42});
	Require(result.MaximumCallDepth == 1 && result.CallInstructions == 1 && result.Operands.ExecutedInstructions == 12, "LOOPCALL must use lower count/upper id, reuse one actual return record and execute each real ENDF");
	const std::array<std::uint8_t, 1> loop{0x2a};
	for (const auto count : {0, -1}) {
		const auto empty = Run(font, loop, {7, count, 9});
		RequireStack(empty, {7});
		Require(empty.MaximumCallDepth == 0 && empty.CallInstructions == 1 && empty.Operands.ExecutedInstructions == 1, "nonpositive loop count must consume operands after validating the real function");
		RequireFailure([&]() {
			static_cast<void>(Run(font, loop, {count, 8}));
		},
		    sgre::text::TrueTypeProgramFailure::InvalidReference, 0);
	}
}

static void TestSameFontNestedDepthAndOriginalCapacity() {
	std::vector<std::uint8_t> bytes;
	for (std::uint8_t index = 0; index < 31; ++index) {
		AppendDefinition(bytes, index, {0xb0, static_cast<std::uint8_t>(index + 1), 0x2b});
	}
	AppendDefinition(bytes, 31, {0xb0, 77});
	const auto font = Define(bytes);
	const std::array<std::uint8_t, 3> glyph{0xb0, 0, 0x2b};
	const auto result = Run(font, glyph);
	RequireStack(result, {77});
	Require(result.MaximumCallDepth == 32 && result.CallInstructions == 32, "nested calls must retain all32 genuine original return frames and the shared operand stack");
	const std::vector<std::uint8_t> recursive_bytes{0xb0, 0, 0x2c, 0xb0, 0, 0x2b, 0x2d};
	const auto recursive = Define(recursive_bytes);
	RequireFailure([&]() {
		static_cast<void>(Run(recursive, glyph));
	},
	    sgre::text::TrueTypeProgramFailure::StackOverflow, 5);
}

static void TestPreparationAndGlyphRangeReturnOwnership() {
	sgre::text::TrueTypeControlProgramResult prepared;
	{
		const std::vector<std::uint8_t> bytes{0xb0, 3, 0x2c, 0xb0, 6, 0x60, 0x2d};
		const auto font = Define(bytes);
		const std::array<std::uint8_t, 3> preparation{0xb0, 3, 0x2b};
		const std::array<std::uint8_t, 3> glyph{0xb0, 2, 0x63};
		sgre::text::TrueTypeProgramCode code;
		code.Preparation = preparation;
		code.Glyph = glyph;
		const std::array<std::int32_t, 1> initial{58};
		prepared = sgre::text::ExecuteTrueTypeControlProgram(font, code, sgre::text::TrueTypeProgramRange::Preparation, false, initial);
		RequireStack(prepared, {64});
		Require(prepared.FinalRange == sgre::text::TrueTypeProgramRange::Preparation, "font-body ENDF must restore actual prep range2");
		const auto rendered = sgre::text::ExecuteTrueTypeControlProgram(font, code, sgre::text::TrueTypeProgramRange::Glyph, false, prepared.Operands.Stack);
		RequireStack(rendered, {2});
		RequireStack(prepared, {64});
	}

	RequireStack(prepared, {64});
	Require(prepared.MaximumCallDepth == 1, "owned control result must survive the original code and definition owners");
}

static void TestNestedConditionalLexicalSkipAndPushPayloads() {
	const auto font = Define({});
	const std::array<std::uint8_t, 10> yes{0xb0, 1, 0x58, 0xb0, 7, 0x1b, 0xb0, 9, 0x59, 0x59};
	const auto positive = Run(font, yes);
	RequireStack(positive, {7});
	Require(positive.Operands.ExecutedInstructions == 5 && positive.ScannedInstructions == 2, "executed ELSE skips its opposite branch without counting scanned opcodes as execution");
	auto no = yes;
	no[1] = 0;
	RequireStack(Run(font, no), {9});
	const std::array<std::uint8_t, 11> payload{0xb0, 0, 0x58, 0x40, 3, 0x1b, 0x58, 0x59, 0x59, 0xb0, 5};
	RequireStack(Run(font, payload), {5});
	const std::array<std::uint8_t, 17> nested{0xb0, 0, 0x58, 0xb0, 1, 0x58, 0xb0, 77, 0x1b, 0xb0, 88, 0x59, 0x1b, 0xb0, 33, 0x59, 0x59};
	RequireStack(Run(font, nested), {33});
	const std::array<std::uint8_t, 7> ignored_unrecovered{0xb0, 0, 0x58, 0x45, 0x59, 0xb0, 6};
	RequireStack(Run(font, ignored_unrecovered), {6});
	const std::array<std::uint8_t, 5> open_true{0xb0, 1, 0x58, 0xb0, 7};
	RequireStack(Run(font, open_true), {7});
}

static void TestRelativeBranchesPreserveNativeByteTargets() {
	const auto font = Define({});
	const std::array<std::uint8_t, 7> jumped{0xb0, 3, 0x1c, 0, 0, 0xb0, 7};
	RequireStack(Run(font, jumped), {7});
	const std::array<std::uint8_t, 8> conditional{0xb1, 3, 1, 0x78, 0, 0, 0xb0, 8};
	RequireStack(Run(font, conditional), {8});
	const std::array<std::uint8_t, 3> untaken{0x78, 0xb0, 9};
	RequireStack(Run(font, untaken, {999, 0}), {9});
	auto false_condition = untaken;
	false_condition[0] = 0x79;
	RequireStack(Run(font, false_condition, {999, 1}), {9});
	const std::array<std::uint8_t, 5> literal_target{0xb0, 2, 0x1c, 0xb0, 0x22};
	RequireStack(Run(font, literal_target, {77}), {});
	const std::array<std::uint8_t, 1> jump{0x1c};
	const auto zero_then_exit = Run(font, jump, {5, 0});
	RequireStack(zero_then_exit, {});
	Require(zero_then_exit.Operands.ExecutedInstructions == 2 && zero_then_exit.FinalOffset == 5, "native zero jump may retain preceding operands and main positive overshoot may end execution");
	RequireFailure([&]() {
		static_cast<void>(Run(font, jump, {0}));
	},
	    sgre::text::TrueTypeProgramFailure::InvalidArgument, 0);
	RequireFailure([&]() {
		static_cast<void>(Run(font, jump, {-1}));
	},
	    sgre::text::TrueTypeProgramFailure::InvalidArgument, 0xffffffffu);
}

static void TestCallErrorsStrictOperandsAndFunctionJumpFence() {
	const std::vector<std::uint8_t> bytes{0xb0, 0, 0x2c, 0xb0, 7, 0x2d};
	const auto font = Define(bytes);
	const std::array<std::uint8_t, 1> call{0x2b};
	RequireStack(Run(font, call), {7});
	RequireFailure([&]() {
		static_cast<void>(Run(font, call, {}, true));
	},
	    sgre::text::TrueTypeProgramFailure::StackUnderflow, 0);
	RequireFailure([&]() {
		static_cast<void>(Run(font, call, {-1}));
	},
	    sgre::text::TrueTypeProgramFailure::InvalidReference, 0);
	const std::array<std::uint8_t, 1> end{0x2d};
	RequireFailure([&]() {
		static_cast<void>(Run(font, end));
	},
	    sgre::text::TrueTypeProgramFailure::EndWithoutCall, 0);
	const std::vector<std::uint8_t> escaping_bytes{0xb0, 0, 0x2c, 0xb0, 250, 0x1c, 0x2d};
	const auto escaping = Define(escaping_bytes);
	RequireFailure([&]() {
		static_cast<void>(Run(escaping, call, {0}));
	},
	    sgre::text::TrueTypeProgramFailure::InvalidArgument, 255);
}

static void TestMalformedSkippedCodeAndRemainingStateGates() {
	const auto font = Define({});
	const std::array<std::uint8_t, 5> no_end{0xb0, 0, 0x58, 0xb0, 7};
	RequireFailure([&]() {
		static_cast<void>(Run(font, no_end));
	},
	    sgre::text::TrueTypeProgramFailure::CodeOverflow, 5);
	const std::array<std::uint8_t, 5> truncated{0xb0, 0, 0x58, 0xb8, 1};
	RequireFailure([&]() {
		static_cast<void>(Run(font, truncated));
	},
	    sgre::text::TrueTypeProgramFailure::CodeOverflow, 3);
	for (const auto opcode : {0x00, 0x42, 0x43, 0x44, 0x45, 0x48, 0x2c, 0x89}) {
		const std::array<std::uint8_t, 1> code{static_cast<std::uint8_t>(opcode)};
		RequireBoundary([&]() {
			static_cast<void>(Run(font, code));
		},
		    "requires recovered interpreter execution");
	}

	RequireFailure([&]() {
		static_cast<void>(sgre::text::ExecuteTrueTypeControlProgram(font, {}, static_cast<sgre::text::TrueTypeProgramRange>(4)));
	},
	    sgre::text::TrueTypeProgramFailure::InvalidArgument, 0);
	RequireFailure([&]() {
		static_cast<void>(sgre::text::ExecuteTrueTypeControlProgram(font, {}, sgre::text::TrueTypeProgramRange::Preparation));
	},
	    sgre::text::TrueTypeProgramFailure::MissingCodeRange, 0);
}

static void TestBoundedRepeatedLexicalWork() {
	const auto font = Define({});
	std::vector<std::uint8_t> code{0xb0, 0, 0x58};
	code.insert(code.end(), 5000, 0x22);
	code.insert(code.end(), {0x59, 0xb0, 1, 0x61, 0x20, 0xb8, 0, 0, 0x23});
	const auto branch_offset = code.size();
	const auto distance = static_cast<std::uint16_t>(0u - static_cast<std::uint32_t>(branch_offset));
	code[code.size() - 3] = static_cast<std::uint8_t>(distance >> 8);
	code[code.size() - 2] = static_cast<std::uint8_t>(distance & 0xff);
	code.push_back(0x78);
	RequireBoundary([&]() {
		static_cast<void>(Run(font, code, {1000}));
	},
	    "bounded lexical scan work");
}

int main() {
	try {
		TestActualDefinitionCallsAndSharedOperandStack();
		TestLoopCountOrderAndValidatedNonpositiveLoops();
		TestSameFontNestedDepthAndOriginalCapacity();
		TestPreparationAndGlyphRangeReturnOwnership();
		TestNestedConditionalLexicalSkipAndPushPayloads();
		TestRelativeBranchesPreserveNativeByteTargets();
		TestCallErrorsStrictOperandsAndFunctionJumpFence();
		TestMalformedSkippedCodeAndRemainingStateGates();
		TestBoundedRepeatedLexicalWork();
		std::cout << "9 TrueType control-program groups passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
