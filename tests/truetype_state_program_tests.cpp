#include "sgre/text/truetype_state_program.hpp"

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

static sgre::text::TrueTypeFontProgram Define(std::span<const std::uint8_t> program) {
	sgre::text::TrueTypeDefinitionLimits limits;
	limits.MaxFunctionDefinitions = 16;

	return sgre::text::TrueTypeFontProgram(program, limits);
}

static sgre::text::TrueTypeControlProgramResult Run(const sgre::text::TrueTypeFontProgram& font, sgre::text::TrueTypeInterpreterState& state, std::span<const std::uint8_t> program, std::initializer_list<std::int32_t> initial = {}, bool strict = false) {
	sgre::text::TrueTypeProgramCode code;
	code.Glyph = program;

	return sgre::text::ExecuteTrueTypeStateProgram(font, code, sgre::text::TrueTypeProgramRange::Glyph, state, strict, std::span(initial.begin(), initial.size()));
}

static void RequireStack(const sgre::text::TrueTypeControlProgramResult& result, std::initializer_list<std::int32_t> expected) {
	Require(std::ranges::equal(result.Operands.Stack, expected), "TrueType state result differs from independent word ordering");
}

template <typename Operation>
static void RequireFailure(Operation operation, sgre::text::TrueTypeProgramFailure expected, std::uint32_t offset = 0) {
	try {
		operation();
	} catch (const sgre::text::TrueTypeProgramError& error) {
		Require(error.Failure() == expected && error.Offset() == offset, "TrueType state instruction lost its native error or offset");

		return;
	}

	throw std::runtime_error("malformed TrueType state instruction was accepted");
}

template <typename Operation>
static void RequireBoundary(Operation operation, std::string_view expected) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(expected) != std::string_view::npos, "TrueType state failure lost its precise remaining boundary");

		return;
	}

	throw std::runtime_error("unrecovered TrueType state input was accepted");
}

static void TestNativeDefaultsAndOwnedCrossRangeState() {
	sgre::text::TrueTypeInterpreterState state;
	Require(state.Graphics.Loop == 1 && state.Graphics.MinimumDistance == 64 && state.Graphics.ControlValueCutIn == 68 && state.Graphics.AutoFlip, "fresh graphics state differs from the original68-byte initializer");
	Require(state.Graphics.DeltaBase == 9 && state.Graphics.DeltaShift == 3 && state.Graphics.ZonePointers == std::array<std::uint16_t, 3>{1, 1, 1}, "fresh delta or zone fields differ from original initialization");
	state.Storage.resize(2);
	state.ControlValues.resize(2);
	const std::array<std::uint8_t, 5> bytes{0xb0, 3, 0x2c, 0x42, 0x2d};
	const auto font = Define(bytes);
	const std::array<std::uint8_t, 5> preparation{0xb1, 1, 79, 0xb0, 3};
	std::vector<std::uint8_t> prepared(preparation.begin(), preparation.end());
	prepared.push_back(0x2b);
	prepared.insert(prepared.end(), {0xb1, 0, 91, 0x44});
	sgre::text::TrueTypeProgramCode code;
	code.Preparation = prepared;
	const auto result = sgre::text::ExecuteTrueTypeStateProgram(font, code, sgre::text::TrueTypeProgramRange::Preparation, state);
	Require(result.Operands.Stack.empty() && result.MaximumCallDepth == 1 && state.Storage[1] == 79 && state.ControlValues[0] == 91, "preparation CALL must mutate actual persistent storage/CVT owners");
	prepared.clear();
	prepared.shrink_to_fit();
	const std::array<std::uint8_t, 6> glyph{0xb0, 1, 0x43, 0xb0, 0, 0x45};
	RequireStack(Run(font, state, glyph), {79, 91});
	Require(font.Definitions().size() == 1 && font.Stack().empty(), "state execution must preserve the borrowed FDEF owner");
}

