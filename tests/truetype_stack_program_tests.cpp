#include "sgre/text/truetype_stack_program.hpp"

#include "sgre/text/truetype_program.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <limits>
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

static void RequireStack(const sgre::text::TrueTypeStackProgramResult& result, std::initializer_list<std::int32_t> expected) {
	Require(std::ranges::equal(result.Stack, expected), "TrueType stack program differs from the independent operand vector");
}

static sgre::text::TrueTypeStackProgramResult Run(std::uint8_t opcode, std::initializer_list<std::int32_t> initial = {}, bool strict = false) {
	const std::array code{opcode};
	sgre::text::TrueTypeStackLimits limits;
	limits.StrictStackUnderflow = strict;

	return sgre::text::ExecuteTrueTypeStackProgram(code, limits, std::span(initial.begin(), initial.size()));
}

template <typename Operation>
static void RequireFailure(Operation operation, sgre::text::TrueTypeProgramFailure expected, std::uint32_t offset = 0) {
	try {
		operation();
	} catch (const sgre::text::TrueTypeProgramError& error) {
		Require(error.Failure() == expected && error.Offset() == offset, "TrueType primitive failure lost its native code or exact instruction offset");

		return;
	}

	throw std::runtime_error("TrueType malformed primitive program was accepted");
}

static void TestPushDecodeCompositionAndOwnedResult() {
	sgre::text::TrueTypeStackProgramResult result;
	{
		const std::vector<std::uint8_t> code{0x40, 3, 0, 128, 255, 0xb9, 0xff, 0xff, 0x80, 0, 0x41, 0};
		result = sgre::text::ExecuteTrueTypeStackProgram(code, {});
		Require(result.ExecutedInstructions == 3 && result.StackCapacity == 32, "stack program must share the actual PUSH decoder and native extra32 capacity");
	}

	RequireStack(result, {0, 128, 255, -1, -32768});
	const std::array<std::int32_t, 2> initial{3, 9};
	const auto empty = sgre::text::ExecuteTrueTypeStackProgram({}, {}, initial);
	Require(empty.ExecutedInstructions == 0 && std::ranges::equal(empty.Stack, initial), "empty stack program must own an independent copy of its initial operands");
}

static void TestStackOrderAndIndexedOperands() {
	RequireStack(Run(0x20, {9, 5}), {9, 5, 5});
	RequireStack(Run(0x21, {9, 5}), {9});
	RequireStack(Run(0x22, {9, 5}), {});
	RequireStack(Run(0x23, {9, 5}), {5, 9});
	RequireStack(Run(0x24, {9, 5}), {9, 5, 2});
	RequireStack(Run(0x25, {10, 20, 30, 1}), {10, 20, 30, 30});
	RequireStack(Run(0x25, {10, 20, 30, 3}), {10, 20, 30, 10});
	RequireStack(Run(0x26, {10, 20, 30, 1}), {10, 20, 30});
	RequireStack(Run(0x26, {10, 20, 30, 3}), {20, 30, 10});
	RequireStack(Run(0x8a, {7, 10, 20, 30}), {7, 20, 30, 10});
	for (const auto index : {0, -1, 4}) {
		RequireStack(Run(0x25, {10, 20, 30, index}), {10, 20, 30, 0});
		RequireStack(Run(0x26, {10, 20, 30, index}), {10, 20, 30});
		for (const auto opcode : {0x25, 0x26}) {
			RequireFailure([=]() {
				static_cast<void>(Run(static_cast<std::uint8_t>(opcode), {10, 20, 30, index}, true));
			},
			    sgre::text::TrueTypeProgramFailure::InvalidReference);
		}
	}
}

static void TestSignedComparisonAndBooleanWords() {
	const std::array<std::uint8_t, 6> comparisons{0x50, 0x51, 0x52, 0x53, 0x54, 0x55};
	const std::array<std::array<std::int32_t, 6>, 3> expected{{{1, 1, 0, 0, 0, 1}, {0, 1, 0, 1, 1, 0}, {0, 0, 1, 1, 0, 1}}};
	const std::array<std::array<std::int32_t, 2>, 3> inputs{{{-4, 7}, {-4, -4}, {7, -4}}};
	for (std::size_t row = 0; row < inputs.size(); ++row) {
		for (std::size_t column = 0; column < comparisons.size(); ++column) {
			RequireStack(Run(comparisons[column], {77, inputs[row][0], inputs[row][1]}), {77, expected[row][column]});
		}
	}

	RequireStack(Run(0x5a, {-2, 9}), {1});
	RequireStack(Run(0x5a, {-2, 0}), {0});
	RequireStack(Run(0x5b, {0, -9}), {1});
	RequireStack(Run(0x5b, {0, 0}), {0});
	RequireStack(Run(0x5c, {-9}), {0});
	RequireStack(Run(0x5c, {0}), {1});
}

