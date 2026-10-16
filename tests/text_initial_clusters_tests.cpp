#include "sgre/text/initial_clusters.hpp"

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

static sgre::text::SubstitutionGlyph MakeRecord(std::uint32_t cluster, std::uint16_t properties, std::uint32_t mask = 0x10) {
	sgre::text::SubstitutionGlyph glyph;
	glyph.GlyphIndex = 0x3042;
	glyph.Mask = mask;
	glyph.Cluster = cluster;
	glyph.GlyphProperties = 0x7654;
	glyph.PrivateState = 0x3210;
	glyph.CharacterState = 0xabcd0000U | properties;

	return glyph;
}

static void TestMergedClustersAndRetention() {
	std::array<sgre::text::SubstitutionGlyph, 4> glyphs{MakeRecord(9, 7, 0x11), MakeRecord(2, 12, 0x11), MakeRecord(4, 10, 0x11), MakeRecord(8, 7, 0x11)};
	auto expected = glyphs;
	for (std::size_t index = 0; index < 3; ++index) {
		expected[index].Cluster = 2;
		if (index != 1) {
			expected[index].Mask = 0x10;
		}
	}
	const auto flags = sgre::text::InitializeUnicodeGlyphClusters(glyphs, sgre::text::GlyphClusterLevel::MonotoneGraphemes, static_cast<sgre::text::UnicodeBufferScratchFlags>(0x80000003));
	Require(glyphs == expected, "merged cluster or retained record fields differ");
	Require(static_cast<std::uint32_t>(flags) == 0x80000003, "cluster merge changed scratch bits");

	std::array<sgre::text::SubstitutionGlyph, 3> unsigned_clusters{MakeRecord(0x80000000, 7), MakeRecord(0xffffffff, 12), MakeRecord(7, 11)};
	static_cast<void>(sgre::text::InitializeUnicodeGlyphClusters(unsigned_clusters, sgre::text::GlyphClusterLevel::MonotoneGraphemes, sgre::text::UnicodeBufferScratchFlags::HasNonAscii));
	Require(unsigned_clusters[0].Cluster == 7 && unsigned_clusters[1].Cluster == 7 && unsigned_clusters[2].Cluster == 7, "native cluster minimum was treated as signed");
}

static void TestEqualClusterBoundaryExpansion() {
	std::array<sgre::text::SubstitutionGlyph, 5> glyphs{MakeRecord(9, 7, 1), MakeRecord(9, 7, 1), MakeRecord(2, 12, 1), MakeRecord(2, 7, 1), MakeRecord(8, 7, 1)};
	static_cast<void>(sgre::text::InitializeUnicodeGlyphClusters(glyphs, sgre::text::GlyphClusterLevel::MonotoneGraphemes, sgre::text::UnicodeBufferScratchFlags::HasNonAscii));
	Require(glyphs[0].Cluster == 2 && glyphs[1].Cluster == 2 && glyphs[2].Cluster == 2 && glyphs[3].Cluster == 2 && glyphs[4].Cluster == 8, "native equal-cluster extension differs");
	Require(glyphs[0].Mask == 0 && glyphs[1].Mask == 0 && glyphs[2].Mask == 1 && glyphs[3].Mask == 1, "native changed-cluster unsafe bit clearing differs");
}

static void TestRetainedCharacterClustersAndJoiners() {
	for (const auto level : {sgre::text::GlyphClusterLevel::MonotoneCharacters, sgre::text::GlyphClusterLevel::Characters}) {
		std::array<sgre::text::SubstitutionGlyph, 6> glyphs{MakeRecord(4, 7), MakeRecord(8, 0x121), MakeRecord(9, 0x221), MakeRecord(1, 0x21), MakeRecord(6, 11), MakeRecord(7, 7)};
		auto expected = glyphs;
		expected[1].Mask |= 1;
		expected[2].Mask |= 1;
		expected[4].Mask |= 1;
		const auto flags = sgre::text::InitializeUnicodeGlyphClusters(glyphs, level, sgre::text::UnicodeBufferScratchFlags::HasNonAscii);
		Require(glyphs == expected, "retained cluster values or joining predicates differ");
		Require(static_cast<std::uint32_t>(flags) == 0x11, "unsafe cluster scratch bit differs");
	}
	std::array<sgre::text::SubstitutionGlyph, 2> equal{MakeRecord(0xffffffff, 7), MakeRecord(0xffffffff, 12)};
	const auto flags = sgre::text::InitializeUnicodeGlyphClusters(equal, sgre::text::GlyphClusterLevel::Characters, sgre::text::UnicodeBufferScratchFlags::HasNonAscii);
	Require(static_cast<std::uint32_t>(flags) == 1 && equal[0].Mask == 0x10 && equal[1].Mask == 0x10, "equal clusters spuriously became unsafe");
}

static void TestNoOpAndPreflight() {
	std::array<sgre::text::SubstitutionGlyph, 2> glyphs{MakeRecord(0x80000000, 7), MakeRecord(1, 12)};
	const auto initial = glyphs;
	const auto flags = static_cast<sgre::text::UnicodeBufferScratchFlags>(0xfffffff0);
	Require(sgre::text::InitializeUnicodeGlyphClusters(glyphs, sgre::text::GlyphClusterLevel::MonotoneGraphemes, flags) == flags && glyphs == initial, "non-ASCII gate changed records");
	Require(sgre::text::InitializeUnicodeGlyphClusters({}, sgre::text::GlyphClusterLevel::Characters, flags) == flags, "empty initial clustering changed flags");
	std::vector<sgre::text::SubstitutionGlyph> oversized(4097, glyphs.front());
	bool rejected = false;
	try {
		static_cast<void>(sgre::text::InitializeUnicodeGlyphClusters(oversized, sgre::text::GlyphClusterLevel::MonotoneGraphemes, sgre::text::UnicodeBufferScratchFlags::HasNonAscii));
	} catch (const std::runtime_error&) {
		rejected = true;
	}
	Require(rejected && oversized.front() == initial.front() && oversized.back() == initial.front(), "oversized clustering was not rejected before mutation");
	rejected = false;
	try {
		static_cast<void>(sgre::text::InitializeUnicodeGlyphClusters(glyphs, static_cast<sgre::text::GlyphClusterLevel>(3), sgre::text::UnicodeBufferScratchFlags::HasNonAscii));
	} catch (const std::runtime_error&) {
		rejected = true;
	}
	Require(rejected && glyphs == initial, "unsupported cluster level was not rejected before mutation");
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

		WriteWord(static_cast<std::uint32_t>(sgre::text::InitializeUnicodeGlyphClusters(glyphs, level, scratch_flags)));
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

		Require(argc == 1, "unsupported initial Unicode cluster fixture arguments");
		TestMergedClustersAndRetention();
		TestEqualClusterBoundaryExpansion();
		TestRetainedCharacterClustersAndJoiners();
		TestNoOpAndPreflight();
		std::cout << "Initial Unicode clusters: four groups passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
