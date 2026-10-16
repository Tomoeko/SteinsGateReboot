#include "sgre/text/combining_reorder.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static sgre::text::SubstitutionGlyph MakeGlyph(std::uint32_t identity, std::uint32_t category, std::uint32_t combining_class, std::uint32_t cluster = 9, std::uint32_t mask = 0x80000007) {
	sgre::text::SubstitutionGlyph glyph;
	glyph.GlyphIndex = identity;
	glyph.Mask = mask;
	glyph.Cluster = cluster;
	glyph.GlyphProperties = static_cast<std::uint16_t>(identity ^ 0x5aa5U);
	glyph.PrivateState = static_cast<std::uint16_t>(identity ^ 0x3cc3U);
	glyph.CharacterState = 0xa55a00e0U | (combining_class << 8) | category;

	return glyph;
}

static bool SameGlyph(const sgre::text::SubstitutionGlyph& first, const sgre::text::SubstitutionGlyph& second) {
	return first.GlyphIndex == second.GlyphIndex && first.Mask == second.Mask && first.Cluster == second.Cluster && first.GlyphProperties == second.GlyphProperties && first.PrivateState == second.PrivateState && first.CharacterState == second.CharacterState;
}

static void TestStableOrderingAndCompleteRecords() {
	std::vector<sgre::text::SubstitutionGlyph> glyphs = {MakeGlyph(1, 7, 255), MakeGlyph(2, 12, 230), MakeGlyph(3, 10, 202), MakeGlyph(4, 11, 230), MakeGlyph(5, 12, 1), MakeGlyph(6, 7, 255)};
	const auto original = glyphs;
	const auto flags = sgre::text::ReorderNeutralUnicodeCombiningGlyphs(glyphs, {}, sgre::text::GlyphClusterLevel::Characters, static_cast<sgre::text::UnicodeBufferScratchFlags>(0xa5a50003));
	Require(static_cast<std::uint32_t>(flags) == 0xa5a50003, "equal-cluster ordering changed scratch flags");
	const std::vector<std::size_t> expected = {0, 4, 2, 1, 3, 5};
	for (std::size_t index = 0; index < glyphs.size(); ++index) {
		Require(SameGlyph(glyphs[index], original[expected[index]]), "stable combining ordering lost complete record fields");
	}
}

static void TestMergedClustersAndPreviousInput() {
	for (const auto level : {sgre::text::GlyphClusterLevel::MonotoneGraphemes, sgre::text::GlyphClusterLevel::MonotoneCharacters}) {
		std::vector<sgre::text::SubstitutionGlyph> glyphs = {MakeGlyph(1, 12, 230, 9, 7), MakeGlyph(2, 12, 202, 2, 5), MakeGlyph(3, 12, 0, 2, 11), MakeGlyph(4, 7, 0, 8)};
		std::vector<sgre::text::SubstitutionGlyph> previous = {MakeGlyph(10, 7, 0, 4, 7), MakeGlyph(11, 7, 0, 9, 3), MakeGlyph(12, 7, 0, 9, 7)};
		const auto unchanged_previous = previous.front();
		const auto flags = sgre::text::ReorderNeutralUnicodeCombiningGlyphs(glyphs, previous, level, static_cast<sgre::text::UnicodeBufferScratchFlags>(0x80000001));
		Require(static_cast<std::uint32_t>(flags) == 0x80000001, "cluster merge changed unrelated scratch flags");
		Require(glyphs[0].GlyphIndex == 2 && glyphs[0].Cluster == 2 && glyphs[0].Mask == 5, "minimum-cluster moved mark changed its mask");
		Require(glyphs[1].GlyphIndex == 1 && glyphs[1].Cluster == 2 && glyphs[1].Mask == 6, "changed input cluster retained unsafe bit");
		Require(glyphs[2].Cluster == 2 && glyphs[2].Mask == 11 && glyphs[3].Cluster == 8, "equal-cluster boundary expansion changed the wrong records");
		Require(SameGlyph(previous[0], unchanged_previous), "backfill crossed a different retained cluster");
		Require(previous[1].Cluster == 2 && previous[1].Mask == 2 && previous[2].Cluster == 2 && previous[2].Mask == 6, "retained previous input did not receive native backfill");
	}

	std::vector<sgre::text::SubstitutionGlyph> aliased = {MakeGlyph(1, 12, 230, 9, 7), MakeGlyph(2, 12, 202, 2, 5), MakeGlyph(3, 7, 0, 9, 3)};
	static_cast<void>(sgre::text::ReorderNeutralUnicodeCombiningGlyphs(aliased, aliased, sgre::text::GlyphClusterLevel::MonotoneCharacters, sgre::text::UnicodeBufferScratchFlags::None));
	Require(aliased[0].GlyphIndex == 2 && aliased[1].GlyphIndex == 1 && aliased[2].Cluster == 2 && aliased[2].Mask == 2, "aliased retained input backfill differs");
}