static void TestStorageAndControlValueIndexRules() {
	const std::array<std::uint8_t, 0> bytes{};
	const auto font = Define(bytes);
	sgre::text::TrueTypeInterpreterState state;
	state.Storage = {13};
	state.ControlValues = {29};
	for (const auto opcode : {0x42, 0x44, 0x70}) {
		const std::array program{static_cast<std::uint8_t>(opcode)};
		RequireStack(Run(font, state, program, {-1, 77}), {});
		Require(state.Storage[0] == 13 && state.ControlValues[0] == 29, "non-pedantic invalid write must not alter any actual word");
		RequireFailure([&]() {
			static_cast<void>(Run(font, state, program, {1, 77}, true));
		},
		    sgre::text::TrueTypeProgramFailure::InvalidReference);
	}
	for (const auto opcode : {0x43, 0x45}) {
		const std::array program{static_cast<std::uint8_t>(opcode)};
		RequireStack(Run(font, state, program, {-1}), {0});
		RequireFailure([&]() {
			static_cast<void>(Run(font, state, program, {1}, true));
		},
		    sgre::text::TrueTypeProgramFailure::InvalidReference);
	}

	const std::array<std::uint8_t, 1> write{0x42};
	RequireStack(Run(font, state, write, {99}), {});
	Require(state.Storage[0] == 0, "partial fixed-group underflow must zero index and value before actual storage write");
	RequireFailure([&]() {
		static_cast<void>(Run(font, state, write, {99}, true));
	},
	    sgre::text::TrueTypeProgramFailure::StackUnderflow);
}

static void TestScaledCvtSignedHalfTiesAndWordOverflow() {
	const std::array<std::uint8_t, 0> bytes{};
	const auto font = Define(bytes);
	sgre::text::TrueTypeInterpreterState state;
	state.ControlValues.resize(1);
	state.Metrics.Scale16Dot16 = 0x8000;
	const std::array<std::uint8_t, 1> write{0x70};
	for (const auto value : {-3, -1, 0, 1, 3}) {
		static_cast<void>(Run(font, state, write, {0, value}));
		const auto expected = value < 0 ? (value - 1) / 2 : (value + 1) / 2;
		Require(state.ControlValues[0] == expected, "native MulFix must round positive and negative half ties away from zero");
	}
	state.Metrics.Scale16Dot16 = 0x20000;
	static_cast<void>(Run(font, state, write, {0, std::numeric_limits<std::int32_t>::max()}));
	Require(state.ControlValues[0] == -2, "scaled CVT result must preserve low32 return bits");
	const std::array<std::uint8_t, 1> width{0x1f};
	static_cast<void>(Run(font, state, width, {-63}));
	Require(state.Graphics.SingleWidth == -126, "SSW must consume the same original fixed scale as WCVTF");
}

static void TestAxisReferencesAndZoneSelection() {
	const std::array<std::uint8_t, 0> bytes{};
	const auto font = Define(bytes);
	sgre::text::TrueTypeInterpreterState state;
	const std::array<std::uint8_t, 5> axes{0x00, 0x05, 0x0c, 0x0d, 0x0e};
	RequireStack(Run(font, state, axes), {0, 0x4000, 0x4000, 0});
	Require(state.Graphics.Freedom.X == 0 && state.Graphics.Freedom.Y == 0x4000 && state.Graphics.DualProjection.Y == 0x4000, "axis setters must distinguish projection/dual and freedom before SFVTPV");
	const std::array<std::uint8_t, 3> references{0x10, 0x11, 0x12};
	static_cast<void>(Run(font, state, references, {0x10003, -1, 0x10002}));
	Require(state.Graphics.ReferencePoints == std::array<std::uint16_t, 3>{2, 65535, 3}, "reference setters must consume top-first and retain native16-bit values");
	const std::array<std::uint8_t, 7> zones{0xb0, 0, 0x16, 0xb0, 1, 0x14, 0x13};
	static_cast<void>(Run(font, state, zones, {7}));
	Require(state.Graphics.ZonePointers == std::array<std::uint16_t, 3>{0, 1, 0}, "invalid non-pedantic zone must preserve the prior copied zone index");
	const std::array<std::uint8_t, 1> zone{0x15};
	RequireFailure([&]() {
		static_cast<void>(Run(font, state, zone, {-1}, true));
	},
	    sgre::text::TrueTypeProgramFailure::InvalidReference);
}

