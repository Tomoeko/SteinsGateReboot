#include "sgre/text/truetype_point_program.hpp"

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

static sgre::text::TrueTypeHintZone Rectangle() {
	sgre::text::TrueTypeHintZone zone;
	zone.Original = {{0, 0}, {64, 0}, {64, 64}, {0, 64}, {-32, 0}, {128, 0}, {0, 96}, {0, -32}};
	zone.Current = zone.Original;
	zone.Current[0] = {16, 24};
	zone.Current[2] = {80, 96};
	zone.Unscaled = zone.Original;
	for (auto& point : zone.Unscaled) {
		point.X *= 2;
		point.Y *= 2;
	}
	zone.Tags.assign(zone.Current.size(), 1);
	zone.ContourEnds = {3};

	return zone;
}

struct Fixture {
	std::array<std::uint8_t, 0> FontBytes{};
	sgre::text::TrueTypeFontProgram Font{FontBytes, sgre::text::TrueTypeDefinitionLimits{}};
	sgre::text::TrueTypeInterpreterState State;
	sgre::text::TrueTypePointState Points;

	Fixture() {
		State.Metrics.InterpreterVersion = 35;
		State.ControlValues = {37, -41};
		Points.Zones = {Rectangle(), Rectangle()};
		Points.Zones[0].ContourEnds.clear();
		Points.OriginalXScale16Dot16 = 0x8000;
		Points.OriginalYScale16Dot16 = 0x8000;
	}

	sgre::text::TrueTypeControlProgramResult Run(std::span<const std::uint8_t> program, std::initializer_list<std::int32_t> initial = {}, bool strict = false) {
		sgre::text::TrueTypeProgramCode code;
		code.Glyph = program;

		return sgre::text::ExecuteTrueTypePointProgram(Font, code, sgre::text::TrueTypeProgramRange::Glyph, State, Points, strict, std::span(initial.begin(), initial.size()));
	}
};

template <typename Operation>
static void RequireFailure(Operation operation, sgre::text::TrueTypeProgramFailure expected) {
	try {
		operation();
	} catch (const sgre::text::TrueTypeProgramError& error) {
		Require(error.Failure() == expected, "TrueType point failure lost its native error identity");

		return;
	}

	throw std::runtime_error("invalid TrueType point reference was accepted");
}

template <typename Operation>
static void RequireBoundary(Operation operation, std::string_view expected) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(expected) != std::string_view::npos, "TrueType point gate lost its precise unsupported domain");

		return;
	}

	throw std::runtime_error("unrecovered TrueType point domain was accepted");
}

static void TestMdapRoundingReferencesAndWordIndices() {
	Fixture fixture;
	const std::array<std::uint8_t, 1> touch{0x2e};
	static_cast<void>(fixture.Run(touch, {0}));
	Require(fixture.Points.Zones[1].Current[0].X == 16 && fixture.Points.Zones[1].Tags[0] == 9, "MDAP without rounding still touches its selected point");
	const std::array<std::uint8_t, 1> rounded{0x2f};
	static_cast<void>(fixture.Run(rounded, {65536}));
	Require(fixture.Points.Zones[1].Current[0].X == 0 && fixture.State.Graphics.ReferencePoints[0] == 0 && fixture.State.Graphics.ReferencePoints[1] == 0, "MDAP must use the actual low16 point and update both reference fields");
	static_cast<void>(fixture.Run(rounded, {2}));
	Require(fixture.Points.Zones[1].Current[2].X == 64 && fixture.State.Graphics.ReferencePoints[0] == 2 && fixture.State.Graphics.ReferencePoints[1] == 2, "MDAP rounded movement must use the shared original rounding state");
}

