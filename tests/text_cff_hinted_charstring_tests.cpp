#include "sgre/text/cff_hinted_charstring.hpp"

#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static sgre::text::CffHintedCharstringConfiguration Configuration() {
	sgre::text::CffBlueZoneInput parameters;
	parameters.VerticalScale16Dot16 = 4096;
	sgre::text::CffHintedCharstringConfiguration configuration;
	configuration.Contour.BlueZones = sgre::text::BuildCffBlueZones(parameters);
	configuration.Contour.BaseScale16Dot16 = 4096;
	configuration.DefaultWidthFontUnits = 500;
	configuration.NominalWidthFontUnits = 7;

	return configuration;
}

static sgre::text::CffHintedCharstring Execute(std::span<const std::uint8_t> bytes) {
	sgre::text::CffCharstringPrograms programs;
	programs.Glyph = bytes;

	return sgre::text::ExecuteCffHintedCharstring(programs, Configuration());
}

static void CheckWidthAndInterpreterCoordinates() {
	const std::array<std::uint8_t, 1> empty{14};
	const auto default_width = Execute(empty);
	Require(default_width.Width.Width16Dot16 == 500 * 65536 && !default_width.Width.Read && default_width.Callbacks.empty(), "an empty endchar retains default width and the native prior-width flag");
	const std::array<std::uint8_t, 6> fixed_width{255, 0, 13, 128, 0, 14};
	const auto supplied = Execute(fixed_width);
	Require(supplied.Width.Width16Dot16 == 1343488 && !supplied.Width.Read && supplied.Callbacks.empty(), "endchar must apply a fixed width plus nominal width without changing the prior-width flag");
	const std::array<std::uint8_t, 8> moved{137, 140, 141, 21, 142, 143, 5, 14};
	const auto contour = Execute(moved);
	Require(contour.Width.Width16Dot16 == 5 * 65536 && contour.Width.Read && contour.Callbacks.size() == 3, "the first move consumes the supplied width once and preserves its drawing operands");
	Require(contour.InterpreterLogicalPoint.X16Dot16 == 4 * 65536 && contour.InterpreterLogicalPoint.Y16Dot16 == 6 * 65536 && contour.FinalState.CurrentLogicalPoint.X16Dot16 == 65536 && contour.FinalState.CurrentLogicalPoint.Y16Dot16 == 2 * 65536, "interpreter coordinates persist independently from the owner's closing logical position");
}

static void CheckDrawingGroup(std::uint8_t operation, std::uint8_t operand_count, std::int32_t logical_x, std::int32_t logical_y, std::size_t callback_count, std::size_t cubic_count) {
	std::vector<std::uint8_t> program{139, 139, 21};
	for (std::uint8_t operand = 1; operand <= operand_count; ++operand) {
		program.push_back(static_cast<std::uint8_t>(139 + operand));
	}
	program.push_back(operation);
	program.push_back(14);
	const auto result = Execute(program);
	Require(result.InterpreterLogicalPoint.X16Dot16 == logical_x * 65536 && result.InterpreterLogicalPoint.Y16Dot16 == logical_y * 65536, "observed drawing groups must retain their optional and alternating axis semantics");
	Require(result.Callbacks.size() == callback_count && result.Callbacks.front().Kind == sgre::text::CffContourCallbackKind::Move, "drawing groups must preserve native pending callback counts");
	std::size_t cubics = 0;
	for (const auto& callback : result.Callbacks) {
		cubics += callback.Kind == sgre::text::CffContourCallbackKind::Cubic;
	}
	Require(cubics == cubic_count && result.Callbacks.back().Points[1].X16Dot16 == 0 && result.Callbacks.back().Points[1].Y16Dot16 == 0 && !result.FinalState.ContourOpen, "each drawing group closes its device contour after preserving its cubic callbacks");
}

static void CheckObservedDrawingGroups() {
	CheckDrawingGroup(5, 4, 4, 6, 4, 0);
	CheckDrawingGroup(6, 2, 1, 2, 4, 0);
	CheckDrawingGroup(7, 2, 2, 1, 4, 0);
	CheckDrawingGroup(8, 6, 9, 12, 3, 1);
	CheckDrawingGroup(24, 8, 16, 20, 4, 1);
	CheckDrawingGroup(25, 8, 16, 20, 4, 1);
	// Five-operand curves retain the native initial/final optional axis displacement.
	CheckDrawingGroup(26, 5, 4, 11, 3, 1);
	CheckDrawingGroup(27, 5, 10, 5, 3, 1);
	CheckDrawingGroup(30, 5, 6, 9, 3, 1);
	CheckDrawingGroup(31, 5, 8, 7, 3, 1);
}

static void CheckSharedSubroutineDeclarationsAndCounter() {
	const std::array<std::uint8_t, 7> glyph{139, 141, 1, 32, 29, 14, 14};
	const std::array<std::uint8_t, 3> global{32, 10, 11};
	const std::array<std::uint8_t, 17> local{140, 142, 3, 20, 192, 139, 139, 21, 140, 141, 142, 143, 144, 145, 8, 11, 11};
	const std::array<std::span<const std::uint8_t>, 1> globals{global};
	const std::array<std::span<const std::uint8_t>, 1> locals{local};
	sgre::text::CffCharstringPrograms programs;
	programs.Glyph = glyph;
	programs.GlobalSubroutines = globals;
	programs.LocalSubroutines = locals;
	const auto result = sgre::text::ExecuteCffHintedCharstring(programs, Configuration());
	Require(result.Callbacks.size() == 3 && result.FinalState.HorizontalStems.Count == 1 && result.FinalState.VerticalStemCount == 1 && result.FinalState.Mask.HintCount == 2, "nested local/global execution shares declaration and contour owners");
	Require(result.Width.Width16Dot16 == 500 * 65536 && result.Width.Read && result.FinalState.HorizontalStems.Stems[0].HasCachedDevicePositions && result.FinalState.Mask.Bytes[0] == 192, "a counter map prepares caches without consuming the initial primary mask's lifetime");
	Require(result.InterpreterLogicalPoint.X16Dot16 == 9 * 65536 && result.InterpreterLogicalPoint.Y16Dot16 == 12 * 65536, "subroutine return must preserve native drawing position");
}