static void TestScalarModesAndPreciseErrors() {
	const std::array<std::uint8_t, 0> bytes{};
	const auto font = Define(bytes);
	sgre::text::TrueTypeInterpreterState state;
	const std::array<std::uint8_t, 1> loop{0x17};
	static_cast<void>(Run(font, state, loop, {0}));
	Require(state.Graphics.Loop == 0, "SLOOP zero is valid original state");
	RequireFailure([&]() {
		static_cast<void>(Run(font, state, loop, {-1}));
	},
	    sgre::text::TrueTypeProgramFailure::InvalidArgument);
	const std::array<std::uint8_t, 1> shift{0x5f};
	static_cast<void>(Run(font, state, shift, {6}));
	Require(state.Graphics.DeltaShift == 6, "SDS must admit the original upper bound");
	for (const auto invalid : {-1, 7}) {
		RequireFailure([&]() {
			static_cast<void>(Run(font, state, shift, {invalid}));
		},
		    sgre::text::TrueTypeProgramFailure::InvalidArgument);
	}
	const std::array<std::uint8_t, 6> setters{0x1a, 0x1d, 0x1e, 0x5e, 0x4e, 0x8d};
	static_cast<void>(Run(font, state, setters, {7, -1, 0x10009, 23, 81, 19}));
	Require(state.Graphics.MinimumDistance == 19 && state.Graphics.ControlValueCutIn == 81 && state.Graphics.SingleWidthCutIn == 23 && state.Graphics.DeltaBase == 9 && !state.Graphics.AutoFlip && state.Graphics.ScanType == 0, "scalar setters must preserve independent fields and ignore negative scan type");
}

static void TestSignedRoundingAndCompensationFamilies() {
	const std::array<std::uint8_t, 0> bytes{};
	const auto font = Define(bytes);
	sgre::text::TrueTypeInterpreterState state;
	const std::array<std::uint8_t, 7> selectors{0x19, 0x18, 0x3d, 0x7d, 0x7c, 0x7a, 0x18};
	const std::array<std::int32_t, 7> positive{32, 64, 32, 0, 64, 33, 64};
	const std::array<std::int32_t, 7> negative{-32, -64, -32, 0, -64, -33, -64};
	for (std::size_t index = 0; index < selectors.size(); ++index) {
		const std::array program{selectors[index], std::uint8_t(0x68)};
		RequireStack(Run(font, state, program, {33}), {positive[index]});
		RequireStack(Run(font, state, program, {-33}), {negative[index]});
	}
	state.Metrics.Compensation = {1, 2, 3, 4};
	for (std::uint8_t color = 0; color < 4; ++color) {
		const std::array program{std::uint8_t(0x7a), static_cast<std::uint8_t>(0x68 + color), static_cast<std::uint8_t>(0x6c + color)};
		RequireStack(Run(font, state, program, {10}), {10 + 2 * (color + 1)});
		RequireStack(Run(font, state, program, {-10}), {-10 - 2 * (color + 1)});
	}
	state.Metrics.Compensation[0] = -100;
	const std::array<std::uint8_t, 2> off{0x7a, 0x6c};
	RequireStack(Run(font, state, off, {10}), {0});
	RequireStack(Run(font, state, off, {-10}), {0});
}

static void TestSuperRoundParametersAndPersistedModes() {
	const std::array<std::uint8_t, 0> bytes{};
	const auto font = Define(bytes);
	sgre::text::TrueTypeInterpreterState state;
	const std::array<std::uint8_t, 4> normal{0xb0, 0x48, 0x76, 0x68};
	RequireStack(Run(font, state, normal, {33}), {64});
	Require(state.Graphics.RoundPeriod == 64 && state.Graphics.RoundPhase == 0 && state.Graphics.RoundThreshold == 32, "SROUND selector must derive native8.8 parameters before integer truncation");
	const std::array<std::uint8_t, 4> diagonal{0xb0, 0x48, 0x77, 0x68};
	RequireStack(Run(font, state, diagonal, {33}), {45});
	Require(state.Graphics.RoundPeriod == 45 && state.Graphics.RoundThreshold == 22 && state.Graphics.Round == sgre::text::TrueTypeRoundMode::Super45, "S45ROUND must retain exact0x2d41 native base and signed remainder kernel");
	const std::array<std::uint8_t, 1> round{0x68};
	RequireStack(Run(font, state, round, {-33}), {-45});
	const std::array<std::uint8_t, 3> selector{0xb0, 0x00, 0x77};
	static_cast<void>(Run(font, state, selector));
	Require(state.Graphics.RoundPeriod == 22 && state.Graphics.RoundThreshold == 22, "zero threshold nibble must use pre-truncated period minus one");
}

