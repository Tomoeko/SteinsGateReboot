#include "sgre/script/native_regexp.hpp"
#include "sgre/script/regexp_bindings.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view fragment) {
	try {
		operation();
	} catch (const std::exception& error) {
		Require(std::string_view(error.what()).find(fragment) != std::string_view::npos, "regexp failure did not contain its expected context");

		return;
	}

	throw std::runtime_error("regexp operation unexpectedly succeeded");
}

static void CheckNativeCase(sgre::script::NativeRegexp& program, std::string_view pattern, std::string_view text, bool expected_match, std::optional<sgre::script::NativeRegexpRange> expected_search, std::span<const sgre::script::NativeRegexpCapture> expected_captures) {
	const auto match = program.Match(text);
	const auto search = program.Search(text);
	if (match != expected_match || search != expected_search) {
		throw std::runtime_error("native regexp match/search mismatch: pattern=" + std::string(pattern) + " text=" + std::string(text));
	}
	if (search) {
		const auto captures = program.Captures();
		Require(captures.size() == expected_captures.size(), "native regexp capture count mismatch");
		for (std::size_t index = 0; index < captures.size(); ++index) {
			if (captures[index].Begin != expected_captures[index].Begin || captures[index].Length != expected_captures[index].Length) {
				throw std::runtime_error("native regexp capture mismatch: pattern=" + std::string(pattern) + " text=" + std::string(text) + " group=" + std::to_string(index));
			}
		}
	}
}