static void TestTwilightMiapScfsAndSelectedAliases() {
	Fixture fixture;
	const std::array<std::uint8_t, 4> preparation{0xb0, 0, 0x13, 0x3e};
	static_cast<void>(fixture.Run(preparation, {0, 0}));
	Require(fixture.Points.Zones[0].Original[0] == sgre::text::TrueTypeHintPoint{37, 0} && fixture.Points.Zones[0].Current[0] == fixture.Points.Zones[0].Original[0], "twilight MIAP must seed both actual coordinate owners before movement");
	Require(fixture.Points.Zones[1].Current[0].X == 16, "selecting twilight must preserve the separate glyph owner");
	const std::array<std::uint8_t, 4> scfs{0xb0, 0, 0x15, 0x48};
	static_cast<void>(fixture.Run(scfs, {0, 73}));
	Require(fixture.Points.Zones[0].Current[0].X == 73 && fixture.Points.Zones[0].Original[0].X == 73, "SCFS through another selected alias must update the same twilight backing");
	Require(fixture.Points.Zones[0].Unscaled[0].X == 0, "twilight mutation must not manufacture font-space coordinates");
}

static void TestCoordinateAndDistanceDomains() {
	Fixture fixture;
	const std::array<std::uint8_t, 1> current{0x46};
	const std::array<std::uint8_t, 1> original{0x47};
	Require(fixture.Run(current, {0}).Operands.Stack == std::vector<std::int32_t>{16} && fixture.Run(original, {0}).Operands.Stack == std::vector<std::int32_t>{0}, "GC variants must distinguish actual current and original point coordinates");
	Require(fixture.Run(current, {65536}).Operands.Stack == std::vector<std::int32_t>{0}, "GC uses a full32 index rather than truncating to the reference word");
	const std::array<std::uint8_t, 1> distance{0x49};
	const std::array<std::uint8_t, 1> design_distance{0x4a};
	Require(fixture.Run(distance, {0, 2}).Operands.Stack == std::vector<std::int32_t>{-64}, "current MD must project the ordered difference between actual selected zones");
	fixture.Points.Zones[1].Unscaled[2].X = 256;
	Require(fixture.Run(design_distance, {0, 2}).Operands.Stack == std::vector<std::int32_t>{-128}, "original MD in glyph zones must use unscaled design positions and the actual design scale");
	fixture.State.Graphics.ZonePointers[0] = 0;
	Require(fixture.Run(design_distance, {0, 2}).Operands.Stack == std::vector<std::int32_t>{-64}, "twilight MD must instead use scaled original coordinates");
}

static void TestContourShiftPhysicalReferenceExclusion() {
	Fixture same;
	const std::array<std::uint8_t, 1> shift{0x35};
	static_cast<void>(same.Run(shift, {0}));
	Require(same.Points.Zones[1].Current[0].X == 16 && same.Points.Zones[1].Current[1].X == 80 && same.Points.Zones[1].Current[2].X == 96, "SHC must exclude only the actual reference in the same physical selected backing");
	Require(same.Points.Zones[1].Current[4].X == -32 && same.Points.Zones[1].Tags[4] == 1, "contour shift must preserve phantom points outside its actual contour");
	Fixture distinct;
	distinct.State.Graphics.ZonePointers[0] = 0;
	static_cast<void>(distinct.Run(shift, {0}));
	Require(distinct.Points.Zones[1].Current[0].X == 32 && distinct.Points.Zones[0].Current[0].X == 16, "equal numeric indices in distinct actual zones must not trigger reference exclusion");
}

