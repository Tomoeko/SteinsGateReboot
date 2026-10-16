#include "sgre/resource/cff_outline.hpp"

#include "cff_outline_fixture.hpp"

#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static sgre::resource::CffOutline ReadFixture(const std::vector<std::uint8_t>& program, const std::vector<std::vector<std::uint8_t>>& locals = {}, const std::vector<std::vector<std::uint8_t>>& globals = {}) {
	const auto bytes = sgre::tests::MakeCffOutlineFixture(program, locals, globals);
	const sgre::resource::SfntFont sfnt(bytes);
	const sgre::resource::CffFont font(sfnt);

	return sgre::resource::ReadCffGlyphOutline(font, 0);
}

static void RequireFailure(const std::function<void()>& operation, std::string_view context) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(context) != std::string_view::npos && std::string_view(error.what()).find("CFF") != std::string_view::npos, "CFF outline failure lost its context");

		return;
	}

	throw std::runtime_error("invalid CFF outline was accepted");
}

static void RequirePoint(const sgre::resource::CffOutlinePoint& point, double x, double y) {
	Require(point.X == x && point.Y == y, "CFF outline point differs from its independent geometry");
}

static void TestContourAndWidths() {
	std::vector<std::uint8_t> program;
	sgre::tests::AppendOutlineFixtureOperation(program, {0, 0}, 21);
	sgre::tests::AppendOutlineFixtureOperation(program, {10, 0, 0, 20, -10, 0, 0, -20}, 5);
	program.push_back(14);
	const auto rectangle = ReadFixture(program);
	Require(rectangle.AdvanceWidth == 100 && rectangle.Commands.size() == 6 && rectangle.Commands.back().Kind == sgre::resource::CffOutlineCommandKind::ClosePath, "CFF rectangle width or closure differs");
	RequirePoint(rectangle.Commands[0].Points[0], 0, 0);
	RequirePoint(rectangle.Commands[1].Points[0], 10, 0);
	RequirePoint(rectangle.Commands[2].Points[0], 10, 20);
	RequirePoint(rectangle.Commands[3].Points[0], 0, 20);
	RequirePoint(rectangle.Commands[4].Points[0], 0, 0);
	program.clear();
	sgre::tests::AppendOutlineFixtureOperation(program, {5, -2, 3}, 21);
	sgre::tests::AppendOutlineFixtureOperation(program, {10, -1}, 5);
	sgre::tests::AppendOutlineFixtureOperation(program, {2, 4}, 21);
	sgre::tests::AppendOutlineFixtureOperation(program, {300, -400}, 5);
	sgre::tests::AppendOutlineFixtureOperation(program, {32767, -32768}, 5);
	sgre::tests::AppendOutlineFixtureOperation(program, {0.5, -0.25}, 5);
	program.push_back(14);
	const auto moved = ReadFixture(program);
	Require(moved.AdvanceWidth == 25 && moved.Commands.size() == 8 && moved.Commands[2].Kind == sgre::resource::CffOutlineCommandKind::ClosePath, "explicit width or implicit contour closure differs");
	RequirePoint(moved.Commands[0].Points[0], -2, 3);
	RequirePoint(moved.Commands[1].Points[0], 8, 2);
	RequirePoint(moved.Commands[3].Points[0], 10, 6);
	RequirePoint(moved.Commands[4].Points[0], 310, -394);
	RequirePoint(moved.Commands[5].Points[0], 33077, -33162);
	RequirePoint(moved.Commands[6].Points[0], 33077.5, -33162.25);
	Require(ReadFixture({14}).Commands.empty(), "empty glyph synthesized an outline");
}

static void TestCurveVariants() {
	std::vector<std::uint8_t> program;
	sgre::tests::AppendOutlineFixtureOperation(program, {0, 0}, 21);
	sgre::tests::AppendOutlineFixtureOperation(program, {1, 2, 3, 4, 5, 6}, 8);
	sgre::tests::AppendOutlineFixtureOperation(program, {1, 2, 3, 4, 5}, 31);
	sgre::tests::AppendOutlineFixtureOperation(program, {2, 3, 4, 5, 6}, 30);
	sgre::tests::AppendOutlineFixtureOperation(program, {2, 3, 4, 5, 6}, 27);
	sgre::tests::AppendOutlineFixtureOperation(program, {2, 3, 4, 5, 6}, 26);
	sgre::tests::AppendOutlineFixtureOperation(program, {1, 2, 3, 4, 5, 6, 7, 8}, 24);
	sgre::tests::AppendOutlineFixtureOperation(program, {1, 2, 3, 4, 5, 6, 7, 8}, 25);
	program.push_back(14);
	const auto outline = ReadFixture(program);
	Require(outline.Commands.size() == 11, "CFF combined curve/line command count differs");
	RequirePoint(outline.Commands[1].Points[0], 1, 2);
	RequirePoint(outline.Commands[1].Points[1], 4, 6);
	RequirePoint(outline.Commands[1].Points[2], 9, 12);
	RequirePoint(outline.Commands[2].Points[2], 17, 19);
	RequirePoint(outline.Commands[3].Points[2], 25, 31);
	RequirePoint(outline.Commands[4].Points[2], 38, 38);
	RequirePoint(outline.Commands[5].Points[2], 44, 52);
	RequirePoint(outline.Commands[6].Points[2], 53, 64);
	RequirePoint(outline.Commands[7].Points[0], 60, 72);
	RequirePoint(outline.Commands[8].Points[0], 61, 74);
	RequirePoint(outline.Commands[9].Points[2], 76, 92);
}