static void TestCharacterLevelUnsafeMasks() {
	std::vector<sgre::text::SubstitutionGlyph> glyphs = {MakeGlyph(1, 10, 255, 9, 0x80000002), MakeGlyph(2, 11, 1, 2, 0x40000004)};
	std::vector<sgre::text::SubstitutionGlyph> previous = {MakeGlyph(3, 7, 0, 9, 7)};
	const auto original_previous = previous.front();
	const auto flags = sgre::text::ReorderNeutralUnicodeCombiningGlyphs(glyphs, previous, sgre::text::GlyphClusterLevel::Characters, static_cast<sgre::text::UnicodeBufferScratchFlags>(0x80000001));
	Require(static_cast<std::uint32_t>(flags) == 0x80000011, "character-level ordering did not record unsafe scratch state");
	Require(glyphs[0].GlyphIndex == 2 && glyphs[0].Cluster == 2 && glyphs[0].Mask == 0x40000004, "minimum character cluster was marked unsafe");
	Require(glyphs[1].GlyphIndex == 1 && glyphs[1].Cluster == 9 && glyphs[1].Mask == 0x80000003, "character-level cluster or retained mask bits changed");
	Require(SameGlyph(previous.front(), original_previous), "character level backfilled previous input");
}

static void TestNativeRunLimitsAndPreflight() {
	for (const std::size_t count : {1U, 2U, 32U, 33U, 64U}) {
		std::vector<sgre::text::SubstitutionGlyph> glyphs;
		for (std::size_t index = 0; index < count; ++index) {
			glyphs.push_back(MakeGlyph(static_cast<std::uint32_t>(index), 12, static_cast<std::uint32_t>(count - index)));
		}
		const auto original = glyphs;
		static_cast<void>(sgre::text::ReorderNeutralUnicodeCombiningGlyphs(glyphs, {}, sgre::text::GlyphClusterLevel::Characters, sgre::text::UnicodeBufferScratchFlags::None));
		for (std::size_t index = 0; index < count; ++index) {
			const auto expected = count >= 2 && count <= 32 ? count - index - 1 : index;
			Require(SameGlyph(glyphs[index], original[expected]), "native 2..32 combining-run limit differs");
		}
	}

	std::vector<sgre::text::SubstitutionGlyph> unchanged = {MakeGlyph(1, 12, 230), MakeGlyph(2, 12, 0), MakeGlyph(3, 12, 202), MakeGlyph(4, 13, 255), MakeGlyph(5, 12, 1)};
	const auto original = unchanged;
	static_cast<void>(sgre::text::ReorderNeutralUnicodeCombiningGlyphs(unchanged, {}, sgre::text::GlyphClusterLevel::Characters, sgre::text::UnicodeBufferScratchFlags::None));
	Require(std::equal(unchanged.begin(), unchanged.end(), original.begin(), SameGlyph), "zero class or nonmark failed to split combining runs");

	for (const auto failure : {0U, 1U, 2U}) {
		std::vector<sgre::text::SubstitutionGlyph> too_many(4097);
		bool rejected = false;
		try {
			if (failure == 0) {
				static_cast<void>(sgre::text::ReorderNeutralUnicodeCombiningGlyphs(unchanged, {}, static_cast<sgre::text::GlyphClusterLevel>(3), sgre::text::UnicodeBufferScratchFlags::None));
			} else if (failure == 1) {
				static_cast<void>(sgre::text::ReorderNeutralUnicodeCombiningGlyphs(too_many, unchanged, sgre::text::GlyphClusterLevel::Characters, sgre::text::UnicodeBufferScratchFlags::None));
			} else {
				static_cast<void>(sgre::text::ReorderNeutralUnicodeCombiningGlyphs(unchanged, too_many, sgre::text::GlyphClusterLevel::Characters, sgre::text::UnicodeBufferScratchFlags::None));
			}
		} catch (const std::runtime_error&) {
			rejected = true;
		}
		Require(rejected && std::equal(unchanged.begin(), unchanged.end(), original.begin(), SameGlyph), "combining reorder preflight partially mutated records");
	}
}