static void TestNativeWordWrapAndSignedExtrema() {
	constexpr auto low = std::numeric_limits<std::int32_t>::min();
	constexpr auto high = std::numeric_limits<std::int32_t>::max();
	RequireStack(Run(0x60, {high, 1}), {low});
	RequireStack(Run(0x61, {low, 1}), {high});
	RequireStack(Run(0x65, {low}), {low});
	RequireStack(Run(0x64, {low}), {low});
	RequireStack(Run(0x64, {-31}), {31});
	RequireStack(Run(0x65, {31}), {-31});
	RequireStack(Run(0x8b, {low, high}), {high});
	RequireStack(Run(0x8c, {low, high}), {low});
	RequireStack(Run(0x8b, {-8, -2}), {-2});
	RequireStack(Run(0x8c, {-8, -2}), {-8});
}

static void TestFixedPointRoundingAndDivisionErrors() {
	RequireStack(Run(0x63, {32, 1}), {1});
	RequireStack(Run(0x63, {-32, 1}), {-1});
	RequireStack(Run(0x63, {31, 1}), {0});
	RequireStack(Run(0x63, {96, 96}), {144});
	RequireStack(Run(0x63, {std::numeric_limits<std::int32_t>::min(), 1}), {33554432});
	RequireStack(Run(0x62, {1, 3}), {21});
	RequireStack(Run(0x62, {-1, 3}), {-21});
	RequireStack(Run(0x62, {1, -3}), {-21});
	RequireStack(Run(0x62, {-1, -3}), {21});
	RequireStack(Run(0x62, {96, 64}), {96});
	RequireStack(Run(0x62, {std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::min()}), {0});
	RequireStack(Run(0x66, {65}), {64});
	RequireStack(Run(0x66, {-1}), {-64});
	RequireStack(Run(0x67, {65}), {128});
	RequireStack(Run(0x67, {-1}), {0});
	RequireStack(Run(0x67, {std::numeric_limits<std::int32_t>::max()}), {std::numeric_limits<std::int32_t>::min()});
	RequireFailure([]() {
		static_cast<void>(Run(0x62, {1, 0}));
	},
	    sgre::text::TrueTypeProgramFailure::DivideByZero);
}

static void TestNonPedanticUnderflowClearsTheEntireFixedGroup() {
	RequireStack(Run(0x20), {0, 0});
	RequireStack(Run(0x21), {});
	RequireStack(Run(0x23, {91}), {0, 0});
	RequireStack(Run(0x25), {0});
	RequireStack(Run(0x26), {});
	RequireStack(Run(0x50, {91}), {0});
	RequireStack(Run(0x51, {91}), {1});
	RequireStack(Run(0x54, {91}), {1});
	RequireStack(Run(0x60, {91}), {0});
	RequireStack(Run(0x61, {91}), {0});
	RequireStack(Run(0x63, {91}), {0});
	RequireStack(Run(0x8a, {91, 92}), {0, 0, 0});
	RequireFailure([]() {
		static_cast<void>(Run(0x62, {91}));
	},
	    sgre::text::TrueTypeProgramFailure::DivideByZero);
	for (const auto opcode : {0x20, 0x21, 0x23, 0x25, 0x26, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x5a, 0x5b, 0x5c, 0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x8a, 0x8b, 0x8c}) {
		RequireFailure([opcode]() {
			static_cast<void>(Run(static_cast<std::uint8_t>(opcode), {}, true));
		},
		    sgre::text::TrueTypeProgramFailure::StackUnderflow);
	}

	RequireStack(Run(0x22, {}, true), {});
	RequireStack(Run(0x24, {}, true), {0});
}

