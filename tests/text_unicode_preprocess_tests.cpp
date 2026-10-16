#include "sgre/text/unicode_preprocess.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static sgre::text::SubstitutionGlyph MakeRecord(std::uint32_t codepoint) {
	sgre::text::SubstitutionGlyph glyph;
	glyph.GlyphIndex = codepoint;
	glyph.Mask = 0x89abcdef;
	glyph.Cluster = 0xfedcba98;
	glyph.GlyphProperties = 0x7654;
	glyph.PrivateState = 0x3210;
	glyph.CharacterState = 0xabcdffff;

	return glyph;
}

static void RequireProperties(std::uint32_t codepoint, std::uint16_t properties, std::uint32_t flags) {
	std::array<sgre::text::SubstitutionGlyph, 1> glyphs{MakeRecord(codepoint)};
	auto expected = glyphs.front();
	expected.CharacterState = 0xabcd0000U | properties;
	const auto result = sgre::text::InitializeUnicodeGlyphProperties(glyphs, static_cast<sgre::text::UnicodeBufferScratchFlags>(0x80000000));
	Require(static_cast<std::uint32_t>(result) == (0x80000000U | flags), "Unicode preprocessing scratch bits differ");
	Require(glyphs.front() == expected, "Unicode preprocessing property word or retained record fields differ");
}

static void TestAsciiAndRecordRetention() {
	RequireProperties(0, 0, 0);
	RequireProperties(0x20, 29, 0);
	RequireProperties(0x24, 23, 0);
	RequireProperties(0x41, 9, 0);
	RequireProperties(0x61, 5, 0);
	RequireProperties(0x7f, 0, 0);
	RequireProperties(0x80, 0, 1);
	RequireProperties(0x3042, 7, 1);
	RequireProperties(0xd800, 4, 1);
	RequireProperties(0xe000, 3, 1);
	RequireProperties(0x110000, 2, 1);
	RequireProperties(0xffffffff, 2, 1);
}

static void TestNativeIgnorablePredicates() {
	RequireProperties(0x00ad, 0x21, 3);
	RequireProperties(0x034f, 0x6c, 3);
	RequireProperties(0x061c, 0x21, 3);
	RequireProperties(0x17b4, 0x2c, 3);
	RequireProperties(0x17b5, 0x2c, 3);
	RequireProperties(0x180b, 0x6c, 3);
	RequireProperties(0x180d, 0x6c, 3);
	RequireProperties(0x180e, 0x21, 3);
	RequireProperties(0x200b, 0x21, 3);
	RequireProperties(0x200c, 0x221, 3);
	RequireProperties(0x200d, 0x121, 3);
	RequireProperties(0x200f, 0x21, 3);
	RequireProperties(0x202a, 0x21, 3);
	RequireProperties(0x202e, 0x21, 3);
	RequireProperties(0x2060, 0x21, 3);
	RequireProperties(0x2065, 0x22, 3);
	RequireProperties(0x206f, 0x21, 3);
	RequireProperties(0xfe00, 0x2c, 3);
	RequireProperties(0xfe0f, 0x2c, 3);
	RequireProperties(0xfeff, 0x21, 3);
	RequireProperties(0xfff0, 0x22, 3);
	RequireProperties(0xfff8, 0x22, 3);
	RequireProperties(0xfff9, 0x1, 1);
	RequireProperties(0x1d173, 0x21, 3);
	RequireProperties(0x1d17a, 0x21, 3);
	RequireProperties(0xe0000, 0x22, 3);
	RequireProperties(0xe0001, 0x21, 3);
	RequireProperties(0xe0020, 0x61, 3);
	RequireProperties(0xe007f, 0x61, 3);
	RequireProperties(0xe0080, 0x22, 3);
	RequireProperties(0xe0100, 0x2c, 3);
	RequireProperties(0xe0fff, 0x22, 3);
	RequireProperties(0xe1000, 0x2, 1);
}

static void TestModifiedClassesAndEmojiOverride() {
	RequireProperties(0x0301, 0xe60c, 1);
	RequireProperties(0x0327, 0xca0c, 1);
	RequireProperties(0x05b0, 0x160c, 1);
	RequireProperties(0x05b1, 0x0f0c, 1);
	RequireProperties(0x05b2, 0x100c, 1);
	RequireProperties(0x05b7, 0x140c, 1);
	RequireProperties(0x05bc, 0x0c0c, 1);
	RequireProperties(0x05bf, 0x0d0c, 1);
	RequireProperties(0x0c55, 0x000c, 1);
	RequireProperties(0x0c56, 0x000c, 1);
	RequireProperties(0x0e38, 0x030c, 1);
	RequireProperties(0x0f39, 0x7f0c, 1);
	RequireProperties(0x0f71, 0x810c, 1);
	RequireProperties(0x0f72, 0x840c, 1);
	RequireProperties(0x0f74, 0x830c, 1);
	RequireProperties(0x0fc6, 0xfe0c, 1);
	RequireProperties(0x1037, 0x090c, 1);
	RequireProperties(0x1a60, 0xfe0c, 1);
	RequireProperties(0x3099, 0x080c, 1);
	RequireProperties(0x094d, 0x090c, 1);
	RequireProperties(0x20dd, 0x000b, 1);
	RequireProperties(0x1f3fb, 0x000b, 1);
	RequireProperties(0x1f3ff, 0x000b, 1);
}