static void TestVariablePointGroupsAndErrorState() {
	Fixture shifted;
	shifted.State.Graphics.Loop = 2;
	const std::array<std::uint8_t, 1> shift{0x38};
	static_cast<void>(shifted.Run(shift, {1, 2, 32}));
	Require(shifted.Points.Zones[1].Current[1].X == 96 && shifted.Points.Zones[1].Current[2].X == 112 && shifted.State.Graphics.Loop == 1, "SHPIX must pop point indices top-first and reset the completed loop");
	Fixture aligned;
	aligned.State.Graphics.Loop = 2;
	const std::array<std::uint8_t, 1> align{0x3c};
	static_cast<void>(aligned.Run(align, {1, 2}));
	Require(aligned.Points.Zones[1].Current[1].X == 16 && aligned.Points.Zones[1].Current[2].X == 16, "ALIGNRP must move every selected target to the actual reference projection");
	aligned.State.Graphics.Loop = 2;
	RequireFailure([&]() {
		static_cast<void>(aligned.Run(align, {}, true));
	},
	    sgre::text::TrueTypeProgramFailure::InvalidReference);
	Require(aligned.State.Graphics.Loop == 1, "variable point group underflow must preserve the original reset-to-one error path");
	aligned.State.Graphics.Loop = 2;
	RequireFailure([&]() {
		static_cast<void>(aligned.Run(align, {1, 999}, true));
	},
	    sgre::text::TrueTypeProgramFailure::InvalidReference);
	Require(aligned.State.Graphics.Loop == 2, "strict invalid target inside a live loop must preserve its remaining count");
	RequireFailure([&]() {
		static_cast<void>(aligned.Run(shift, {}, true));
	},
	    sgre::text::TrueTypeProgramFailure::StackUnderflow);
}

static void TestIupIndependentCoordinateSpacesAndSingleTouch() {
	Fixture fixture;
	auto& zone = fixture.Points.Zones[1];
	zone.Original = {{0, 0}, {25, 0}, {50, 0}, {75, 0}, {100, 0}};
	zone.Unscaled = {{0, 0}, {50, 0}, {100, 0}, {150, 0}, {200, 0}};
	zone.Current = zone.Original;
	zone.Current[0].X = 16;
	zone.Current[4].X = 132;
	zone.Tags = {9, 1, 1, 1, 9};
	zone.ContourEnds = {4};
	fixture.State.Graphics.ZonePointers[2] = 0;
	const std::array<std::uint8_t, 1> interpolate{0x31};
	static_cast<void>(fixture.Run(interpolate));
	Require(zone.Current[1].X == 45 && zone.Current[2].X == 74 && zone.Current[3].X == 103, "IUP must interpolate actual font-space distances between current endpoints");
	Require(zone.Tags == std::vector<std::uint8_t>({9, 1, 1, 1, 9}) && fixture.Points.Zones[0].Current[0].X == 16, "IUP must use the glyph source zone and leave all touch tags unchanged");
	zone.Tags = {9, 1, 1, 1, 1};
	zone.Current[1].X = 99;
	static_cast<void>(fixture.Run(interpolate));
	Require(zone.Current[1].X == 115, "single-touch IUP adds displacement to existing Current coordinates rather than replacing them from Original");
}

static void TestVersion40PoliciesAndPerCallIupFlags() {
	Fixture compatibility;
	compatibility.State.Metrics.InterpreterVersion = 40;
	const std::array<std::uint8_t, 1> rounded{0x2f};
	static_cast<void>(compatibility.Run(rounded, {0}));
	Require(compatibility.Points.Zones[1].Current[0].X == 16 && compatibility.Points.Zones[1].Tags[0] == 9, "version40 backward mode suppresses X movement but still publishes the touch bit");
	compatibility.State.Graphics.InstructionControl = 4;
	static_cast<void>(compatibility.Run(rounded, {0}));
	Require(compatibility.Points.Zones[1].Current[0].X == 0, "actual INSTCTRL4 must disable backward movement suppression");
	Fixture y;
	y.State.Metrics.InterpreterVersion = 40;
	const std::array<std::uint8_t, 4> both{0x31, 0x30, 0x00, 0x2f};
	static_cast<void>(y.Run(both, {0}));
	Require(y.Points.IupX && y.Points.IupY && y.Points.Zones[1].Current[0].Y == 24 && (y.Points.Zones[1].Tags[0] & 16) != 0, "both completed IUP axes suppress later compatibility Y movement while retaining touch state");
	const std::array<std::uint8_t, 2> next{0x00, 0x2f};
	static_cast<void>(y.Run(next, {0}));
	Require(!y.Points.IupX && !y.Points.IupY && y.Points.Zones[1].Current[0].Y == 0, "whole interpreter entry must reset IUP completion flags before genuine next execution");
}