static void TestNativeVectors() {
	RequireFailure([] {
		sgre::script::NativeRegexp program("");
	},
	    "letter expected");

	sgre::script::NativeRegexp program_1("a");
	Require(program_1.SubexpressionCount() == 1, "native regexp subexpression count differs");
	CheckNativeCase(program_1, "a", "", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_1, "a", "a", true, sgre::script::NativeRegexpRange{0, 1}, std::array<sgre::script::NativeRegexpCapture, 1>{{{0, 1}}});
	CheckNativeCase(program_1, "a", "ab", false, sgre::script::NativeRegexpRange{0, 1}, std::array<sgre::script::NativeRegexpCapture, 1>{{{0, 1}}});
	CheckNativeCase(program_1, "a", "abc", false, sgre::script::NativeRegexpRange{0, 1}, std::array<sgre::script::NativeRegexpCapture, 1>{{{0, 1}}});
	CheckNativeCase(program_1, "a", "aabc", false, sgre::script::NativeRegexpRange{0, 1}, std::array<sgre::script::NativeRegexpCapture, 1>{{{0, 1}}});
	CheckNativeCase(program_1, "a", "aaab", false, sgre::script::NativeRegexpRange{0, 1}, std::array<sgre::script::NativeRegexpCapture, 1>{{{0, 1}}});
	CheckNativeCase(program_1, "a", "aaaa", false, sgre::script::NativeRegexpRange{0, 1}, std::array<sgre::script::NativeRegexpCapture, 1>{{{0, 1}}});
	CheckNativeCase(program_1, "a", " b abc c ", false, sgre::script::NativeRegexpRange{3, 4}, std::array<sgre::script::NativeRegexpCapture, 1>{{{3, 1}}});
	CheckNativeCase(program_1, "a", "RM_", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_1, "a", "RM_test", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_1, "a", "1m2d3:4", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_1, "a", "5/27(day) 12:34", false, sgre::script::NativeRegexpRange{6, 7}, std::array<sgre::script::NativeRegexpCapture, 1>{{{6, 1}}});
	CheckNativeCase(program_1, "a", "before</d>after", false, sgre::script::NativeRegexpRange{10, 11}, std::array<sgre::script::NativeRegexpCapture, 1>{{{10, 1}}});
	CheckNativeCase(program_1, "a", "bbb", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_1, "a", "7", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});

	sgre::script::NativeRegexp program_2("a|ab");
	Require(program_2.SubexpressionCount() == 1, "native regexp subexpression count differs");
	CheckNativeCase(program_2, "a|ab", "", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_2, "a|ab", "a", true, sgre::script::NativeRegexpRange{0, 1}, std::array<sgre::script::NativeRegexpCapture, 1>{{{0, 1}}});
	CheckNativeCase(program_2, "a|ab", "ab", false, sgre::script::NativeRegexpRange{0, 1}, std::array<sgre::script::NativeRegexpCapture, 1>{{{0, 1}}});
	CheckNativeCase(program_2, "a|ab", "abc", false, sgre::script::NativeRegexpRange{0, 1}, std::array<sgre::script::NativeRegexpCapture, 1>{{{0, 1}}});
	CheckNativeCase(program_2, "a|ab", "aabc", false, sgre::script::NativeRegexpRange{0, 1}, std::array<sgre::script::NativeRegexpCapture, 1>{{{0, 1}}});
	CheckNativeCase(program_2, "a|ab", "aaab", false, sgre::script::NativeRegexpRange{0, 1}, std::array<sgre::script::NativeRegexpCapture, 1>{{{0, 1}}});
	CheckNativeCase(program_2, "a|ab", "aaaa", false, sgre::script::NativeRegexpRange{0, 1}, std::array<sgre::script::NativeRegexpCapture, 1>{{{0, 1}}});
	CheckNativeCase(program_2, "a|ab", " b abc c ", false, sgre::script::NativeRegexpRange{3, 4}, std::array<sgre::script::NativeRegexpCapture, 1>{{{3, 1}}});
	CheckNativeCase(program_2, "a|ab", "RM_", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_2, "a|ab", "RM_test", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_2, "a|ab", "1m2d3:4", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_2, "a|ab", "5/27(day) 12:34", false, sgre::script::NativeRegexpRange{6, 7}, std::array<sgre::script::NativeRegexpCapture, 1>{{{6, 1}}});
	CheckNativeCase(program_2, "a|ab", "before</d>after", false, sgre::script::NativeRegexpRange{10, 11}, std::array<sgre::script::NativeRegexpCapture, 1>{{{10, 1}}});
	CheckNativeCase(program_2, "a|ab", "bbb", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_2, "a|ab", "7", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});

	sgre::script::NativeRegexp program_3("(a|ab)c");
	Require(program_3.SubexpressionCount() == 2, "native regexp subexpression count differs");
	CheckNativeCase(program_3, "(a|ab)c", "", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_3, "(a|ab)c", "a", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_3, "(a|ab)c", "ab", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_3, "(a|ab)c", "abc", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_3, "(a|ab)c", "aabc", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_3, "(a|ab)c", "aaab", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_3, "(a|ab)c", "aaaa", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_3, "(a|ab)c", " b abc c ", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_3, "(a|ab)c", "RM_", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_3, "(a|ab)c", "RM_test", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_3, "(a|ab)c", "1m2d3:4", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_3, "(a|ab)c", "5/27(day) 12:34", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_3, "(a|ab)c", "before</d>after", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_3, "(a|ab)c", "bbb", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_3, "(a|ab)c", "7", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});

	sgre::script::NativeRegexp program_4("a*ab");
	Require(program_4.SubexpressionCount() == 1, "native regexp subexpression count differs");
	CheckNativeCase(program_4, "a*ab", "", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_4, "a*ab", "a", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_4, "a*ab", "ab", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_4, "a*ab", "abc", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_4, "a*ab", "aabc", false, sgre::script::NativeRegexpRange{0, 3}, std::array<sgre::script::NativeRegexpCapture, 1>{{{0, 3}}});
	CheckNativeCase(program_4, "a*ab", "aaab", false, sgre::script::NativeRegexpRange{1, 4}, std::array<sgre::script::NativeRegexpCapture, 1>{{{1, 3}}});
	CheckNativeCase(program_4, "a*ab", "aaaa", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_4, "a*ab", " b abc c ", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_4, "a*ab", "RM_", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_4, "a*ab", "RM_test", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_4, "a*ab", "1m2d3:4", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_4, "a*ab", "5/27(day) 12:34", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_4, "a*ab", "before</d>after", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_4, "a*ab", "bbb", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_4, "a*ab", "7", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});

	sgre::script::NativeRegexp program_5("a+ab");
	Require(program_5.SubexpressionCount() == 1, "native regexp subexpression count differs");
	CheckNativeCase(program_5, "a+ab", "", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_5, "a+ab", "a", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_5, "a+ab", "ab", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_5, "a+ab", "abc", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_5, "a+ab", "aabc", false, sgre::script::NativeRegexpRange{0, 3}, std::array<sgre::script::NativeRegexpCapture, 1>{{{0, 3}}});
	CheckNativeCase(program_5, "a+ab", "aaab", false, sgre::script::NativeRegexpRange{1, 4}, std::array<sgre::script::NativeRegexpCapture, 1>{{{1, 3}}});
	CheckNativeCase(program_5, "a+ab", "aaaa", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_5, "a+ab", " b abc c ", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_5, "a+ab", "RM_", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_5, "a+ab", "RM_test", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_5, "a+ab", "1m2d3:4", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_5, "a+ab", "5/27(day) 12:34", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_5, "a+ab", "before</d>after", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_5, "a+ab", "bbb", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_5, "a+ab", "7", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});

	sgre::script::NativeRegexp program_6("(a*)ab");
	Require(program_6.SubexpressionCount() == 2, "native regexp subexpression count differs");
	CheckNativeCase(program_6, "(a*)ab", "", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_6, "(a*)ab", "a", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_6, "(a*)ab", "ab", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_6, "(a*)ab", "abc", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_6, "(a*)ab", "aabc", false, sgre::script::NativeRegexpRange{0, 3}, std::array<sgre::script::NativeRegexpCapture, 2>{{{0, 3}, {0, 1}}});
	CheckNativeCase(program_6, "(a*)ab", "aaab", false, sgre::script::NativeRegexpRange{1, 4}, std::array<sgre::script::NativeRegexpCapture, 2>{{{1, 3}, {1, 1}}});
	CheckNativeCase(program_6, "(a*)ab", "aaaa", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_6, "(a*)ab", " b abc c ", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_6, "(a*)ab", "RM_", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_6, "(a*)ab", "RM_test", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_6, "(a*)ab", "1m2d3:4", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_6, "(a*)ab", "5/27(day) 12:34", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_6, "(a*)ab", "before</d>after", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_6, "(a*)ab", "bbb", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_6, "(a*)ab", "7", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});

	sgre::script::NativeRegexp program_7(".*a");
	Require(program_7.SubexpressionCount() == 1, "native regexp subexpression count differs");
	CheckNativeCase(program_7, ".*a", "", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_7, ".*a", "a", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_7, ".*a", "ab", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_7, ".*a", "abc", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_7, ".*a", "aabc", false, sgre::script::NativeRegexpRange{0, 2}, std::array<sgre::script::NativeRegexpCapture, 1>{{{0, 2}}});
	CheckNativeCase(program_7, ".*a", "aaab", false, sgre::script::NativeRegexpRange{0, 2}, std::array<sgre::script::NativeRegexpCapture, 1>{{{0, 2}}});
	CheckNativeCase(program_7, ".*a", "aaaa", false, sgre::script::NativeRegexpRange{0, 2}, std::array<sgre::script::NativeRegexpCapture, 1>{{{0, 2}}});
	CheckNativeCase(program_7, ".*a", " b abc c ", false, sgre::script::NativeRegexpRange{0, 4}, std::array<sgre::script::NativeRegexpCapture, 1>{{{0, 4}}});
	CheckNativeCase(program_7, ".*a", "RM_", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_7, ".*a", "RM_test", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_7, ".*a", "1m2d3:4", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_7, ".*a", "5/27(day) 12:34", false, sgre::script::NativeRegexpRange{0, 7}, std::array<sgre::script::NativeRegexpCapture, 1>{{{0, 7}}});
	CheckNativeCase(program_7, ".*a", "before</d>after", false, sgre::script::NativeRegexpRange{0, 11}, std::array<sgre::script::NativeRegexpCapture, 1>{{{0, 11}}});
	CheckNativeCase(program_7, ".*a", "bbb", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_7, ".*a", "7", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});

	sgre::script::NativeRegexp program_8("^RM_.*");
	Require(program_8.SubexpressionCount() == 1, "native regexp subexpression count differs");
	CheckNativeCase(program_8, "^RM_.*", "", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_8, "^RM_.*", "a", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_8, "^RM_.*", "ab", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_8, "^RM_.*", "abc", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_8, "^RM_.*", "aabc", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_8, "^RM_.*", "aaab", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_8, "^RM_.*", "aaaa", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_8, "^RM_.*", " b abc c ", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_8, "^RM_.*", "RM_", false, sgre::script::NativeRegexpRange{0, 4}, std::array<sgre::script::NativeRegexpCapture, 1>{{{0, 4}}});
	CheckNativeCase(program_8, "^RM_.*", "RM_test", true, sgre::script::NativeRegexpRange{0, 7}, std::array<sgre::script::NativeRegexpCapture, 1>{{{0, 7}}});
	CheckNativeCase(program_8, "^RM_.*", "1m2d3:4", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_8, "^RM_.*", "5/27(day) 12:34", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_8, "^RM_.*", "before</d>after", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_8, "^RM_.*", "bbb", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_8, "^RM_.*", "7", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});

	sgre::script::NativeRegexp program_9("(.+)m(.+)d(.+):(.+)");
	Require(program_9.SubexpressionCount() == 5, "native regexp subexpression count differs");
	CheckNativeCase(program_9, "(.+)m(.+)d(.+):(.+)", "", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_9, "(.+)m(.+)d(.+):(.+)", "a", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_9, "(.+)m(.+)d(.+):(.+)", "ab", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_9, "(.+)m(.+)d(.+):(.+)", "abc", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_9, "(.+)m(.+)d(.+):(.+)", "aabc", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_9, "(.+)m(.+)d(.+):(.+)", "aaab", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_9, "(.+)m(.+)d(.+):(.+)", "aaaa", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_9, "(.+)m(.+)d(.+):(.+)", " b abc c ", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_9, "(.+)m(.+)d(.+):(.+)", "RM_", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_9, "(.+)m(.+)d(.+):(.+)", "RM_test", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_9, "(.+)m(.+)d(.+):(.+)", "1m2d3:4", true, sgre::script::NativeRegexpRange{0, 7}, std::array<sgre::script::NativeRegexpCapture, 5>{{{0, 7}, {0, 1}, {2, 1}, {4, 1}, {6, 1}}});
	CheckNativeCase(program_9, "(.+)m(.+)d(.+):(.+)", "5/27(day) 12:34", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_9, "(.+)m(.+)d(.+):(.+)", "before</d>after", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_9, "(.+)m(.+)d(.+):(.+)", "bbb", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_9, "(.+)m(.+)d(.+):(.+)", "7", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});

	sgre::script::NativeRegexp program_10("(\\d)/(\\d+)\\(.+\\) (\\d+):(\\d+)");
	Require(program_10.SubexpressionCount() == 5, "native regexp subexpression count differs");
	CheckNativeCase(program_10, "(\\d)/(\\d+)\\(.+\\) (\\d+):(\\d+)", "", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_10, "(\\d)/(\\d+)\\(.+\\) (\\d+):(\\d+)", "a", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_10, "(\\d)/(\\d+)\\(.+\\) (\\d+):(\\d+)", "ab", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_10, "(\\d)/(\\d+)\\(.+\\) (\\d+):(\\d+)", "abc", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_10, "(\\d)/(\\d+)\\(.+\\) (\\d+):(\\d+)", "aabc", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_10, "(\\d)/(\\d+)\\(.+\\) (\\d+):(\\d+)", "aaab", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_10, "(\\d)/(\\d+)\\(.+\\) (\\d+):(\\d+)", "aaaa", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_10, "(\\d)/(\\d+)\\(.+\\) (\\d+):(\\d+)", " b abc c ", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_10, "(\\d)/(\\d+)\\(.+\\) (\\d+):(\\d+)", "RM_", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_10, "(\\d)/(\\d+)\\(.+\\) (\\d+):(\\d+)", "RM_test", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_10, "(\\d)/(\\d+)\\(.+\\) (\\d+):(\\d+)", "1m2d3:4", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_10, "(\\d)/(\\d+)\\(.+\\) (\\d+):(\\d+)", "5/27(day) 12:34", true, sgre::script::NativeRegexpRange{0, 15}, std::array<sgre::script::NativeRegexpCapture, 5>{{{0, 15}, {0, 1}, {2, 2}, {10, 2}, {13, 2}}});
	CheckNativeCase(program_10, "(\\d)/(\\d+)\\(.+\\) (\\d+):(\\d+)", "before</d>after", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_10, "(\\d)/(\\d+)\\(.+\\) (\\d+):(\\d+)", "bbb", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_10, "(\\d)/(\\d+)\\(.+\\) (\\d+):(\\d+)", "7", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});

	sgre::script::NativeRegexp program_11("(</e>|</d>)");
	Require(program_11.SubexpressionCount() == 2, "native regexp subexpression count differs");
	CheckNativeCase(program_11, "(</e>|</d>)", "", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_11, "(</e>|</d>)", "a", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_11, "(</e>|</d>)", "ab", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_11, "(</e>|</d>)", "abc", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_11, "(</e>|</d>)", "aabc", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_11, "(</e>|</d>)", "aaab", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_11, "(</e>|</d>)", "aaaa", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_11, "(</e>|</d>)", " b abc c ", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_11, "(</e>|</d>)", "RM_", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_11, "(</e>|</d>)", "RM_test", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_11, "(</e>|</d>)", "1m2d3:4", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_11, "(</e>|</d>)", "5/27(day) 12:34", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_11, "(</e>|</d>)", "before</d>after", false, sgre::script::NativeRegexpRange{6, 10}, std::array<sgre::script::NativeRegexpCapture, 2>{{{6, 4}, {6, 4}}});
	CheckNativeCase(program_11, "(</e>|</d>)", "bbb", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_11, "(</e>|</d>)", "7", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});

	sgre::script::NativeRegexp program_12("[a-z]+");
	Require(program_12.SubexpressionCount() == 1, "native regexp subexpression count differs");
	CheckNativeCase(program_12, "[a-z]+", "", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_12, "[a-z]+", "a", true, sgre::script::NativeRegexpRange{0, 1}, std::array<sgre::script::NativeRegexpCapture, 1>{{{0, 1}}});
	CheckNativeCase(program_12, "[a-z]+", "ab", true, sgre::script::NativeRegexpRange{0, 2}, std::array<sgre::script::NativeRegexpCapture, 1>{{{0, 2}}});
	CheckNativeCase(program_12, "[a-z]+", "abc", true, sgre::script::NativeRegexpRange{0, 3}, std::array<sgre::script::NativeRegexpCapture, 1>{{{0, 3}}});
	CheckNativeCase(program_12, "[a-z]+", "aabc", true, sgre::script::NativeRegexpRange{0, 4}, std::array<sgre::script::NativeRegexpCapture, 1>{{{0, 4}}});
	CheckNativeCase(program_12, "[a-z]+", "aaab", true, sgre::script::NativeRegexpRange{0, 4}, std::array<sgre::script::NativeRegexpCapture, 1>{{{0, 4}}});
	CheckNativeCase(program_12, "[a-z]+", "aaaa", true, sgre::script::NativeRegexpRange{0, 4}, std::array<sgre::script::NativeRegexpCapture, 1>{{{0, 4}}});
	CheckNativeCase(program_12, "[a-z]+", " b abc c ", false, sgre::script::NativeRegexpRange{1, 2}, std::array<sgre::script::NativeRegexpCapture, 1>{{{1, 1}}});
	CheckNativeCase(program_12, "[a-z]+", "RM_", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_12, "[a-z]+", "RM_test", false, sgre::script::NativeRegexpRange{3, 7}, std::array<sgre::script::NativeRegexpCapture, 1>{{{3, 4}}});
	CheckNativeCase(program_12, "[a-z]+", "1m2d3:4", false, sgre::script::NativeRegexpRange{1, 2}, std::array<sgre::script::NativeRegexpCapture, 1>{{{1, 1}}});
	CheckNativeCase(program_12, "[a-z]+", "5/27(day) 12:34", false, sgre::script::NativeRegexpRange{5, 8}, std::array<sgre::script::NativeRegexpCapture, 1>{{{5, 3}}});
	CheckNativeCase(program_12, "[a-z]+", "before</d>after", false, sgre::script::NativeRegexpRange{0, 6}, std::array<sgre::script::NativeRegexpCapture, 1>{{{0, 6}}});
	CheckNativeCase(program_12, "[a-z]+", "bbb", true, sgre::script::NativeRegexpRange{0, 3}, std::array<sgre::script::NativeRegexpCapture, 1>{{{0, 3}}});
	CheckNativeCase(program_12, "[a-z]+", "7", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});

	sgre::script::NativeRegexp program_13("[^a]+");
	Require(program_13.SubexpressionCount() == 1, "native regexp subexpression count differs");
	CheckNativeCase(program_13, "[^a]+", "", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_13, "[^a]+", "a", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_13, "[^a]+", "ab", false, sgre::script::NativeRegexpRange{1, 2}, std::array<sgre::script::NativeRegexpCapture, 1>{{{1, 1}}});
	CheckNativeCase(program_13, "[^a]+", "abc", false, sgre::script::NativeRegexpRange{1, 3}, std::array<sgre::script::NativeRegexpCapture, 1>{{{1, 2}}});
	CheckNativeCase(program_13, "[^a]+", "aabc", false, sgre::script::NativeRegexpRange{2, 4}, std::array<sgre::script::NativeRegexpCapture, 1>{{{2, 2}}});
	CheckNativeCase(program_13, "[^a]+", "aaab", false, sgre::script::NativeRegexpRange{3, 4}, std::array<sgre::script::NativeRegexpCapture, 1>{{{3, 1}}});
	CheckNativeCase(program_13, "[^a]+", "aaaa", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_13, "[^a]+", " b abc c ", false, sgre::script::NativeRegexpRange{0, 3}, std::array<sgre::script::NativeRegexpCapture, 1>{{{0, 3}}});
	CheckNativeCase(program_13, "[^a]+", "RM_", true, sgre::script::NativeRegexpRange{0, 3}, std::array<sgre::script::NativeRegexpCapture, 1>{{{0, 3}}});
	CheckNativeCase(program_13, "[^a]+", "RM_test", true, sgre::script::NativeRegexpRange{0, 7}, std::array<sgre::script::NativeRegexpCapture, 1>{{{0, 7}}});
	CheckNativeCase(program_13, "[^a]+", "1m2d3:4", true, sgre::script::NativeRegexpRange{0, 7}, std::array<sgre::script::NativeRegexpCapture, 1>{{{0, 7}}});
	CheckNativeCase(program_13, "[^a]+", "5/27(day) 12:34", false, sgre::script::NativeRegexpRange{0, 6}, std::array<sgre::script::NativeRegexpCapture, 1>{{{0, 6}}});
	CheckNativeCase(program_13, "[^a]+", "before</d>after", false, sgre::script::NativeRegexpRange{0, 10}, std::array<sgre::script::NativeRegexpCapture, 1>{{{0, 10}}});
	CheckNativeCase(program_13, "[^a]+", "bbb", true, sgre::script::NativeRegexpRange{0, 3}, std::array<sgre::script::NativeRegexpCapture, 1>{{{0, 3}}});
	CheckNativeCase(program_13, "[^a]+", "7", true, sgre::script::NativeRegexpRange{0, 1}, std::array<sgre::script::NativeRegexpCapture, 1>{{{0, 1}}});

	sgre::script::NativeRegexp program_14("\\babc\\b");
	Require(program_14.SubexpressionCount() == 1, "native regexp subexpression count differs");
	CheckNativeCase(program_14, "\\babc\\b", "", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_14, "\\babc\\b", "a", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_14, "\\babc\\b", "ab", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_14, "\\babc\\b", "abc", true, sgre::script::NativeRegexpRange{0, 3}, std::array<sgre::script::NativeRegexpCapture, 1>{{{0, 3}}});
	CheckNativeCase(program_14, "\\babc\\b", "aabc", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_14, "\\babc\\b", "aaab", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_14, "\\babc\\b", "aaaa", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_14, "\\babc\\b", " b abc c ", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_14, "\\babc\\b", "RM_", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_14, "\\babc\\b", "RM_test", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_14, "\\babc\\b", "1m2d3:4", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_14, "\\babc\\b", "5/27(day) 12:34", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_14, "\\babc\\b", "before</d>after", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_14, "\\babc\\b", "bbb", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_14, "\\babc\\b", "7", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});

	sgre::script::NativeRegexp program_15("(a)?b");
	Require(program_15.SubexpressionCount() == 2, "native regexp subexpression count differs");
	CheckNativeCase(program_15, "(a)?b", "", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_15, "(a)?b", "a", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_15, "(a)?b", "ab", true, sgre::script::NativeRegexpRange{0, 2}, std::array<sgre::script::NativeRegexpCapture, 2>{{{0, 2}, {0, 1}}});
	CheckNativeCase(program_15, "(a)?b", "abc", false, sgre::script::NativeRegexpRange{0, 2}, std::array<sgre::script::NativeRegexpCapture, 2>{{{0, 2}, {0, 1}}});
	CheckNativeCase(program_15, "(a)?b", "aabc", false, sgre::script::NativeRegexpRange{1, 3}, std::array<sgre::script::NativeRegexpCapture, 2>{{{1, 2}, {1, 1}}});
	CheckNativeCase(program_15, "(a)?b", "aaab", false, sgre::script::NativeRegexpRange{2, 4}, std::array<sgre::script::NativeRegexpCapture, 2>{{{2, 2}, {2, 1}}});
	CheckNativeCase(program_15, "(a)?b", "aaaa", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_15, "(a)?b", " b abc c ", false, sgre::script::NativeRegexpRange{1, 2}, std::array<sgre::script::NativeRegexpCapture, 2>{{{1, 1}, {std::nullopt, 0}}});
	CheckNativeCase(program_15, "(a)?b", "RM_", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_15, "(a)?b", "RM_test", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_15, "(a)?b", "1m2d3:4", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_15, "(a)?b", "5/27(day) 12:34", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_15, "(a)?b", "before</d>after", false, sgre::script::NativeRegexpRange{0, 1}, std::array<sgre::script::NativeRegexpCapture, 2>{{{0, 1}, {std::nullopt, 0}}});
	CheckNativeCase(program_15, "(a)?b", "bbb", false, sgre::script::NativeRegexpRange{0, 1}, std::array<sgre::script::NativeRegexpCapture, 2>{{{0, 1}, {std::nullopt, 0}}});
	CheckNativeCase(program_15, "(a)?b", "7", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});

	sgre::script::NativeRegexp program_16("(a)+");
	Require(program_16.SubexpressionCount() == 2, "native regexp subexpression count differs");
	CheckNativeCase(program_16, "(a)+", "", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_16, "(a)+", "a", true, sgre::script::NativeRegexpRange{0, 1}, std::array<sgre::script::NativeRegexpCapture, 2>{{{0, 1}, {0, 1}}});
	CheckNativeCase(program_16, "(a)+", "ab", false, sgre::script::NativeRegexpRange{0, 1}, std::array<sgre::script::NativeRegexpCapture, 2>{{{0, 1}, {0, 1}}});
	CheckNativeCase(program_16, "(a)+", "abc", false, sgre::script::NativeRegexpRange{0, 1}, std::array<sgre::script::NativeRegexpCapture, 2>{{{0, 1}, {0, 1}}});
	CheckNativeCase(program_16, "(a)+", "aabc", false, sgre::script::NativeRegexpRange{0, 2}, std::array<sgre::script::NativeRegexpCapture, 2>{{{0, 2}, {0, 1}}});
	CheckNativeCase(program_16, "(a)+", "aaab", false, sgre::script::NativeRegexpRange{0, 3}, std::array<sgre::script::NativeRegexpCapture, 2>{{{0, 3}, {0, 1}}});
	CheckNativeCase(program_16, "(a)+", "aaaa", true, sgre::script::NativeRegexpRange{0, 4}, std::array<sgre::script::NativeRegexpCapture, 2>{{{0, 4}, {0, 1}}});
	CheckNativeCase(program_16, "(a)+", " b abc c ", false, sgre::script::NativeRegexpRange{3, 4}, std::array<sgre::script::NativeRegexpCapture, 2>{{{3, 1}, {3, 1}}});
	CheckNativeCase(program_16, "(a)+", "RM_", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_16, "(a)+", "RM_test", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_16, "(a)+", "1m2d3:4", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_16, "(a)+", "5/27(day) 12:34", false, sgre::script::NativeRegexpRange{6, 7}, std::array<sgre::script::NativeRegexpCapture, 2>{{{6, 1}, {6, 1}}});
	CheckNativeCase(program_16, "(a)+", "before</d>after", false, sgre::script::NativeRegexpRange{10, 11}, std::array<sgre::script::NativeRegexpCapture, 2>{{{10, 1}, {10, 1}}});
	CheckNativeCase(program_16, "(a)+", "bbb", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_16, "(a)+", "7", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});

	sgre::script::NativeRegexp program_17("(?:a)+");
	Require(program_17.SubexpressionCount() == 1, "native regexp subexpression count differs");
	CheckNativeCase(program_17, "(?:a)+", "", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_17, "(?:a)+", "a", true, sgre::script::NativeRegexpRange{0, 1}, std::array<sgre::script::NativeRegexpCapture, 1>{{{0, 1}}});
	CheckNativeCase(program_17, "(?:a)+", "ab", false, sgre::script::NativeRegexpRange{0, 1}, std::array<sgre::script::NativeRegexpCapture, 1>{{{0, 1}}});
	CheckNativeCase(program_17, "(?:a)+", "abc", false, sgre::script::NativeRegexpRange{0, 1}, std::array<sgre::script::NativeRegexpCapture, 1>{{{0, 1}}});
	CheckNativeCase(program_17, "(?:a)+", "aabc", false, sgre::script::NativeRegexpRange{0, 2}, std::array<sgre::script::NativeRegexpCapture, 1>{{{0, 2}}});
	CheckNativeCase(program_17, "(?:a)+", "aaab", false, sgre::script::NativeRegexpRange{0, 3}, std::array<sgre::script::NativeRegexpCapture, 1>{{{0, 3}}});
	CheckNativeCase(program_17, "(?:a)+", "aaaa", true, sgre::script::NativeRegexpRange{0, 4}, std::array<sgre::script::NativeRegexpCapture, 1>{{{0, 4}}});
	CheckNativeCase(program_17, "(?:a)+", " b abc c ", false, sgre::script::NativeRegexpRange{3, 4}, std::array<sgre::script::NativeRegexpCapture, 1>{{{3, 1}}});
	CheckNativeCase(program_17, "(?:a)+", "RM_", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_17, "(?:a)+", "RM_test", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_17, "(?:a)+", "1m2d3:4", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_17, "(?:a)+", "5/27(day) 12:34", false, sgre::script::NativeRegexpRange{6, 7}, std::array<sgre::script::NativeRegexpCapture, 1>{{{6, 1}}});
	CheckNativeCase(program_17, "(?:a)+", "before</d>after", false, sgre::script::NativeRegexpRange{10, 11}, std::array<sgre::script::NativeRegexpCapture, 1>{{{10, 1}}});
	CheckNativeCase(program_17, "(?:a)+", "bbb", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_17, "(?:a)+", "7", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});

	sgre::script::NativeRegexp program_18("a{2,3}");
	Require(program_18.SubexpressionCount() == 1, "native regexp subexpression count differs");
	CheckNativeCase(program_18, "a{2,3}", "", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_18, "a{2,3}", "a", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_18, "a{2,3}", "ab", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_18, "a{2,3}", "abc", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_18, "a{2,3}", "aabc", false, sgre::script::NativeRegexpRange{0, 2}, std::array<sgre::script::NativeRegexpCapture, 1>{{{0, 2}}});
	CheckNativeCase(program_18, "a{2,3}", "aaab", false, sgre::script::NativeRegexpRange{0, 3}, std::array<sgre::script::NativeRegexpCapture, 1>{{{0, 3}}});
	CheckNativeCase(program_18, "a{2,3}", "aaaa", false, sgre::script::NativeRegexpRange{0, 3}, std::array<sgre::script::NativeRegexpCapture, 1>{{{0, 3}}});
	CheckNativeCase(program_18, "a{2,3}", " b abc c ", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_18, "a{2,3}", "RM_", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_18, "a{2,3}", "RM_test", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_18, "a{2,3}", "1m2d3:4", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_18, "a{2,3}", "5/27(day) 12:34", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_18, "a{2,3}", "before</d>after", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_18, "a{2,3}", "bbb", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_18, "a{2,3}", "7", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});

	sgre::script::NativeRegexp program_19("a{3,2}");
	Require(program_19.SubexpressionCount() == 1, "native regexp subexpression count differs");
	CheckNativeCase(program_19, "a{3,2}", "", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_19, "a{3,2}", "a", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_19, "a{3,2}", "ab", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_19, "a{3,2}", "abc", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_19, "a{3,2}", "aabc", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_19, "a{3,2}", "aaab", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_19, "a{3,2}", "aaaa", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_19, "a{3,2}", " b abc c ", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_19, "a{3,2}", "RM_", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_19, "a{3,2}", "RM_test", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_19, "a{3,2}", "1m2d3:4", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_19, "a{3,2}", "5/27(day) 12:34", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_19, "a{3,2}", "before</d>after", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_19, "a{3,2}", "bbb", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_19, "a{3,2}", "7", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});

	RequireFailure([] {
		sgre::script::NativeRegexp program("()");
	},
	    "expected paren");

	RequireFailure([] {
		sgre::script::NativeRegexp program("a|");
	},
	    "letter expected");

	RequireFailure([] {
		sgre::script::NativeRegexp program("[");
	},
	    "letter expected");

	sgre::script::NativeRegexp program_23("\\");
	Require(program_23.SubexpressionCount() == 1, "native regexp subexpression count differs");
	CheckNativeCase(program_23, "\\", "", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_23, "\\", "a", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_23, "\\", "ab", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_23, "\\", "abc", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_23, "\\", "aabc", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_23, "\\", "aaab", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_23, "\\", "aaaa", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_23, "\\", " b abc c ", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_23, "\\", "RM_", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_23, "\\", "RM_test", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_23, "\\", "1m2d3:4", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_23, "\\", "5/27(day) 12:34", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_23, "\\", "before</d>after", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_23, "\\", "bbb", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_23, "\\", "7", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});

	sgre::script::NativeRegexp program_24("*");
	Require(program_24.SubexpressionCount() == 1, "native regexp subexpression count differs");
	CheckNativeCase(program_24, "*", "", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_24, "*", "a", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_24, "*", "ab", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_24, "*", "abc", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_24, "*", "aabc", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_24, "*", "aaab", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_24, "*", "aaaa", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_24, "*", " b abc c ", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_24, "*", "RM_", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_24, "*", "RM_test", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_24, "*", "1m2d3:4", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_24, "*", "5/27(day) 12:34", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_24, "*", "before</d>after", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_24, "*", "bbb", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});
	CheckNativeCase(program_24, "*", "7", false, std::nullopt, std::array<sgre::script::NativeRegexpCapture, 0>{{}});

	RequireFailure([] {
		sgre::script::NativeRegexp program("a**");
	},
	    "unexpected character");

}

