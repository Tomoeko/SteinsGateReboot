#include "sgre/text/reveal.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static void RequireFailure(const std::function<void()>& operation) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find("text reveal") != std::string_view::npos, "reveal failure lost its context");

		return;
	}

	throw std::runtime_error("invalid reveal input was accepted");
}

struct NativeRevealFixture {
	std::size_t Count = 0;
	std::uint32_t First = 0;
	bool Force = false;
	std::int32_t Target = 0;
	std::uint32_t PreviousFirst = 0;
	std::array<std::uint32_t, 4> CursorInputs{};
	std::array<std::uint32_t, 6> ElementInputs{};
	std::array<std::uint32_t, 3> CursorOutputs{};
	std::array<std::uint32_t, 6> ElementOutputs{};
};

static void TestOriginalCompleteRevealOperation() {
	// Complete unmodified 0x140030250 executions produced these output words.
	const std::array<NativeRevealFixture, 3> fixtures = {{
	    {3, 0, true, -7995, 1, {0x418367a6, 0x4368987d, 0x43a2fbd1, 0x4438b996}, {0xc248434e, 0x441abc70, 0x443efc00}, {0, 3, 0x4487295e}, {0x442c3561, 0x44a9bb03, 0x44bbdacb}},
	    {6, 5, false, 2611, 2, {0x41b277d8, 0x445dec81, 0x435a9d0e, 0x41e2d330}, {0x43f61148, 0x44257917, 0x444564b3, 0x420bc13c, 0x409664ca, 0x442d29c4}, {0, 6, 0x45233000}, {0x43f61148, 0x44257917, 0x444564b3, 0x420bc13c, 0x409664ca, 0x4434405e}},
	    {4, 0, true, 6297, 10, {0x420bf4cf, 0x4425c239, 0x433dc248, 0x43d8bf01}, {0x44437087, 0x41ab2b4c, 0x4377522a, 0x43bcbbc2}, {0, 4, 0x45c4c800}, {0x46a2b9f7, 0x4477ece8, 0x45d72f4c, 0x4620a54a}},
	}};
	for (const auto& fixture : fixtures) {
		sgre::text::TextRevealCursor cursor;
		cursor.SegmentFirstElement = fixture.PreviousFirst;
		cursor.NextRelativeTime = std::bit_cast<float>(fixture.CursorInputs[1]);
		cursor.LastRelativeTime = std::bit_cast<float>(fixture.CursorInputs[2]);
		cursor.SegmentBaseTime = std::bit_cast<float>(fixture.CursorInputs[3]);
		std::array<float, 6> times{};
		for (std::size_t index = 0; index < fixture.Count; ++index) {
			times[index] = std::bit_cast<float>(fixture.ElementInputs[index]);
		}

		static_cast<void>(sgre::text::RetimeTextRevealSegment(cursor, std::span<float>(times).first(fixture.Count), fixture.First, fixture.Force, std::bit_cast<float>(fixture.CursorInputs[0]), fixture.Target));
		Require(std::bit_cast<std::uint32_t>(cursor.NextRelativeTime) == fixture.CursorOutputs[0] && cursor.SegmentFirstElement == fixture.CursorOutputs[1] && std::bit_cast<std::uint32_t>(cursor.SegmentBaseTime) == fixture.CursorOutputs[2], "reveal cursor differs from original instructions");
		for (std::size_t index = 0; index < fixture.Count; ++index) {
			Require(std::bit_cast<std::uint32_t>(times[index]) == fixture.ElementOutputs[index], "reveal element time differs from original instructions");
		}
	}
}