static void TestScratchAccumulationAndBounds() {
	const auto retained = static_cast<sgre::text::UnicodeBufferScratchFlags>(0xfffffff0);
	Require(sgre::text::InitializeUnicodeGlyphProperties({}, retained) == retained, "empty preprocessing changed scratch bits");
	std::array<sgre::text::SubstitutionGlyph, 3> glyphs{MakeRecord(0x41), MakeRecord(0x034f), MakeRecord(0x301)};
	const auto flags = sgre::text::InitializeUnicodeGlyphProperties(glyphs, retained);
	Require(static_cast<std::uint32_t>(flags) == 0xfffffff3, "preprocessing did not accumulate native buffer flags");
	Require(glyphs[0].CharacterState == 0xabcd0009 && glyphs[1].CharacterState == 0xabcd006c && glyphs[2].CharacterState == 0xabcde60c, "mixed preprocessing record properties differ");

	std::vector<sgre::text::SubstitutionGlyph> oversized(65537, MakeRecord(0x301));
	bool rejected = false;
	try {
		static_cast<void>(sgre::text::InitializeUnicodeGlyphProperties(oversized));
	} catch (const std::exception&) {
		rejected = true;
	}

	Require(rejected, "oversized Unicode preprocessing was accepted");
	for (const auto& glyph : oversized) {
		Require(glyph.CharacterState == 0xabcdffff, "oversized preprocessing partially changed records");
	}
}

static std::optional<std::uint32_t> ReadWord(bool allow_end = false) {
	std::uint32_t value = 0;
	for (std::uint32_t shift = 0; shift < 32; shift += 8) {
		const auto byte = std::cin.get();
		if (byte == std::char_traits<char>::eof()) {
			if (allow_end && shift == 0 && std::cin.eof()) {
				return std::nullopt;
			}

			throw std::runtime_error("truncated Unicode preprocessing fixture word");
		}

		value |= static_cast<std::uint32_t>(byte) << shift;
	}

	return value;
}

static void WriteWord(std::uint32_t value) {
	for (std::uint32_t shift = 0; shift < 32; shift += 8) {
		std::cout.put(static_cast<char>((value >> shift) & 0xffU));
	}
}

static void RunNativeVectors() {
	while (const auto count = ReadWord(true)) {
		Require(*count <= 4096, "native Unicode preprocessing fixture exceeds its chunk limit");
		const auto scratch_flags = static_cast<sgre::text::UnicodeBufferScratchFlags>(*ReadWord());
		std::vector<sgre::text::SubstitutionGlyph> glyphs(*count);
		for (auto& glyph : glyphs) {
			glyph.GlyphIndex = *ReadWord();
			glyph.Mask = *ReadWord();
			glyph.Cluster = *ReadWord();
			const auto properties = *ReadWord();
			glyph.GlyphProperties = static_cast<std::uint16_t>(properties & 0xffffU);
			glyph.PrivateState = static_cast<std::uint16_t>(properties >> 16);
			glyph.CharacterState = *ReadWord();
		}

		WriteWord(static_cast<std::uint32_t>(sgre::text::InitializeUnicodeGlyphProperties(glyphs, scratch_flags)));
		for (const auto& glyph : glyphs) {
			WriteWord(glyph.GlyphIndex);
			WriteWord(glyph.Mask);
			WriteWord(glyph.Cluster);
			WriteWord(glyph.GlyphProperties | (static_cast<std::uint32_t>(glyph.PrivateState) << 16));
			WriteWord(glyph.CharacterState);
		}

		std::cout.flush();
	}
}

int main(int argc, char** argv) {
	try {
		if (argc == 2 && std::string_view(argv[1]) == "--native") {
			RunNativeVectors();

			return 0;
		}

		Require(argc == 1, "unsupported Unicode preprocessing fixture arguments");
		TestAsciiAndRecordRetention();
		TestNativeIgnorablePredicates();
		TestModifiedClassesAndEmojiOverride();
		TestScratchAccumulationAndBounds();
		std::cout << "Unicode glyph preprocessing: four groups passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
