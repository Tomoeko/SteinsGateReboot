#include "sgre/text/direction_adjustment.hpp"

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

static sgre::text::SubstitutionGlyph MakeRecord(std::uint32_t index, std::uint16_t properties, std::uint32_t cluster) {
	sgre::text::SubstitutionGlyph glyph;
	glyph.GlyphIndex = index;
	glyph.Mask = 0x80000001;
	glyph.Cluster = cluster;
	glyph.GlyphProperties = static_cast<std::uint16_t>(0x7650 + index);
	glyph.PrivateState = static_cast<std::uint16_t>(0x3210 + index);
	glyph.CharacterState = 0xabcd0000U | properties;

	return glyph;
}

static void TestExactScriptGates() {
	const std::array<std::uint32_t, 27> rtl_tags{0x41646c6d, 0x41726162, 0x41726d69, 0x41767374, 0x43707274, 0x48656272, 0x48756e67, 0x4b686172, 0x4c796469, 0x4d616e64, 0x4d616e69, 0x4d656e64, 0x4d657263, 0x4d65726f, 0x4e617262, 0x4e626174, 0x4e6b6f6f, 0x4f726b68, 0x50616c6d, 0x50686c69, 0x50686c70, 0x50686e78, 0x50727469, 0x53616d72, 0x53617262, 0x53797263, 0x54686161};
	for (const auto tag : rtl_tags) {
		Require(sgre::text::AdjustUnicodeGlyphDirection({}, sgre::text::GlyphClusterLevel::Characters, sgre::text::GlyphShapingDirection::LeftToRight, tag) == sgre::text::GlyphShapingDirection::RightToLeft, "native RTL script gate differs");
	}
	for (const auto tag : {0U, 0xffffffffU, 0x61726162U, 0x4c61746eU, 0x48616e69U}) {
		Require(sgre::text::AdjustUnicodeGlyphDirection({}, sgre::text::GlyphClusterLevel::Characters, sgre::text::GlyphShapingDirection::RightToLeft, tag) == sgre::text::GlyphShapingDirection::LeftToRight, "native default/exact-case LTR gate differs");
	}
}

static void TestGroupReversalAndWholeRecords() {
	std::array<sgre::text::SubstitutionGlyph, 5> glyphs{MakeRecord(0, 7, 1), MakeRecord(1, 12, 2), MakeRecord(2, 0x121, 3), MakeRecord(3, 12, 4), MakeRecord(4, 7, 5)};
	const auto original = glyphs;
	Require(sgre::text::AdjustUnicodeGlyphDirection(glyphs, sgre::text::GlyphClusterLevel::Characters, sgre::text::GlyphShapingDirection::LeftToRight, 0x4c61746e) == sgre::text::GlyphShapingDirection::LeftToRight && glyphs == original, "matching horizontal direction changed records");
	const std::array<sgre::text::SubstitutionGlyph, 5> expected{original[4], original[2], original[3], original[0], original[1]};
	const auto direction = sgre::text::AdjustUnicodeGlyphDirection(glyphs, sgre::text::GlyphClusterLevel::Characters, sgre::text::GlyphShapingDirection::RightToLeft, 0x4c61746e);
	Require(direction == sgre::text::GlyphShapingDirection::LeftToRight && glyphs == expected, "mark-only group or whole-record reversal differs");
}

static void TestLevelOneMergesBeforeReversal() {
	std::array<sgre::text::SubstitutionGlyph, 5> glyphs{MakeRecord(0, 7, 9), MakeRecord(1, 12, 2), MakeRecord(2, 7, 2), MakeRecord(3, 11, 8), MakeRecord(4, 7, 8)};
	const auto original = glyphs;
	std::array<sgre::text::SubstitutionGlyph, 5> expected{original[4], original[2], original[3], original[0], original[1]};
	for (auto& glyph : expected) {
		if (glyph.Cluster != 2) {
			glyph.Mask &= 0xfffffffe;
		}
		glyph.Cluster = 2;
	}
	const auto direction = sgre::text::AdjustUnicodeGlyphDirection(glyphs, sgre::text::GlyphClusterLevel::MonotoneCharacters, sgre::text::GlyphShapingDirection::RightToLeft, 0x4c61746e);
	Require(direction == sgre::text::GlyphShapingDirection::LeftToRight && glyphs == expected, "level-one cluster expansion/merge ordering differs");
}

static void TestVerticalGatesAndPreflight() {
	std::array<sgre::text::SubstitutionGlyph, 2> glyphs{MakeRecord(0, 7, 9), MakeRecord(1, 7, 2)};
	const auto original = glyphs;
	Require(sgre::text::AdjustUnicodeGlyphDirection(glyphs, sgre::text::GlyphClusterLevel::Characters, sgre::text::GlyphShapingDirection::TopToBottom, 0x41726162) == sgre::text::GlyphShapingDirection::TopToBottom && glyphs == original, "native forward vertical direction changed records");
	Require(sgre::text::AdjustUnicodeGlyphDirection(glyphs, sgre::text::GlyphClusterLevel::Characters, sgre::text::GlyphShapingDirection::BottomToTop, 0x41726162) == sgre::text::GlyphShapingDirection::TopToBottom && glyphs[0] == original[1] && glyphs[1] == original[0], "native backward vertical direction differs");
	std::vector<sgre::text::SubstitutionGlyph> oversized(4097, original[0]);
	bool rejected = false;
	try {
		static_cast<void>(sgre::text::AdjustUnicodeGlyphDirection(oversized, sgre::text::GlyphClusterLevel::Characters, sgre::text::GlyphShapingDirection::RightToLeft, 0));
	} catch (const std::runtime_error&) {
		rejected = true;
	}
	Require(rejected && oversized.front() == original[0] && oversized.back() == original[0], "direction count bound did not reject before mutation");
	glyphs = original;
	rejected = false;
	try {
		static_cast<void>(sgre::text::AdjustUnicodeGlyphDirection(glyphs, sgre::text::GlyphClusterLevel::Characters, static_cast<sgre::text::GlyphShapingDirection>(0), 0));
	} catch (const std::runtime_error&) {
		rejected = true;
	}
	Require(rejected && glyphs == original, "invalid direction was not rejected before mutation");
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
		const auto level = static_cast<sgre::text::GlyphClusterLevel>(*ReadWord());
		const auto direction = static_cast<sgre::text::GlyphShapingDirection>(*ReadWord());
		const auto script_tag = *ReadWord();
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

		WriteWord(static_cast<std::uint32_t>(sgre::text::AdjustUnicodeGlyphDirection(glyphs, level, direction, script_tag)));
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

		Require(argc == 1, "unsupported Unicode direction fixture arguments");
		TestExactScriptGates();
		TestGroupReversalAndWholeRecords();
		TestLevelOneMergesBeforeReversal();
		TestVerticalGatesAndPreflight();
		std::cout << "Unicode direction adjustment: four groups passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