static sgre::script::SquirrelValue Call(sgre::script::SquirrelVm& vm, const sgre::script::SquirrelValue& receiver, std::string_view method, std::span<const sgre::script::SquirrelValue> inputs = {}) {
	std::vector<sgre::script::SquirrelValue> arguments{receiver};
	arguments.insert(arguments.end(), inputs.begin(), inputs.end());

	return vm.Invoke(vm.GetSlot(receiver, std::string(method)), arguments).Value;
}

static sgre::script::SquirrelValue Construct(sgre::script::SquirrelVm& vm, const sgre::script::SquirrelValue& class_value, std::string pattern) {
	const auto instance = vm.CreateInstance(class_value);
	const std::array<sgre::script::SquirrelValue, 1> arguments{std::move(pattern)};
	Require(std::holds_alternative<std::monostate>(Call(vm, instance, "constructor", arguments)), "regexp constructor must return no values");

	return instance;
}

static void RequireRange(sgre::script::SquirrelVm& vm, const sgre::script::SquirrelValue& value, std::int32_t begin, std::int32_t end) {
	Require(vm.Type(value) == sgre::script::SquirrelValueType::Table && std::get<std::int32_t>(vm.GetSlot(value, std::string("begin"))) == begin && std::get<std::int32_t>(vm.GetSlot(value, std::string("end"))) == end, "native regexp range table differs");
}

