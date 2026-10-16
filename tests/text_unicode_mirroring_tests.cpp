#include "sgre/text/unicode_mirroring.hpp"

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

static void RequireMirror(std::uint32_t codepoint, std::uint32_t mirrored) {
	Require(sgre::text::MirrorUnicodeCodepoint(codepoint) == mirrored, "native Unicode mirroring result differs");
}

static void TestPunctuationAndMathPairs() {
	RequireMirror(0x28, 0x29);
	RequireMirror(0x29, 0x28);
	RequireMirror(0x3c, 0x3e);
	RequireMirror(0x3e, 0x3c);
	RequireMirror(0x5b, 0x5d);
	RequireMirror(0x5d, 0x5b);
	RequireMirror(0x7b, 0x7d);
	RequireMirror(0x7d, 0x7b);
	RequireMirror(0xab, 0xbb);
	RequireMirror(0xbb, 0xab);
	RequireMirror(0x2215, 0x29f5);
	RequireMirror(0x29f5, 0x2215);
	RequireMirror(0x2252, 0x2253);
	RequireMirror(0x2253, 0x2252);
	RequireMirror(0x2298, 0x29b8);
	RequireMirror(0x29b8, 0x2298);
	RequireMirror(0x22a6, 0x2ade);
	RequireMirror(0x2ade, 0x22a6);
}

static void TestOtherOriginalPairs() {
	RequireMirror(0x0f3a, 0x0f3b);
	RequireMirror(0x0f3b, 0x0f3a);
	RequireMirror(0x169b, 0x169c);
	RequireMirror(0x169c, 0x169b);
	RequireMirror(0x3008, 0x3009);
	RequireMirror(0x3009, 0x3008);
	RequireMirror(0x300c, 0x300d);
	RequireMirror(0x300d, 0x300c);
	RequireMirror(0xfe59, 0xfe5a);
	RequireMirror(0xfe5a, 0xfe59);
	RequireMirror(0xff62, 0xff63);
	RequireMirror(0xff63, 0xff62);
}

static void TestUnmappedInputsAndBoundaries() {
	for (const auto codepoint : {0U, 0x20U, 0x27U, 0x2aU, 0x41U, 0x3042U, 0xd800U, 0xff61U, 0xff64U, 0x1f600U, 0x10ffffU, 0x110000U, 0x80000041U, 0xffffffffU}) {
		RequireMirror(codepoint, codepoint);
	}
}

static void TestNativeLowWordAliasing() {
	RequireMirror(0x10028, 0x29);
	RequireMirror(0x10029, 0x28);
	RequireMirror(0x12215, 0x29f5);
	RequireMirror(0x1ff62, 0xff63);
	RequireMirror(0x10ff62, 0xff63);
	RequireMirror(0x110028, 0x29);
	RequireMirror(0x80000028, 0x29);
	RequireMirror(0xffff0028, 0x29);
	Require(sgre::text::MirrorUnicodeCodepoint(sgre::text::MirrorUnicodeCodepoint(0x10028)) == 0x28, "native supplementary alias was made artificially reversible");
}

static std::uint32_t ReadNumber(std::string_view text) {
	std::uint32_t value = 0;
	const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
	Require(parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size(), "invalid decimal Unicode mirroring range");

	return value;
}

static void WriteMirrors(std::uint32_t first, std::uint32_t count) {
	for (std::uint32_t offset = 0; offset < count; ++offset) {
		const auto value = sgre::text::MirrorUnicodeCodepoint(first + offset);
		for (std::uint32_t shift = 0; shift < 32; shift += 8) {
			std::cout.put(static_cast<char>((value >> shift) & 0xffU));
		}
	}
}

int main(int argc, char** argv) {
	try {
		if (argc == 2 && std::string_view(argv[1]) == "--all") {
			WriteMirrors(0, 0x110000);

			return 0;
		}
		if (argc == 4 && std::string_view(argv[1]) == "--range") {
			const auto first = ReadNumber(argv[2]);
			const auto count = ReadNumber(argv[3]);
			Require(count <= 4096 && (count == 0 || count - 1 <= 0xffffffff - first), "Unicode mirroring range exceeds its output limit or wraps");
			WriteMirrors(first, count);

			return 0;
		}

		Require(argc == 1, "unsupported Unicode mirroring fixture arguments");
		TestPunctuationAndMathPairs();
		TestOtherOriginalPairs();
		TestUnmappedInputsAndBoundaries();
		TestNativeLowWordAliasing();
		std::cout << "Unicode mirroring: four groups passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