static void TestMasksAndSharedSubroutineStack() {
	std::vector<std::uint8_t> program;
	sgre::tests::AppendOutlineFixtureOperation(program, {5, 1, 2}, 1);
	program.push_back(19);
	program.push_back(0x80);
	sgre::tests::AppendOutlineFixtureOperation(program, {3, 4}, 23);
	program.push_back(20);
	program.push_back(0);
	sgre::tests::AppendOutlineFixtureOperation(program, {0, 0}, 21);
	program.push_back(14);
	const auto hinted = ReadFixture(program);
	Require(hinted.HintEvents.size() == 3 && hinted.HintEvents[0].Kind == sgre::resource::CffOutlineHintEventKind::DeclareHorizontalStems && hinted.HintEvents[1].Kind == sgre::resource::CffOutlineHintEventKind::HintMask && hinted.HintEvents[2].Kind == sgre::resource::CffOutlineHintEventKind::CounterMask, "native primary-mask state must ignore later axis declarations while preserving the counter event");
	Require(hinted.AdvanceWidth == 25 && hinted.StemCount == 1 && hinted.Commands.size() == 2, "CFF stem width or mask byte boundary differs");
	program.clear();
	sgre::tests::AppendOutlineFixtureOperation(program, {5, 0}, 21);
	sgre::tests::AppendOutlineFixtureOperation(program, {1, 2, -107}, 10);
	sgre::tests::AppendOutlineFixtureOperation(program, {-107}, 29);
	program.push_back(14);
	std::vector<std::uint8_t> global;
	sgre::tests::AppendOutlineFixtureOperation(global, {3, 4}, 5);
	global.push_back(11);
	const auto called = ReadFixture(program, {{5, 11}}, {global});
	Require(called.Commands.size() == 4, "CFF call/return lost drawing commands");
	RequirePoint(called.Commands[1].Points[0], 6, 2);
	RequirePoint(called.Commands[2].Points[0], 9, 6);
	const auto early_end = ReadFixture({139, 139, 21, 32, 10, 0}, {{14}});
	Require(early_end.Commands.size() == 2 && early_end.AdvanceWidth == 100, "subroutine endchar did not end the glyph");
}

static void TestMalformedAndUnsupportedPrograms() {
	for (const auto& [program, context] : std::vector<std::pair<std::vector<std::uint8_t>, std::string_view>>{{{11}, "root glyph"}, {{139}, "endchar"}, {{139, 139, 5, 14}, "contour move"}, {{12, 9, 14}, "unsupported"}, {{139, 139, 139, 139, 14}, "composite"}, {{10, 14}, "index operand"}, {{255, 0}, "truncated"}}) {
		RequireFailure([&] {
			static_cast<void>(ReadFixture(program));
		},
		    context);
	}
	std::vector<std::uint8_t> stack(49, 139);
	stack.push_back(14);
	RequireFailure([&] {
		static_cast<void>(ReadFixture(stack));
	},
	    "operand stack");
	RequireFailure([] {
		static_cast<void>(ReadFixture({32, 10, 14}, {{32, 10, 11}}));
	},
	    "depth limit");
	std::vector<std::uint8_t> excessive;
	sgre::tests::AppendOutlineFixtureOperation(excessive, {0, 0}, 21);
	for (std::size_t index = 0; index < 65536; ++index) {
		sgre::tests::AppendOutlineFixtureOperation(excessive, {0, 0}, 5);
	}
	excessive.push_back(14);
	RequireFailure([&] {
		static_cast<void>(ReadFixture(excessive));
	},
	    "command count");
}

static void TestHintEventWorkLimit() {
	std::vector<std::vector<std::uint8_t>> subroutines;
	std::vector<std::uint8_t> masks(8193, 19);
	masks.push_back(11);
	subroutines.push_back(masks);
	for (std::size_t index = 1; index < 10; ++index) {
		std::vector<std::uint8_t> calls;
		sgre::tests::AppendOutlineFixtureOperation(calls, {static_cast<double>(index - 1) - 107}, 10);
		sgre::tests::AppendOutlineFixtureOperation(calls, {static_cast<double>(index - 1) - 107}, 10);
		calls.push_back(11);
		subroutines.push_back(calls);
	}
	std::vector<std::uint8_t> program;
	sgre::tests::AppendOutlineFixtureOperation(program, {-98}, 10);
	program.push_back(14);
	RequireFailure([&] {
		static_cast<void>(ReadFixture(program, subroutines));
	},
	    "hint event count");
}