static sgre::script::SquirrelValue ScriptTypeOf(sgre::script::SquirrelVm& vm, const sgre::script::SquirrelValue& value) {
	sgre::script::BytecodeProgram program;
	program.MainFunction.Name = std::string("regexp actual typeof fixture");
	program.MainFunction.Parameters = {std::string("this"), std::string("value")};
	program.MainFunction.StackSize = 3;
	sgre::script::SquirrelInstruction query;
	query.Opcode = 55;
	query.Argument0 = 2;
	query.Argument1 = 1;
	sgre::script::SquirrelInstruction returned;
	returned.Opcode = 19;
	returned.Argument0 = 1;
	returned.Argument1 = 2;
	program.MainFunction.Instructions = {query, returned};
	const auto closure = vm.CreateClosure(std::move(program));
	const std::array<sgre::script::SquirrelValue, 2> arguments{vm.RootTable(), value};

	return vm.Invoke(closure, arguments).Value;
}

static void TestActualVmInstances() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	sgre::script::SquirrelVm vm(limits);
	const auto class_value = sgre::script::InstallRecoveredRegexpLibrary(vm, {}, 2);
	Require(vm.Type(class_value) == sgre::script::SquirrelValueType::Class && vm.GetSlot(vm.RootTable(), std::string("regexp")) == class_value, "regexp must register its genuine root class");
	auto first = Construct(vm, class_value, "(a)+");
	const std::array<sgre::script::SquirrelValue, 1> text{std::string("aaaa")};
	Require(std::get<bool>(Call(vm, first, "match", text)), "regexp real VM match failed");
	Require(std::get<std::int32_t>(Call(vm, first, "subexpcount")) == 2 && sgre::script::StringText(ScriptTypeOf(vm, first)) == "regexp", "regexp real payload count/type differs");
	RequireRange(vm, Call(vm, first, "search", text), 0, 4);
	const auto captures = Call(vm, first, "capture", text);
	Require(vm.Length(captures) == 2, "regexp capture must include group zero");
	RequireRange(vm, vm.GetSlot(captures, std::int32_t{0}), 0, 4);
	RequireRange(vm, vm.GetSlot(captures, std::int32_t{1}), 0, 1);
	const std::array<sgre::script::SquirrelValue, 1> no_match{std::string("bbb")};
	Require(std::holds_alternative<std::monostate>(Call(vm, first, "search", no_match)) && std::holds_alternative<std::monostate>(Call(vm, first, "capture", no_match)), "regexp failed search/capture must return no values");
	const std::array<sgre::script::SquirrelValue, 2> offset{std::string("baaa"), 2.75f};
	RequireRange(vm, Call(vm, first, "search", offset), 2, 4);
	const std::array<sgre::script::SquirrelValue, 2> invalid_offset{std::string("aaaa"), true};
	RequireFailure([&] {
		static_cast<void>(Call(vm, first, "search", invalid_offset));
	}, "parameter");
	const std::array<sgre::script::SquirrelValue, 2> negative{std::string("aaaa"), std::int32_t{-1}};
	RequireFailure([&] {
		static_cast<void>(Call(vm, first, "search", negative));
	}, "precedes");
	const std::array<sgre::script::SquirrelValue, 2> outside{std::string("aaaa"), std::int32_t{5}};
	RequireFailure([&] {
		static_cast<void>(Call(vm, first, "search", outside));
	}, "exceeds");
	const std::array<sgre::script::SquirrelValue, 1> duplicate_pattern{std::string("a")};
	RequireFailure([&] {
		static_cast<void>(Call(vm, first, "constructor", duplicate_pattern));
	}, "already initialized");
	RequireFailure([&] {
		static_cast<void>(Construct(vm, class_value, "["));
	}, "letter expected");
	const auto unconstructed = vm.CreateInstance(class_value);
	RequireFailure([&] {
		static_cast<void>(Call(vm, unconstructed, "subexpcount"));
	}, "invalid regexp instance");
	auto second = Construct(vm, class_value, "a*");
	const std::array<sgre::script::SquirrelValue, 1> b_text{std::string("b")};
	const auto empty_capture = Call(vm, second, "capture", b_text);
	RequireRange(vm, vm.GetSlot(empty_capture, std::int32_t{0}), 0, 0);
	RequireFailure([&] {
		static_cast<void>(Construct(vm, class_value, "b"));
	}, "instance limit");
	const auto weak = vm.CreateWeakReference(first);
	first = {};
	static_cast<void>(vm.CollectGarbage());
	vm.RethrowReleaseFailure();
	Require(std::holds_alternative<std::monostate>(vm.ResolveWeakReference(weak)), "regexp payload registry must not root its instance");
	const auto replacement = Construct(vm, class_value, "b");
	Require(std::get<bool>(Call(vm, replacement, "match", b_text)), "collected regexp payload must release genuine capacity");
	second = {};
	vm.Close();
	Require(vm.IsClosed(), "regexp class/payload hooks must permit complete actual VM close");
}