static void TestNaturalAndTargetDurations() {
	sgre::text::TextRevealCursor cursor;
	cursor.NextRelativeTime = 30;
	cursor.LastRelativeTime = 20;
	cursor.SegmentBaseTime = 100;
	std::array<float, 4> times{3, 0, 10, 20};
	const auto natural = sgre::text::RetimeTextRevealSegment(cursor, times, 1, false, 10, 0);
	Require(natural.Changed && natural.Scale == 1 && natural.SegmentDuration == 30 && times == std::array<float, 4>{3, 100, 110, 120}, "natural reveal segment did not retain delay and base time");
	Require(cursor.NextRelativeTime == 0 && cursor.SegmentBaseTime == 130 && cursor.SegmentFirstElement == 4 && cursor.LastRelativeTime == 20, "reveal segment advanced or reset the wrong cursor fields");
	cursor.SegmentBaseTime = 100;
	times = {3, 0, 10, 20};
	const auto shortened = sgre::text::RetimeTextRevealSegment(cursor, times, 1, false, 10, 115);
	Require(shortened.Scale == 0.5f && shortened.SegmentDuration == 15 && times == std::array<float, 4>{3, 100, 105, 110} && cursor.SegmentBaseTime == 115, "shorter target did not compress the current segment");
	cursor.SegmentBaseTime = 100;
	times = {3, 0, 10, 20};
	const auto extended = sgre::text::RetimeTextRevealSegment(cursor, times, 1, false, 10, 160);
	Require(extended.Scale == 1 && extended.SegmentDuration == 60 && times[3] == 120 && cursor.SegmentBaseTime == 160, "unforced later target stretched element reveal times");
	cursor.SegmentBaseTime = 100;
	times = {3, 0, 10, 20};
	const auto forced = sgre::text::RetimeTextRevealSegment(cursor, times, 1, true, 10, 160);
	Require(forced.Scale == 2 && forced.SegmentDuration == 60 && times[3] == 140 && cursor.SegmentBaseTime == 160, "forced target did not scale the selected segment");
}

static void TestEmptyAndSignedZero() {
	sgre::text::TextRevealCursor cursor;
	cursor.NextRelativeTime = 20;
	cursor.SegmentBaseTime = 12;
	std::array<float, 1> times{99};
	const auto unchanged = sgre::text::RetimeTextRevealSegment(cursor, times, 1, true, 10, 0);
	Require(!unchanged.Changed && times[0] == 99 && cursor.NextRelativeTime == 20 && cursor.SegmentBaseTime == 12, "empty selection mutated timing state");
	cursor = {};
	cursor.SegmentBaseTime = 0.0f;
	cursor.LastRelativeTime = -0.0f;
	const auto zero = sgre::text::RetimeTextRevealSegment(cursor, times, 0, false, -0.0f, 0);
	Require(zero.Changed && std::bit_cast<std::uint32_t>(zero.SegmentDuration) == 0, "native MAXSS equal signed-zero selection changed");
	Require(cursor.SegmentFirstElement == 1, "retiming did not count the selected element");
}

static void TestPreflightAndLimits() {
	sgre::text::TextRevealCursor cursor;
	cursor.LastRelativeTime = 2;
	cursor.NextRelativeTime = 3;
	cursor.SegmentBaseTime = 10;
	std::array<float, 3> times{1, 2, std::numeric_limits<float>::infinity()};
	RequireFailure([&] {
		static_cast<void>(sgre::text::RetimeTextRevealSegment(cursor, times, 0, false, 1, 0));
	});
	Require(times[0] == 1 && times[1] == 2 && cursor.NextRelativeTime == 3 && cursor.SegmentBaseTime == 10, "invalid reveal preparation partially mutated output");
	times = {1, 2, 3};
	cursor.LastRelativeTime = 0;
	RequireFailure([&] {
		static_cast<void>(sgre::text::RetimeTextRevealSegment(cursor, times, 0, true, 0, 100));
	});
	Require(times == std::array<float, 3>{1, 2, 3} && cursor.SegmentBaseTime == 10, "zero-duration division changed output before validation");
	cursor.SegmentFirstElement = 1'000'001;
	RequireFailure([&] {
		static_cast<void>(sgre::text::RetimeTextRevealSegment(cursor, times, 0, false, 1, 0));
	});
	cursor = {};
	RequireFailure([&] {
		static_cast<void>(sgre::text::RetimeTextRevealSegment(cursor, times, 1'000'001, false, 1, 0));
	});
	RequireFailure([&] {
		static_cast<void>(sgre::text::RetimeTextRevealSegment(cursor, times, 0, false, std::numeric_limits<float>::quiet_NaN(), 0));
	});
}

int main() {
	try {
		TestOriginalCompleteRevealOperation();
		TestNaturalAndTargetDurations();
		TestEmptyAndSignedZero();
		TestPreflightAndLimits();
		std::cout << "Text reveal tests passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
