#include "sgre/text/unicode_decomposition.hpp"

#include <charconv>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static void RequireDecomposition(std::uint32_t codepoint, std::uint32_t first, std::uint32_t second) {
	const auto result = sgre::text::DecomposeUnicodeCodepoint(codepoint);
	Require(result.has_value(), "native canonical decomposition unexpectedly absent");
	Require(result->First == first && result->Second == second, "native canonical decomposition outputs differ");
}

static void TestCanonicalPairsAndOneStepBoundary() {
	RequireDecomposition(0xc0, 0x41, 0x300);
	RequireDecomposition(0xc5, 0x41, 0x30a);
	RequireDecomposition(0xe9, 0x65, 0x301);
	RequireDecomposition(0x1fa, 0xc5, 0x301);
	RequireDecomposition(0x212b, 0xc5, 0);
	RequireDecomposition(0x304c, 0x304b, 0x3099);
	RequireDecomposition(0x30ac, 0x30ab, 0x3099);
	RequireDecomposition(0xf900, 0x8c48, 0);
	RequireDecomposition(0x2f81d, 0x51f5, 0);
}

static void TestHangulBoundaries() {
	RequireDecomposition(0xac00, 0x1100, 0x1161);
	RequireDecomposition(0xac01, 0xac00, 0x11a8);
	RequireDecomposition(0xac1b, 0xac00, 0x11c2);
	RequireDecomposition(0xac1c, 0x1100, 0x1162);
	RequireDecomposition(0xac1d, 0xac1c, 0x11a8);
	RequireDecomposition(0xae4c, 0x1101, 0x1161);
	RequireDecomposition(0xd788, 0x1112, 0x1175);
	RequireDecomposition(0xd7a3, 0xd788, 0x11c2);
	Require(!sgre::text::DecomposeUnicodeCodepoint(0xabff).has_value(), "codepoint before Hangul range was decomposed");
	Require(!sgre::text::DecomposeUnicodeCodepoint(0xd7a4).has_value(), "codepoint after Hangul range was decomposed");
}

static void TestSupplementaryOutputs() {
	RequireDecomposition(0x1d15e, 0x1d157, 0x1d165);
	RequireDecomposition(0x2fa1d, 0x2a600, 0);
	const auto singleton = sgre::text::DecomposeUnicodeCodepoint(0x2fa1d);
	Require(singleton.has_value() && singleton->Second == 0, "singleton supplementary output acquired a second scalar");
}

static void TestAbsentCompatibilityAndUnsignedInputs() {
	for (const auto codepoint : {0U, 0x41U, 0xa0U, 0x300U, 0x1100U, 0x1161U, 0x11a8U, 0xd800U, 0xdfffU, 0xfb01U, 0xff21U, 0x10ffffU, 0x110000U, 0x1100c0U, 0x800000c0U, 0x8000ac00U, 0xffffac00U, 0xffffffffU}) {
		Require(!sgre::text::DecomposeUnicodeCodepoint(codepoint).has_value(), "absent canonical input was decomposed");
	}
}

static std::uint32_t ReadNumber(std::string_view text) {
	std::uint32_t value = 0;
	const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
	Require(parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size(), "invalid decimal canonical decomposition range");

	return value;
}

static void WriteWord(std::uint32_t value) {
	for (std::uint32_t shift = 0; shift < 32; shift += 8) {
		std::cout.put(static_cast<char>((value >> shift) & 0xffU));
	}
}

static void WriteDecompositions(std::uint32_t first, std::uint32_t count) {
	for (std::uint32_t offset = 0; offset < count; ++offset) {
		const auto result = sgre::text::DecomposeUnicodeCodepoint(first + offset);
		WriteWord(result.has_value() ? 1U : 0U);
		// Match the native caller's untouched output sentinels when no decomposition exists.
		WriteWord(result.has_value() ? result->First : 0xabcd1234U);
		WriteWord(result.has_value() ? result->Second : 0xef987654U);
	}
}

int main(int argc, char** argv) {
	try {
		if (argc == 2 && std::string_view(argv[1]) == "--all") {
			WriteDecompositions(0, 0x110000);

			return 0;
		}
		if (argc == 4 && std::string_view(argv[1]) == "--range") {
			const auto first = ReadNumber(argv[2]);
			const auto count = ReadNumber(argv[3]);
			Require(count <= 4096 && (count == 0 || count - 1 <= 0xffffffff - first), "canonical decomposition range exceeds its output limit or wraps");
			WriteDecompositions(first, count);

			return 0;
		}

		Require(argc == 1, "unsupported canonical decomposition fixture arguments");
		TestCanonicalPairsAndOneStepBoundary();
		TestHangulBoundaries();
		TestSupplementaryOutputs();
		TestAbsentCompatibilityAndUnsignedInputs();
		std::cout << "Unicode canonical decomposition: four groups passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