static void TestPreciseInvalidReferencesAndOwnerBoundaries() {
	Fixture fixture;
	const std::array<std::uint8_t, 1> miap{0x3e};
	RequireFailure([&]() {
		static_cast<void>(fixture.Run(miap, {-1, 0}, true));
	},
	    sgre::text::TrueTypeProgramFailure::InvalidReference);
	Require(fixture.State.Graphics.ReferencePoints[0] == 65535 && fixture.State.Graphics.ReferencePoints[1] == 65535, "MIAP must publish its low16 references even on the native invalid-reference exit");
	const std::array<std::uint8_t, 1> coordinate{0x46};
	RequireFailure([&]() {
		static_cast<void>(fixture.Run(coordinate, {65536}, true));
	},
	    sgre::text::TrueTypeProgramFailure::InvalidReference);
	const auto before = fixture.Points.Zones[1].Current;
	fixture.State.Graphics.Freedom = {0x2000, 0x2000};
	RequireBoundary([&]() {
		static_cast<void>(fixture.Run(coordinate, {0}));
	},
	    "positive unit-axis");
	Require(fixture.Points.Zones[1].Current == before, "unsupported vector geometry must reject before mutating actual point owners");
	fixture.State.Graphics.Freedom = {0x4000, 0};
	fixture.Points.Zones[1].Tags.pop_back();
	RequireBoundary([&]() {
		static_cast<void>(fixture.Run(coordinate, {0}));
	},
	    "exact bounded");
	Fixture legacy;
	sgre::text::TrueTypeProgramCode code;
	code.Glyph = coordinate;
	RequireBoundary([&]() {
		static_cast<void>(sgre::text::ExecuteTrueTypeStateProgram(legacy.Font, code, sgre::text::TrueTypeProgramRange::Glyph, legacy.State, false, std::array<std::int32_t, 1>{0}));
	},
	    "requires recovered interpreter execution");
}

static void TestSharedCallAndPreparationOwners() {
	const std::array<std::uint8_t, 7> definitions{0xb0, 1, 0x2c, 0xb0, 0, 0x46, 0x2d};
	sgre::text::TrueTypeDefinitionLimits limits;
	limits.MaxFunctionDefinitions = 1;
	const sgre::text::TrueTypeFontProgram font(definitions, limits);
	Fixture fixture;
	const std::array<std::uint8_t, 3> glyph{0xb0, 1, 0x2b};
	sgre::text::TrueTypeProgramCode code;
	code.Glyph = glyph;
	const auto result = sgre::text::ExecuteTrueTypePointProgram(font, code, sgre::text::TrueTypeProgramRange::Glyph, fixture.State, fixture.Points);
	Require(result.Operands.Stack == std::vector<std::int32_t>{16} && result.MaximumCallDepth == 1, "point kernels inside genuine FDEF calls must share original code and operand owners");
	Require(font.Definitions().size() == 1 && font.Definitions()[0].Identifier == 1, "point execution must preserve immutable function definitions");
}

int main() {
	try {
		TestMdapRoundingReferencesAndWordIndices();
		TestTwilightMiapScfsAndSelectedAliases();
		TestCoordinateAndDistanceDomains();
		TestContourShiftPhysicalReferenceExclusion();
		TestVariablePointGroupsAndErrorState();
		TestIupIndependentCoordinateSpacesAndSingleTouch();
		TestVersion40PoliciesAndPerCallIupFlags();
		TestPreciseInvalidReferencesAndOwnerBoundaries();
		TestSharedCallAndPreparationOwners();
		std::cout << "truetype_point_program_tests: 9 groups passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