static void CheckLateDeclarationsAndFixedWrapping() {
	const std::array<std::uint8_t, 13> later{139, 141, 1, 139, 139, 21, 148, 141, 1, 140, 140, 5, 14};
	const auto ignored = Execute(later);
	Require(ignored.FinalState.HorizontalStems.Count == 1 && ignored.FinalState.HorizontalStems.Stems[0].Start16Dot16 == 0 && ignored.FinalState.Mask.HintCount == 1, "later declarations after the automatically initialized primary mask must be ignored");
	const std::array<std::uint8_t, 10> wrapping{28, 127, 255, 139, 21, 140, 139, 5, 14, 14};
	const auto wrapped = Execute(wrapping);
	Require(wrapped.InterpreterLogicalPoint.X16Dot16 == std::numeric_limits<std::int32_t>::min() && wrapped.Callbacks[1].Points[1].X16Dot16 == std::numeric_limits<std::int32_t>::min(), "native coordinate addition must preserve signed fixed wrap without host undefined behavior");
}

static void RequireRejected(std::span<const std::uint8_t> program, std::string_view message) {
	bool failed = false;
	try {
		(void)Execute(program);
	} catch (const std::runtime_error&) {
		failed = true;
	}
	Require(failed, message);
}

static void CheckMalformedAndUnsupportedPrograms() {
	const std::array<std::uint8_t, 1> root_return{11};
	RequireRejected(root_return, "a root return must reject instead of underflowing a subroutine frame");
	const std::array<std::uint8_t, 3> truncated_fixed{255, 0, 1};
	RequireRejected(truncated_fixed, "fixed operand decoding must preflight all encoded bytes");
	const std::array<std::uint8_t, 3> escaped{12, 10, 14};
	RequireRejected(escaped, "unrecovered escaped arithmetic must produce a contextual unsupported error");
	const std::array<std::uint8_t, 5> invalid_curve{139, 139, 21, 8, 14};
	RequireRejected(invalid_curve, "an incomplete cubic operand group must fail before drawing");
	const std::array<std::uint8_t, 4> missing_mask{139, 141, 1, 19};
	RequireRejected(missing_mask, "a primary mask must contain every declared hint byte");
	const std::array<std::uint8_t, 7> fixed_subroutine{255, 255, 149, 0, 0, 10, 14};
	RequireRejected(fixed_subroutine, "native subroutine indices must keep their integer number tag");
	const std::array<std::uint8_t, 3> missing_end{139, 139, 21};
	RequireRejected(missing_end, "a glyph without an explicit valid-program endchar remains outside the accepted boundary");
	std::vector<std::uint8_t> excessive_operands(49, 139);
	excessive_operands.push_back(14);
	RequireRejected(excessive_operands, "the operand stack must remain within its native 48-number limit");
	std::vector<std::uint8_t> excessive_hints;
	for (std::size_t declaration = 0; declaration < 5; ++declaration) {
		excessive_hints.insert(excessive_hints.end(), 48, 140);
		excessive_hints.push_back(1);
	}
	excessive_hints.push_back(14);
	RequireRejected(excessive_hints, "both declaration axes share the 96-hint storage limit");
}

static void CheckRecursiveAndWorkLimits() {
	const std::array<std::uint8_t, 3> glyph{32, 10, 14};
	const std::array<std::uint8_t, 3> recursive{32, 10, 11};
	const std::array<std::span<const std::uint8_t>, 1> subroutines{recursive};
	sgre::text::CffCharstringPrograms programs;
	programs.Glyph = glyph;
	programs.LocalSubroutines = subroutines;
	bool failed = false;
	try {
		(void)sgre::text::ExecuteCffHintedCharstring(programs, Configuration());
	} catch (const std::runtime_error&) {
		failed = true;
	}
	Require(failed, "self-referencing subroutines must terminate at the bounded native frame depth");
	std::vector<std::uint8_t> excessive_callbacks{139, 139, 21};
	for (std::size_t line = 0; line < 65537; ++line) {
		excessive_callbacks.push_back(140);
		excessive_callbacks.push_back(6);
	}
	excessive_callbacks.push_back(14);
	RequireRejected(excessive_callbacks, "executed geometry must fail at its callback preparation bound");
	std::vector<std::uint8_t> excessive_work(4 * 1024 * 1024, 19);
	excessive_work.push_back(14);
	RequireRejected(excessive_work, "executed masks must terminate at the byte-work limit even without geometry allocation");
}

int main() {
	try {
		CheckWidthAndInterpreterCoordinates();
		CheckObservedDrawingGroups();
		CheckSharedSubroutineDeclarationsAndCounter();
		CheckLateDeclarationsAndFixedWrapping();
		CheckMalformedAndUnsupportedPrograms();
		CheckRecursiveAndWorkLimits();
		std::cout << "CFF hinted charstring tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