static void TestExecutionByteWorkLimit() {
	std::vector<std::vector<std::uint8_t>> subroutines{{11}};
	std::vector<std::uint8_t> repeated_calls;
	for (std::size_t index = 0; index < 8193; ++index) {
		sgre::tests::AppendOutlineFixtureOperation(repeated_calls, {-107}, 10);
	}
	repeated_calls.push_back(11);
	subroutines.push_back(repeated_calls);
	for (std::size_t index = 2; index < 10; ++index) {
		std::vector<std::uint8_t> calls;
		sgre::tests::AppendOutlineFixtureOperation(calls, {static_cast<double>(index - 1) - 107}, 10);
		sgre::tests::AppendOutlineFixtureOperation(calls, {static_cast<double>(index - 1) - 107}, 10);
		calls.push_back(11);
		subroutines.push_back(calls);
	}
	std::vector<std::uint8_t> program;
	sgre::tests::AppendOutlineFixtureOperation(program, {-98}, 10);
	program.push_back(14);
	RequireFailure([&] {
		(void)ReadFixture(program, subroutines);
	},
	    "byte-work limit");
}

static void TestHintEventOrderAndCounterState() {
	using sgre::resource::CffOutlineHintEventKind;
	std::vector<std::uint8_t> local;
	sgre::tests::AppendOutlineFixtureOperation(local, {1, 2, 3, 4}, 18);
	local.push_back(11);
	std::vector<std::uint8_t> program;
	sgre::tests::AppendOutlineFixtureOperation(program, {-107}, 10);
	program.push_back(20);
	program.push_back(0xc0);
	sgre::tests::AppendOutlineFixtureOperation(program, {5, 6}, 23);
	sgre::tests::AppendOutlineFixtureOperation(program, {7, 8}, 19);
	program.push_back(0xf0);
	sgre::tests::AppendOutlineFixtureOperation(program, {0, 0}, 21);
	sgre::tests::AppendOutlineFixtureOperation(program, {10, 20}, 5);
	program.push_back(19);
	program.push_back(0xa0);
	program.push_back(14);
	const auto outline = ReadFixture(program, {local});
	Require(outline.StemCount == 4 && outline.HintEvents.size() == 6, "counter masks must permit later declarations until the first primary mask");
	Require(outline.HintEvents[0].Kind == CffOutlineHintEventKind::DeclareHorizontalStems && outline.HintEvents[0].Stems.size() == 2 && outline.HintEvents[0].Stems[0].StartFontUnits == 1 && outline.HintEvents[0].Stems[0].EndFontUnits == 3 && outline.HintEvents[0].Stems[1].StartFontUnits == 6 && outline.HintEvents[0].Stems[1].EndFontUnits == 10, "shared-subroutine stem events must retain per-call absolute deltas");
	Require(outline.HintEvents[1].Kind == CffOutlineHintEventKind::CounterMask && outline.HintEvents[1].MaskBytes[0] == 0xc0 && outline.HintEvents[1].HorizontalStemCount == 2 && outline.HintEvents[1].VerticalStemCount == 0, "counter mask event lost its declaration snapshot");
	Require(outline.HintEvents[3].Kind == CffOutlineHintEventKind::DeclareVerticalStems && outline.HintEvents[3].Stems[0].StartFontUnits == 7 && outline.HintEvents[3].VerticalStemCount == 2 && outline.HintEvents[4].Kind == CffOutlineHintEventKind::HintMask && outline.HintEvents[4].MaskBytes[0] == 0xf0, "mask trailing operands must declare vertical stems before retaining bytes");
	Require(outline.HintEvents[0].CommandIndex == 0 && outline.HintEvents[4].CommandIndex == 0 && outline.HintEvents[5].CommandIndex == 2 && outline.HintEvents[5].MaskBytes[0] == 0xa0, "mask changes must retain the original raw command boundary");
	RequireFailure([] {
		(void)ReadFixture({139, 140, 1, 19, 0x80, 139, 140, 19, 0x80, 14});
	},
	    "mask operands after its primary");
}

int main() {
	try {
		TestContourAndWidths();
		TestCurveVariants();
		TestMasksAndSharedSubroutineStack();
		TestMalformedAndUnsupportedPrograms();
		TestHintEventWorkLimit();
		TestHintEventOrderAndCounterState();
		TestExecutionByteWorkLimit();
		std::cout << "CFF outline tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << "CFF outline test failure: " << error.what() << '\n';

		return 1;
	}
}