static void TestBoundedSafety() {
	sgre::script::NativeRegexpLimits limits;
	limits.MaxPatternBytes = 3;
	RequireFailure([&] {
		sgre::script::NativeRegexp program("abcd", limits);
	}, "pattern byte");
	limits = {};
	limits.MaxNodes = 2;
	RequireFailure([&] {
		sgre::script::NativeRegexp program("ab", limits);
	}, "node limit");
	limits = {};
	limits.MaxDepth = 3;
	RequireFailure([&] {
		sgre::script::NativeRegexp program("((((a))))", limits);
	}, "depth");
	limits = {};
	limits.MaxMatchSteps = 3;
	sgre::script::NativeRegexp program("a+", limits);
	RequireFailure([&] {
		static_cast<void>(program.Search("aaaa"));
	}, "work");
	limits = {};
	limits.MaxTextBytes = 2;
	sgre::script::NativeRegexp small("a", limits);
	RequireFailure([&] {
		static_cast<void>(small.Match("aaa"));
	}, "input byte");
	sgre::script::NativeRegexp c_string(std::string("a\0b", 3));
	Require(c_string.Match(std::string("a\0z", 3)), "regexp must use the native C-string prefix");
}

int main() {
	try {
		TestNativeVectors();
		TestActualVmInstances();
		TestBoundedSafety();
		std::cout << "native regexp 26-pattern match/search/capture vectors, real VM payloads and bounded lifetime PASS\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