static void TestNativeStackCapacityBeforeAnyOutOfRangeWrite() {
	const std::vector<std::int32_t> full(32, 7);
	const std::array<std::uint8_t, 1> depth{0x24};
	RequireFailure([&]() {
		static_cast<void>(sgre::text::ExecuteTrueTypeStackProgram(depth, {}, full));
	},
	    sgre::text::TrueTypeProgramFailure::StackOverflow);
	const std::array<std::uint8_t, 1> duplicate{0x20};
	RequireFailure([&]() {
		static_cast<void>(sgre::text::ExecuteTrueTypeStackProgram(duplicate, {}, full));
	},
	    sgre::text::TrueTypeProgramFailure::StackOverflow);
	const std::vector<std::int32_t> oversized(33, 7);
	RequireFailure([&]() {
		static_cast<void>(sgre::text::ExecuteTrueTypeStackProgram({}, {}, oversized));
	},
	    sgre::text::TrueTypeProgramFailure::StackOverflow);
	Require(std::ranges::all_of(full, [](auto value) {
		return value == 7;
	}),
	    "failed programs must not mutate the borrowed input stack");
	sgre::text::TrueTypeStackLimits large;
	large.MaxStackElements = 65535;
	Require(sgre::text::ExecuteTrueTypeStackProgram({}, large).StackCapacity == 65567, "native extra32 capacity must be wider than the serialized uint16 limit");
}

static void TestPreciseUnrecoveredAndMalformedInstructionBoundaries() {
	const std::array<std::uint8_t, 27> admitted{0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x5a, 0x5b, 0x5c, 0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x8a, 0x8b, 0x8c};
	for (std::uint32_t opcode = 0; opcode < 256; ++opcode) {
		if (std::ranges::find(admitted, opcode) != admitted.end() || opcode == 0x40 || opcode == 0x41 || opcode == 0x2d || (opcode >= 0xb0 && opcode <= 0xbf)) {
			continue;
		}

		try {
			static_cast<void>(Run(static_cast<std::uint8_t>(opcode)));
		} catch (const std::runtime_error& error) {
			Require(std::string_view(error.what()).find("requires recovered interpreter execution") != std::string_view::npos, "unsupported opcode must retain its explicit recovery boundary");

			continue;
		}

		throw std::runtime_error("unrecovered interpreter opcode was accepted");
	}

	RequireFailure([]() {
		static_cast<void>(Run(0x2d));
	},
	    sgre::text::TrueTypeProgramFailure::EndWithoutCall);
	const std::array<std::uint8_t, 4> truncated{0xb0, 7, 0xb8, 1};
	RequireFailure([&]() {
		static_cast<void>(sgre::text::ExecuteTrueTypeStackProgram(truncated, {}));
	},
	    sgre::text::TrueTypeProgramFailure::CodeOverflow, 2);
}

static void TestOriginalInstructionWorkBoundAndBorrowedDefinitionBody() {
	const std::vector<std::uint8_t> million(1000000, 0x22);
	Require(sgre::text::ExecuteTrueTypeStackProgram(million, {}).ExecutedInstructions == 1000000, "actual million-instruction boundary must remain admitted");
	const std::vector<std::uint8_t> exceeded(1000001, 0x22);
	RequireFailure([&]() {
		static_cast<void>(sgre::text::ExecuteTrueTypeStackProgram(exceeded, {}));
	},
	    sgre::text::TrueTypeProgramFailure::InstructionLimit, 1000000);
	const std::array<std::uint8_t, 7> definitions{0xb0, 3, 0x2c, 0xb0, 5, 0x60, 0x2d};
	sgre::text::TrueTypeDefinitionLimits definition_limits;
	definition_limits.MaxFunctionDefinitions = 1;
	const sgre::text::TrueTypeFontProgram declarations(definitions, definition_limits);
	const auto* definition = declarations.FindFunction(3);
	Require(definition != nullptr, "accepted FDEF owner must supply the actual borrowed body extent");
	const auto body = declarations.Program().subspan(definition->StartOffset, definition->EndOffset - definition->StartOffset);
	const std::array<std::int32_t, 1> initial{7};
	RequireStack(sgre::text::ExecuteTrueTypeStackProgram(body, {}, initial), {12});
	Require(declarations.Stack().empty() && declarations.ExecutedInstructions() == 2, "executing a separately selected body must not mutate the declaration owner or pretend to execute CALL");
}

int main() {
	try {
		TestPushDecodeCompositionAndOwnedResult();
		TestStackOrderAndIndexedOperands();
		TestSignedComparisonAndBooleanWords();
		TestNativeWordWrapAndSignedExtrema();
		TestFixedPointRoundingAndDivisionErrors();
		TestNonPedanticUnderflowClearsTheEntireFixedGroup();
		TestNativeStackCapacityBeforeAnyOutOfRangeWrite();
		TestPreciseUnrecoveredAndMalformedInstructionBoundaries();
		TestOriginalInstructionWorkBoundAndBorrowedDefinitionBody();
		std::cout << "9 TrueType stack-program groups passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
