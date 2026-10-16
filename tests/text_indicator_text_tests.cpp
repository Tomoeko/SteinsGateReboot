#include "sgre/text/indicator_text.hpp"

#include <array>
#include <cstdint>
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

static std::vector<std::uint16_t> Units(std::u16string_view text) {
	std::vector<std::uint16_t> units;
	for (const auto unit : text) {
		units.push_back(static_cast<std::uint16_t>(unit));
	}

	return units;
}

static void TestOwnedLiteralClusters() {
	auto units = Units(u"A\U0001f600\u3099\u309a\u0e01\u0e31\u0e4f\r\n");
	const auto elements = sgre::text::PrepareIndicatorText(units, 0x10203040u);
	units.clear();
	const std::array<std::vector<std::uint16_t>, 7> expected{{{65}, {0xd83d, 0xde00, 0x3099}, {0x309a}, {0x0e01, 0x0e31}, {0x0e4f}, {13}, {10}}};
	Require(elements.size() == expected.size(), "native text clusters must consume one surrogate successor and at most one selected combining unit");
	for (std::size_t index = 0; index < expected.size(); ++index) {
		Require(elements[index].CodeUnits == expected[index] && elements[index].Color == 0x10203040u && elements[index].Scale == 1, "literal elements must own their exact UTF-16 cluster and initial font style");
	}

	Require(!elements[5].IsLineBreak() && elements[6].IsLineBreak(), "only a single LF unit is a native Indicator line separator");
}

static void TestNativeColorScaleAndClearing() {
	const auto elements = sgre::text::PrepareIndicatorText(Units(u"#{color,ff112233}A#{scale,0.5}B#{color,clear}C#{scale,nonsense}D#{scale,+1.5junk}E"), 0x12345678u, true);
	Require(elements.size() == 5, "native color/scale directives must change element style without creating glyphs");
	Require(elements[0].Color == 0xff112233u && elements[0].Scale == 1, "hexadecimal color must retain all unsigned 32 bits");
	Require(elements[1].Color == 0xff112233u && elements[1].Scale == 0.5f, "scale affects following elements while preserving color");
	Require(elements[2].Color == 0x12345678u && elements[2].Scale == 0.5f, "clear restores the incoming font color without resetting scale");
	Require(elements[3].Scale == 0 && elements[4].Scale == 1.5f, "native decimal conversion accepts a numeric prefix and yields zero when no digits exist");
	const auto overflow = sgre::text::PrepareIndicatorText(Units(u"#{color,-1}A#{color,100000000}B#{color,0x1234}C#{color,xyz}D"), 0, true);
	Require(overflow[0].Color == 0xffffffffu && overflow[1].Color == 0xffffffffu && overflow[2].Color == 0x1234u && overflow[3].Color == 0, "native unsigned color conversion must distinguish negation, overflow, hexadecimal prefix and missing digits");
}

static void TestPersistentHashStateAndIncompleteDirectives() {
	const auto elements = sgre::text::PrepareIndicatorText(Units(u"#xカ\u3099{unknown,a}カ\u3099#{unknown,unfinished"), 0xaabbccddu, true);
	Require(elements.size() == 4 && elements[0].CodeUnits == Units(u"x") && elements[1].CodeUnits == Units(u"カ") && elements[2].CodeUnits == Units(u"\u3099") && elements[3].CodeUnits == Units(u"カ\u3099"), "after-hash literals must keep native state 1 until a brace, then resume ordinary clustering");
	const auto hashes = sgre::text::PrepareIndicatorText(Units(u"###"), 0, true);
	Require(hashes.size() == 2 && hashes[0].CodeUnits == Units(u"#") && hashes[1].CodeUnits == Units(u"#"), "persistent hash state emits subsequent hashes individually");
	const auto literal = sgre::text::PrepareIndicatorText(Units(u"#{scale,2}A"));
	Require(literal.size() == 11 && literal.front().CodeUnits == Units(u"#") && literal.back().Scale == 1, "tag recognition must be disabled by the native font default");
}