static void TestInformationScanAndInstructionControl() {
	const std::array<std::uint8_t, 0> bytes{};
	const auto font = Define(bytes);
	sgre::text::TrueTypeInterpreterState state;
	state.Metrics.CurrentPpem = 33;
	state.Metrics.PointSize = 2112;
	state.Metrics.Rotated = true;
	state.Metrics.Stretched = true;
	state.Metrics.HasVariations = true;
	state.Metrics.Grayscale = true;
	state.Metrics.Version40Flag372 = true;
	state.Metrics.Version40Flag376 = true;
	const std::array<std::uint8_t, 2> metrics{0x4b, 0x4c};
	RequireStack(Run(font, state, metrics), {33, 2112});
	const std::array<std::uint8_t, 1> information{0x88};
	RequireStack(Run(font, state, information, {0x1d6f}), {40 | 0x100 | 0x200 | 0x400 | 0x1000 | 0x2000 | 0x8000 | 0x20000 | 0x40000 | 0x80000});
	const std::array<std::uint8_t, 1> scan{0x85};
	static_cast<void>(Run(font, state, scan, {255}));
	Require(state.Graphics.ScanControl, "SCANCTRL threshold255 must force on before other conditions");
	static_cast<void>(Run(font, state, scan, {0x1000 | 33}));
	Require(!state.Graphics.ScanControl, "SCANCTRL disable conditions must take precedence after enables");
	const std::array<std::uint8_t, 1> control{0x8e};
	static_cast<void>(Run(font, state, control, {4, 3}));
	Require(state.Graphics.InstructionControl == 4 && !state.BackwardCompatibility, "actual version40 INSTCTRL selector3 must change backward compatibility");
	static_cast<void>(Run(font, state, control, {0, 3}));
	Require(state.Graphics.InstructionControl == 0 && state.BackwardCompatibility, "INSTCTRL clearing selector3 must restore native compatibility policy");
	RequireFailure([&]() {
		static_cast<void>(Run(font, state, control, {1, 3}, true));
	},
	    sgre::text::TrueTypeProgramFailure::InvalidReference);
}

static void TestRemainingBoundariesAndOldApiGates() {
	const std::array<std::uint8_t, 0> bytes{};
	const auto font = Define(bytes);
	sgre::text::TrueTypeInterpreterState state;
	state.Storage.resize(1);
	state.ControlValues.resize(1);
	state.Metrics.XPpem = 22;
	state.Metrics.YPpem = 33;
	for (const auto opcode : {0x44, 0x45, 0x4b}) {
		const std::array program{static_cast<std::uint8_t>(opcode)};
		RequireBoundary([&]() {
			if (opcode == 0x44) {
				static_cast<void>(Run(font, state, program, {0, 42}));
			} else {
				static_cast<void>(Run(font, state, program, {0}));
			}
		},
		    "anisotropic");
	}
	for (const auto opcode : {0x06, 0x2e, 0x30, 0x38, 0x48, 0x5d, 0x89}) {
		const std::array program{static_cast<std::uint8_t>(opcode)};
		RequireBoundary([&]() {
			static_cast<void>(Run(font, state, program));
		},
		    "requires recovered interpreter");
	}
	const std::array<std::uint8_t, 1> read{0x43};
	sgre::text::TrueTypeProgramCode code;
	code.Glyph = read;
	RequireBoundary([&]() {
		static_cast<void>(sgre::text::ExecuteTrueTypeControlProgram(font, code, sgre::text::TrueTypeProgramRange::Glyph));
	},
	    "requires recovered interpreter");
	RequireBoundary([&]() {
		static_cast<void>(sgre::text::ExecuteTrueTypeStackProgram(read, {}));
	},
	    "requires recovered interpreter");
	state.Storage.resize(65536);
	RequireBoundary([&]() {
		static_cast<void>(Run(font, state, read));
	},
	    "bounded storage");
}

int main() {
	try {
		TestNativeDefaultsAndOwnedCrossRangeState();
		TestStorageAndControlValueIndexRules();
		TestScaledCvtSignedHalfTiesAndWordOverflow();
		TestAxisReferencesAndZoneSelection();
		TestScalarModesAndPreciseErrors();
		TestSignedRoundingAndCompensationFamilies();
		TestSuperRoundParametersAndPersistedModes();
		TestInformationScanAndInstructionControl();
		TestRemainingBoundariesAndOldApiGates();
		std::cout << "TrueType state program tests passed (9 groups)\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