static std::uint32_t ReadWord() {
	std::uint32_t value = 0;
	for (std::uint32_t shift = 0; shift < 32; shift += 8) {
		const auto byte = std::cin.get();
		Require(byte != std::char_traits<char>::eof(), "truncated combining reorder proof input");
		value |= static_cast<std::uint32_t>(byte) << shift;
	}

	return value;
}

static sgre::text::SubstitutionGlyph ReadGlyph() {
	sgre::text::SubstitutionGlyph glyph;
	glyph.GlyphIndex = ReadWord();
	glyph.Mask = ReadWord();
	glyph.Cluster = ReadWord();
	const auto private_word = ReadWord();
	glyph.GlyphProperties = static_cast<std::uint16_t>(private_word & 0xffffU);
	glyph.PrivateState = static_cast<std::uint16_t>(private_word >> 16);
	glyph.CharacterState = ReadWord();

	return glyph;
}

static void WriteWord(std::uint32_t value) {
	for (std::uint32_t shift = 0; shift < 32; shift += 8) {
		std::cout.put(static_cast<char>((value >> shift) & 0xffU));
	}
}

static void WriteGlyphs(std::span<const sgre::text::SubstitutionGlyph> glyphs) {
	for (const auto& glyph : glyphs) {
		WriteWord(glyph.GlyphIndex);
		WriteWord(glyph.Mask);
		WriteWord(glyph.Cluster);
		WriteWord(glyph.GlyphProperties | (static_cast<std::uint32_t>(glyph.PrivateState) << 16));
		WriteWord(glyph.CharacterState);
	}
}

static void RunNativeDriver() {
	while (std::cin.peek() != std::char_traits<char>::eof()) {
		const auto count = ReadWord();
		const auto previous_count = ReadWord();
		const auto level = static_cast<sgre::text::GlyphClusterLevel>(ReadWord());
		const auto scratch_flags = static_cast<sgre::text::UnicodeBufferScratchFlags>(ReadWord());
		const auto alias = ReadWord();
		Require(count <= 4096 && previous_count <= 4096 && alias <= 1, "combining reorder proof input exceeds its limits");
		std::vector<sgre::text::SubstitutionGlyph> glyphs(count);
		std::vector<sgre::text::SubstitutionGlyph> previous(previous_count);
		for (auto& glyph : glyphs) {
			glyph = ReadGlyph();
		}
		for (auto& glyph : previous) {
			glyph = ReadGlyph();
		}

		std::span<sgre::text::SubstitutionGlyph> input_view = glyphs;
		std::span<sgre::text::SubstitutionGlyph> previous_view = previous;
		if (alias != 0) {
			const auto original_count = glyphs.size();
			glyphs.resize(std::max(glyphs.size(), previous.size()));
			for (std::size_t index = 0; index < previous.size(); ++index) {
				if (index < original_count) {
					Require(SameGlyph(glyphs[index], previous[index]), "aliased combining proof records disagree");
				} else {
					glyphs[index] = previous[index];
				}
			}
			input_view = std::span(glyphs).first(count);
			previous_view = std::span(glyphs).first(previous_count);
		}

		const auto result = sgre::text::ReorderNeutralUnicodeCombiningGlyphs(input_view, previous_view, level, scratch_flags);
		WriteWord(static_cast<std::uint32_t>(result));
		WriteGlyphs(input_view);
		WriteGlyphs(previous_view);
		std::cout.flush();
	}
}

int main(int argc, char** argv) {
	try {
		if (argc == 2 && std::string_view(argv[1]) == "--native") {
			RunNativeDriver();

			return 0;
		}

		Require(argc == 1, "unsupported combining reorder fixture arguments");
		TestStableOrderingAndCompleteRecords();
		TestMergedClustersAndPreviousInput();
		TestCharacterLevelUnsafeMasks();
		TestNativeRunLimitsAndPreflight();
		std::cout << "Neutral Unicode combining reorder: four groups passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