static void TestFixedLineMeasurement() {
	const std::array<sgre::text::IndicatorMeasuredElement, 6> elements{{{{2, 3}, 0.5f, false}, {{4, 2}, 2, false}, {{}, 1, true}, {{}, 1, true}, {{5, 6}, 1, false}, {{}, 1, true}}};
	const auto measured = sgre::text::MeasureIndicatorText(elements, {1, 2});
	Require(measured.Width == 10 && measured.Height == 16, "fixed Indicator bounds must use maximum line width, scaled line heights and unscaled interelement/interline spacing");
	const auto empty = sgre::text::MeasureIndicatorText({}, {10, 20});
	Require(empty.Width == 0 && empty.Height == 0, "empty text must not create an implicit line extent");
	const std::array<sgre::text::IndicatorMeasuredElement, 2> negative{{{{2, 3}, -1, false}, {{4, 5}, -1, false}}};
	const auto negative_measured = sgre::text::MeasureIndicatorText(negative, {10, 20});
	Require(negative_measured.Width == 4 && negative_measured.Height == 0, "native line maxima begin at zero and spacing is added after summing scaled widths");
	sgre::text::IndicatorMeasuredElement line_break;
	line_break.LineBreak = true;
	line_break.Scale = std::numeric_limits<float>::infinity();
	line_break.Metrics.Width = std::numeric_limits<float>::quiet_NaN();
	const auto separator = sgre::text::MeasureIndicatorText(std::span<const sgre::text::IndicatorMeasuredElement>(&line_break, 1), {0, 3});
	Require(separator.Width == 0 && separator.Height == 3, "line separators must bypass glyph metric access and retain their spacing contribution");
}

static void TestIncrementalMeasurement() {
	sgre::text::IndicatorTextMeasurement measurement({1, 2});
	measurement.Append({{2, 3}, 0.5f, false});
	measurement.Append({{4, 2}, 2, false});
	const auto first = measurement.Finish();
	const auto repeated = measurement.Finish();
	Require(first.Width == 10 && first.Height == 4 && repeated.Width == first.Width && repeated.Height == first.Height, "incremental measurement snapshots must be repeatable without ending the current line");
	measurement.Append({{}, 1, true});
	Require(measurement.Finish().Width == 10 && measurement.Finish().Height == 6, "incremental line completion must retain a trailing separator's spacing");
	measurement.Append({{5, 6}, 1, false});
	bool rejected = false;
	try {
		measurement.Append({{2, 3}, std::numeric_limits<float>::infinity(), false});
	} catch (const std::runtime_error&) {
		rejected = true;
	}

	Require(rejected && measurement.Finish().Width == 10 && measurement.Finish().Height == 12, "invalid appended metrics must preserve the preceding incremental measurement");
}

static void TestBoundedMalformedInput() {
	bool terminal_surrogate_rejected = false;
	try {
		static_cast<void>(sgre::text::PrepareIndicatorText(std::array<std::uint16_t, 1>{0xd800}));
	} catch (const std::runtime_error&) {
		terminal_surrogate_rejected = true;
	}

	bool missing_value_rejected = false;
	try {
		static_cast<void>(sgre::text::PrepareIndicatorText(Units(u"#{color}A"), 0, true));
	} catch (const std::runtime_error&) {
		missing_value_rejected = true;
	}

	bool nonfinite_rejected = false;
	try {
		static_cast<void>(sgre::text::PrepareIndicatorText(Units(u"#{scale,inf}A"), 0, true));
	} catch (const std::runtime_error&) {
		nonfinite_rejected = true;
	}

	Require(terminal_surrogate_rejected && missing_value_rejected && nonfinite_rejected, "unsafe terminal clusters, missing native parameters and nonfinite layout must fail explicitly");
	const std::vector<std::uint16_t> excessive(65537, 65);
	bool budget_rejected = false;
	try {
		static_cast<void>(sgre::text::PrepareIndicatorText(excessive));
	} catch (const std::runtime_error&) {
		budget_rejected = true;
	}

	Require(budget_rejected, "Indicator text construction must enforce its bounded input budget");
}

int main() {
	try {
		TestOwnedLiteralClusters();
		TestNativeColorScaleAndClearing();
		TestPersistentHashStateAndIncompleteDirectives();
		TestFixedLineMeasurement();
		TestIncrementalMeasurement();
		TestBoundedMalformedInput();
		std::cout << "Indicator element construction and fixed line measurement fixtures passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
