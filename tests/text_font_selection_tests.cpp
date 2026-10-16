#include "sgre/text/font_selection.hpp"

#include <bit>
#include <cstdint>
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

static void RequireFailure(const std::function<void()>& operation, std::string_view context) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("text font selection expected failure");
}

static sgre::text::TextFontSelectionEntry Entry(float size, std::uint32_t face, sgre::text::TextFontStyle style, bool ignore_size = false) {
	sgre::text::TextFontSelectionEntry result;
	result.Key.RequestedSize = size;
	result.Key.FaceIndex = face;
	result.Key.Style = style;
	result.IgnoreSize = ignore_size;

	return result;
}

static void TestNormalCurrentPrecedenceAndRegularWildcard() {
	sgre::text::TextFontSelectionCache cache(sgre::text::TextFontFamily::Normal);
	const auto regular = Entry(24, 7, sgre::text::TextFontStyle::Regular);
	const auto bold = Entry(24, 7, sgre::text::TextFontStyle::Bold);
	const auto italic = Entry(24, 7, sgre::text::TextFontStyle::Italic);
	Require(cache.Append(regular) == 0 && cache.Append(bold) == 1 && cache.Append(italic) == 2, "native normal entries must retain construction order");
	Require(cache.Find(regular.Key) == 2, "normal regular-style request must preserve current nonzero-style wildcard match");
	Require(cache.Find(bold.Key) == 1 && cache.Find(regular.Key) == 1, "native current pointer must precede earlier insertion-order matches");
	cache.ResolveFailed();
	Require(cache.Find(regular.Key) == 0, "without a current entry, selection must use the first compatible retained font");
	const auto miss = Entry(12, 8, sgre::text::TextFontStyle::Regular);
	Require(!cache.Find(miss.Key) && cache.Current() == 0 && cache.Entries().size() == 3, "a lookup miss must preserve existing current ownership until findFont returns");
	cache.ResolveFailed();
	Require(!cache.Current() && cache.Entries().size() == 3, "failed findFont must clear only the current selection");
}

static void TestRubyIndependenceAndOrderedFloatComparison() {
	sgre::text::TextFontSelectionCache normal(sgre::text::TextFontFamily::Normal);
	sgre::text::TextFontSelectionCache ruby(sgre::text::TextFontFamily::Ruby);
	const auto first = Entry(12, 4, sgre::text::TextFontStyle::Bold);
	Require(ruby.Append(first) == 0 && ruby.Entries()[0].Key.Style == sgre::text::TextFontStyle::Regular && !normal.Current(), "ruby entries must store stylekey zero without merging normal cache identity");
	const auto italic = Entry(12, 4, sgre::text::TextFontStyle::Italic);
	Require(ruby.Find(italic.Key) == 0, "ruby matching must omit normal style identity");
	const auto zero = Entry(0, 8, sgre::text::TextFontStyle::Regular);
	Require(normal.Append(zero) == 0, "zero fixture entry identity differs");
	const auto negative_zero = Entry(std::bit_cast<float>(std::uint32_t{0x80000000}), 8, sgre::text::TextFontStyle::Regular);
	Require(normal.Find(negative_zero.Key) == 0, "native ordered comparison must accept positive/negative-zero sizes");
	const auto nan = Entry(std::bit_cast<float>(std::uint32_t{0x7fc00001}), 9, sgre::text::TextFontStyle::Regular);
	Require(normal.Append(nan) == 1 && !normal.Find(nan.Key), "a NaN size must never match itself under native ordered comparison");
	const auto ignoring = Entry(1, 9, sgre::text::TextFontStyle::Bold, true);
	Require(normal.Append(ignoring) == 2 && normal.Find(nan.Key) == 2, "native ignore-size branch must bypass even unordered size comparisons while preserving regular style wildcard");
	const auto wrong_face = Entry(1, 7, sgre::text::TextFontStyle::Regular);
	Require(!normal.Find(wrong_face.Key), "ignore-size must never erase face identity");
}

static void TestCapacityAndClear() {
	RequireFailure([] {
		static_cast<void>(sgre::text::TextFontSelectionCache(sgre::text::TextFontFamily::Normal, 0));
	},
	    "bounded positive");
	sgre::text::TextFontSelectionCache cache(sgre::text::TextFontFamily::Normal, 1);
	const auto entry = Entry(24, 1, sgre::text::TextFontStyle::Regular);
	Require(cache.Append(entry) == 0, "bounded cache failed its first entry");
	RequireFailure([&] {
		static_cast<void>(cache.Append(entry));
	},
	    "limit");
	Require(cache.Current() == 0 && cache.Entries().size() == 1, "failed cache append must preserve prior entries/current state");
	cache.Clear();
	Require(!cache.Current() && cache.Entries().empty() && cache.Append(entry) == 0, "clear must retire selector identities and reset reusable capacity");
}

int main() {
	try {
		TestNormalCurrentPrecedenceAndRegularWildcard();
		TestRubyIndependenceAndOrderedFloatComparison();
		TestCapacityAndClear();
		std::cout << "Text font normal/ruby cache identity, ordered matching and callback phases passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
