#include "sgre/text/unicode_composition.hpp"

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

static void RequireComposition(std::uint32_t first, std::uint32_t second, sgre::text::UnicodeCompositionStatus status, std::uint32_t codepoint = 0) {
	const auto result = sgre::text::ComposeUnicodeCodepoints(first, second);
	Require(result.Status == status && result.Codepoint == codepoint, "native Unicode composition result differs");
}

static void TestActualTablePairs() {
	const auto composed = sgre::text::UnicodeCompositionStatus::Composed;
	RequireComposition(0x41, 0x300, composed, 0xc0);
	RequireComposition(0x41, 0x301, composed, 0xc1);
	RequireComposition(0x41, 0x30a, composed, 0xc5);
	RequireComposition(0xc5, 0x301, composed, 0x1fa);
	RequireComposition(0x65, 0x301, composed, 0xe9);
	RequireComposition(0x3c, 0x338, composed, 0x226e);
	RequireComposition(0x304b, 0x3099, composed, 0x304c);
	RequireComposition(0x30ab, 0x3099, composed, 0x30ac);
	RequireComposition(0x11347, 0x1133e, composed, 0x1134b);
	RequireComposition(0x115b9, 0x115af, composed, 0x115bb);
}

static void TestNativeHangulGates() {
	const auto composed = sgre::text::UnicodeCompositionStatus::Composed;
	RequireComposition(0x1100, 0x1161, composed, 0xac00);
	RequireComposition(0x1101, 0x1161, composed, 0xae4c);
	RequireComposition(0x1112, 0x1175, composed, 0xd788);
	RequireComposition(0xac00, 0x11a8, composed, 0xac01);
	RequireComposition(0xd788, 0x11c2, composed, 0xd7a3);
	RequireComposition(0xac00, 0x11a7, composed, 0xac00);
	RequireComposition(0xac01, 0x11a8, composed, 0xac02);
	RequireComposition(0xac1b, 0x11c2, composed, 0xac36);
	RequireComposition(0xd7a3, 0x11c2, composed, 0xd7be);

	const auto unmapped = sgre::text::UnicodeCompositionStatus::UnmappedInput;
	RequireComposition(0x10ff, 0x1161, unmapped);
	RequireComposition(0x1113, 0x1161, unmapped);
	RequireComposition(0x1100, 0x1160, unmapped);
	RequireComposition(0x1100, 0x1176, unmapped);
	RequireComposition(0xabff, 0x11a8, unmapped);
	RequireComposition(0xd7a4, 0x11a8, unmapped);
	RequireComposition(0xac00, 0x11a6, unmapped);
	RequireComposition(0xac00, 0x11c3, unmapped);
}

static void TestBothFailureOutputsAndExclusions() {
	const auto missing = sgre::text::UnicodeCompositionStatus::MissingPair;
	RequireComposition(0x41, 0x3099, missing);
	RequireComposition(0x304b, 0x301, missing);
	const auto unmapped = sgre::text::UnicodeCompositionStatus::UnmappedInput;
	RequireComposition(0, 0x300, unmapped);
	RequireComposition(0x41, 0, unmapped);
	RequireComposition(0x301, 0x41, unmapped);
	RequireComposition(0x915, 0x93c, unmapped);
	RequireComposition(0xf71, 0xf72, unmapped);
	RequireComposition(0x1d157, 0x1d165, unmapped);
}

static void TestFullWidthUnsignedInputs() {
	const auto unmapped = sgre::text::UnicodeCompositionStatus::UnmappedInput;
	RequireComposition(0x10041, 0x301, unmapped);
	RequireComposition(0x41, 0x10301, unmapped);
	RequireComposition(0x1304b, 0x3099, unmapped);
	RequireComposition(0x304b, 0x13099, unmapped);
	RequireComposition(0x110041, 0x301, unmapped);
	RequireComposition(0x80000041, 0x301, unmapped);
	RequireComposition(0x41, 0x80000301, unmapped);
	RequireComposition(0x8000ac00, 0x11a8, unmapped);
	RequireComposition(0xac00, 0x800011a8, unmapped);
	RequireComposition(0xffffffff, 0x301, unmapped);
	RequireComposition(0x41, 0xffffffff, unmapped);
}

static std::uint32_t ReadNumber(std::string_view text) {
	std::uint32_t value = 0;
	const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
	Require(parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size(), "invalid decimal Unicode composition count");

	return value;
}

static std::uint32_t ReadWord() {
	std::uint32_t value = 0;
	for (std::uint32_t shift = 0; shift < 32; shift += 8) {
		const auto byte = std::cin.get();
		Require(byte != std::char_traits<char>::eof(), "truncated Unicode composition input pair");
		value |= static_cast<std::uint32_t>(byte) << shift;
	}

	return value;
}

static void WriteWord(std::uint32_t value) {
	for (std::uint32_t shift = 0; shift < 32; shift += 8) {
		std::cout.put(static_cast<char>((value >> shift) & 0xffU));
	}
}

static void WriteCompositionPairs(std::uint32_t count) {
	Require(count <= 3000000, "Unicode composition pair count exceeds its proof limit");
	for (std::uint32_t index = 0; index < count; ++index) {
		const auto first = ReadWord();
		const auto second = ReadWord();
		const auto result = sgre::text::ComposeUnicodeCodepoints(first, second);
		WriteWord(result.Status == sgre::text::UnicodeCompositionStatus::Composed ? 1U : 0U);
		WriteWord(result.Status == sgre::text::UnicodeCompositionStatus::UnmappedInput ? 0xabcd1234U : result.Codepoint);
		if ((index + 1) % 256 == 0) {
			std::cout.flush();
		}
	}
	std::cout.flush();
	Require(std::cin.peek() == std::char_traits<char>::eof(), "trailing Unicode composition input bytes");
}

int main(int argc, char** argv) {
	try {
		if (argc == 3 && std::string_view(argv[1]) == "--pairs") {
			WriteCompositionPairs(ReadNumber(argv[2]));

			return 0;
		}

		Require(argc == 1, "unsupported Unicode composition fixture arguments");
		TestActualTablePairs();
		TestNativeHangulGates();
		TestBothFailureOutputsAndExclusions();
		TestFullWidthUnsignedInputs();
		std::cout << "Unicode composition: four groups passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
