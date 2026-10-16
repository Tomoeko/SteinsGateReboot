#include "sgre/text/recursive_decomposition.hpp"

#include <algorithm>
#include <array>
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

static void WriteBig(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value, std::size_t count) {
	for (std::size_t index = 0; index < count; ++index) {
		bytes.at(offset + count - index - 1) = static_cast<std::uint8_t>(value & 0xffU);
		value >>= 8;
	}
}

static std::vector<std::uint8_t> MakeFont(std::uint32_t profile) {
	Require(profile < 4, "unknown neutral font fixture profile");
	const std::array<std::uint32_t, 19> points = {0x20, 0x41, 0x42, 0xc5, 0x1fa, 0x300, 0x301, 0x30a, 0x327, 0x1100, 0x1161, 0x11a8, 0x2009, 0x2010, 0xac00, 0x1d157, 0x1d165, 0x1f600, 0xe0100};
	std::vector<std::uint32_t> selected;
	for (const auto point : points) {
		if ((profile != 3 && (point == 0x1fa || point == 0x2009 || point == 0xe0100)) || (profile == 1 && point == 0x20) || (profile == 2 && point == 0x2010)) {
			continue;
		}
		selected.push_back(point);
	}

	std::array<std::vector<std::uint8_t>, 6> tables;
	tables[0].resize(54);
	WriteBig(tables[0], 0, 0x10000, 4);
	WriteBig(tables[0], 12, 0x5f0f3cf5, 4);
	WriteBig(tables[0], 18, 1000, 2);
	tables[1].resize(6);
	WriteBig(tables[1], 0, 0x5000, 4);
	WriteBig(tables[1], 4, 96, 2);
	tables[2].resize(36);
	WriteBig(tables[2], 0, 0x10000, 4);
	WriteBig(tables[2], 34, 1, 2);
	tables[3].resize(194);
	WriteBig(tables[3], 0, 500, 2);
	tables[4] = {1, 0, 4, 4};
	const auto variation_offset = 20 + 16 + selected.size() * 12;
	tables[5].resize(variation_offset + 90);
	WriteBig(tables[5], 2, 2, 2);
	WriteBig(tables[5], 6, 3, 2);
	WriteBig(tables[5], 8, 20, 4);
	WriteBig(tables[5], 14, 5, 2);
	WriteBig(tables[5], 16, static_cast<std::uint32_t>(variation_offset), 4);
	WriteBig(tables[5], 20, 12, 2);
	WriteBig(tables[5], 24, static_cast<std::uint32_t>(variation_offset - 20), 4);
	WriteBig(tables[5], 32, static_cast<std::uint32_t>(selected.size()), 4);
	for (std::size_t index = 0; index < selected.size(); ++index) {
		const auto found = std::find(points.begin(), points.end(), selected[index]);
		const auto glyph = static_cast<std::uint32_t>(found - points.begin() + 1);
		WriteBig(tables[5], 36 + index * 12, selected[index], 4);
		WriteBig(tables[5], 40 + index * 12, selected[index], 4);
		WriteBig(tables[5], 44 + index * 12, glyph, 4);
	}
	WriteBig(tables[5], variation_offset, 14, 2);
	WriteBig(tables[5], variation_offset + 2, 90, 4);
	WriteBig(tables[5], variation_offset + 6, 4, 4);
	const std::array<std::uint32_t, 4> selectors = {0xfe00, 0xfe01, 0xfe02, 0xe0100};
	for (std::size_t index = 0; index < selectors.size(); ++index) {
		WriteBig(tables[5], variation_offset + 10 + index * 11, selectors[index], 3);
	}
	WriteBig(tables[5], variation_offset + 13, 54, 4);
	WriteBig(tables[5], variation_offset + 28, 62, 4);
	WriteBig(tables[5], variation_offset + 50, 81, 4);
	WriteBig(tables[5], variation_offset + 54, 1, 4);
	WriteBig(tables[5], variation_offset + 58, 0x41, 3);
	WriteBig(tables[5], variation_offset + 61, 1, 1);
	WriteBig(tables[5], variation_offset + 62, 3, 4);
	WriteBig(tables[5], variation_offset + 66, 0x41, 3);
	WriteBig(tables[5], variation_offset + 69, 0xffff, 2);
	WriteBig(tables[5], variation_offset + 71, 0x42, 3);
	WriteBig(tables[5], variation_offset + 74, 0, 2);
	WriteBig(tables[5], variation_offset + 76, 0xc5, 3);
	WriteBig(tables[5], variation_offset + 79, 24, 2);
	WriteBig(tables[5], variation_offset + 81, 1, 4);
	WriteBig(tables[5], variation_offset + 85, 0x1f600, 3);
	WriteBig(tables[5], variation_offset + 88, 24, 2);

	const std::array<std::string_view, 6> tags = {"head", "maxp", "hhea", "hmtx", "CFF ", "cmap"};
	std::vector<std::uint8_t> bytes(12 + tables.size() * 16);
	WriteBig(bytes, 0, 0x4f54544f, 4);
	WriteBig(bytes, 4, static_cast<std::uint32_t>(tables.size()), 2);
	for (std::size_t index = 0; index < tables.size(); ++index) {
		while (bytes.size() % 4 != 0) {
			bytes.push_back(0);
		}
		const auto record = 12 + index * 16;
		for (std::size_t character = 0; character < 4; ++character) {
			bytes[record + character] = static_cast<std::uint8_t>(tags[index][character]);
		}
		WriteBig(bytes, record + 8, static_cast<std::uint32_t>(bytes.size()), 4);
		WriteBig(bytes, record + 12, static_cast<std::uint32_t>(tables[index].size()), 4);
		bytes.insert(bytes.end(), tables[index].begin(), tables[index].end());
	}

	return bytes;
}

static sgre::text::SubstitutionGlyph Glyph(std::uint32_t codepoint, std::uint32_t cluster = 7) {
	sgre::text::SubstitutionGlyph value;
	value.GlyphIndex = codepoint;
	value.Mask = 0xffaaccef;
	value.Cluster = cluster;
	value.GlyphProperties = 0x1234;
	value.PrivateState = 0xabcd;
	value.CharacterState = 0xcafe4321;

	return value;
}

static std::uint32_t GlyphWord(const sgre::text::SubstitutionGlyph& glyph) {
	return glyph.GlyphProperties | (static_cast<std::uint32_t>(glyph.PrivateState) << 16);
}

static sgre::text::UnicodeBufferScratchFlags Initialize(std::span<sgre::text::SubstitutionGlyph> input) {
	return sgre::text::InitializeUnicodeGlyphProperties(input);
}

static void TestSegmentPreferencesAndOrdering() {
	const auto bytes = MakeFont(0);
	const sgre::resource::SfntFont font(bytes);
	std::array isolated = {Glyph(0x1fa)};
	const auto flags = Initialize(isolated);
	sgre::text::UnicodeNormalizationBuffer one(isolated, 8, flags);
	Require(one.NormalizeNeutralBeforeRecomposition(font), "isolated mapping failed");
	Require(one.InputGlyphs().size() == 2 && one.InputGlyphs()[0].GlyphIndex == 0xc5 && one.InputGlyphs()[1].GlyphIndex == 0x301, "isolated mapped-first decomposition differs");
	for (const auto level : {sgre::text::GlyphClusterLevel::MonotoneGraphemes, sgre::text::GlyphClusterLevel::MonotoneCharacters, sgre::text::GlyphClusterLevel::Characters}) {
		std::array input = {Glyph(0x1fa, 7), Glyph(0x327, 2)};
		const auto scratch = Initialize(input);
		sgre::text::UnicodeNormalizationBuffer buffer(input, 8, scratch);
		Require(buffer.NormalizeNeutralBeforeRecomposition(font, level), "combining segment failed");
		const auto output = buffer.InputGlyphs();
		Require(output.size() == 4 && output[0].GlyphIndex == 0x41 && output[1].GlyphIndex == 0x327 && output[2].GlyphIndex == 0x30a && output[3].GlyphIndex == 0x301, "recursive segment or stable combining order differs");
		Require(output[0].Cluster == (level == sgre::text::GlyphClusterLevel::Characters ? 7U : 2U), "cluster boundary expansion differs");
		Require((output[2].Mask & 1U) == (level == sgre::text::GlyphClusterLevel::Characters ? 1U : 0U), "cluster safety mask differs");
	}
}

static void TestOriginalAndSpacePolicies() {
	for (const auto profile : {0U, 1U, 2U, 3U}) {
		const auto bytes = MakeFont(profile);
		const sgre::resource::SfntFont font(bytes);
		std::array input = {Glyph(0x2009), Glyph(0x1680), Glyph(0x2011), Glyph(0x1fa)};
		const auto scratch = Initialize(input);
		sgre::text::UnicodeNormalizationBuffer buffer(input, 12, scratch);
		Require(buffer.NormalizeNeutralBeforeRecomposition(font), "space policy normalization failed");
		const auto output = buffer.InputGlyphs();
		Require(output[0].GlyphIndex == 0x2009 && GlyphWord(output[0]) == (profile == 3 ? 13U : profile == 1 ? 0U
		                                                                                                     : 1U),
		    "space mapping or original priority differs");
		Require((output[0].CharacterState & 0xff00U) == (profile == 0 || profile == 2 ? 0x500U : 0U), "space width marker differs");
		Require(output[1].GlyphIndex == 0x1680 && GlyphWord(output[1]) == 0 && output[2].GlyphIndex == 0x2011 && GlyphWord(output[2]) == (profile == 2 ? 0U : 14U), "missing space or nonbreaking hyphen policy differs");
		Require(output[3].GlyphIndex == (profile == 3 ? 0x1faU : 0xc5U), "original glyph priority differs");
	}
}

static void TestSelectorsAndBackfill() {
	const auto bytes = MakeFont(0);
	const sgre::resource::SfntFont font(bytes);
	for (const auto selector : {0xfe00U, 0xfe01U, 0xfe02U}) {
		std::array input = {Glyph(0x41, 9), Glyph(selector, 3), Glyph(0xfe01, 3)};
		const auto scratch = Initialize(input);
		sgre::text::UnicodeNormalizationBuffer buffer(input, 8, scratch);
		Require(buffer.NormalizeNeutralBeforeRecomposition(font), "variation segment failed");
		const auto output = buffer.InputGlyphs();
		Require(output.size() == (selector == 0xfe02 ? 3U : 2U), "selector pair consumption differs");
		Require(GlyphWord(output[0]) == (selector == 0xfe01 ? 0xffffU : 2U), "default or explicit variation query differs");
		Require(output[0].Cluster == (selector == 0xfe02 ? 9U : 3U) && output.back().GlyphIndex == 0xfe01 && GlyphWord(output.back()) == 0, "variation cluster merge or extra selector retention differs");
	}
	std::array input = {Glyph(0x1fa, 9), Glyph(0x41, 9), Glyph(0xfe01, 2)};
	const auto scratch = Initialize(input);
	sgre::text::UnicodeNormalizationBuffer buffer(input, 8, scratch);
	Require(buffer.NormalizeNeutralBeforeRecomposition(font), "variation backfill failed");
	Require(buffer.InputGlyphs().size() == 3 && buffer.InputGlyphs()[0].Cluster == 2 && buffer.InputGlyphs()[1].Cluster == 2 && buffer.InputGlyphs()[2].Cluster == 2, "previous emitted tail cluster was not backfilled");
}

static void TestFailureAndUnsupportedNonprogress() {
	const auto bytes = MakeFont(0);
	const sgre::resource::SfntFont font(bytes);
	std::array input = {Glyph(0x1fa)};
	const auto scratch = Initialize(input);
	sgre::text::UnicodeNormalizationBuffer failed(input, 1, scratch);
	Require(!failed.NormalizeNeutralBeforeRecomposition(font) && failed.State().Failed && failed.State().OutputActive && failed.State().InputCursor == 1 && failed.State().InputCount == 1 && failed.State().OutputCount == 1, "maximum failure or suppressed publication differs");
	std::array stuck = {Glyph(0x1fa), Glyph(0x41), Glyph(0xfe02)};
	const auto stuck_scratch = Initialize(stuck);
	sgre::text::UnicodeNormalizationBuffer buffer(stuck, 3, stuck_scratch);
	bool rejected = false;
	try {
		static_cast<void>(buffer.NormalizeNeutralBeforeRecomposition(font));
	} catch (const std::runtime_error& error) {
		rejected = std::string_view(error.what()).find("extra-selector copy cannot advance") != std::string_view::npos;
	}
	Require(rejected && buffer.State().Failed && buffer.State().InputCursor == 2 && buffer.State().OutputCount == 3 && buffer.State().OutputActive, "native nonprogressing selector profile was hidden or consumed");
}

static void TestEmptyAndLimits() {
	const auto bytes = MakeFont(0);
	const sgre::resource::SfntFont font(bytes);
	const std::vector<sgre::text::SubstitutionGlyph> empty;
	sgre::text::UnicodeNormalizationBuffer buffer(empty, 0);
	Require(buffer.NormalizeNeutralBeforeRecomposition(font) && !buffer.State().OutputActive && buffer.State().OutputCount == 0, "empty input entered an output phase");
	const std::array input = {Glyph(0x41)};
	for (const auto level : {3U, 0xffffffffU}) {
		bool rejected = false;
		try {
			sgre::text::UnicodeNormalizationBuffer invalid(input, 1);
			static_cast<void>(invalid.NormalizeNeutralBeforeRecomposition(font, static_cast<sgre::text::GlyphClusterLevel>(level)));
		} catch (const std::runtime_error&) {
			rejected = true;
		}
		Require(rejected, "unsupported cluster level was accepted");
	}
	bool rejected = false;
	try {
		sgre::text::UnicodeNormalizationBuffer excessive(input, 4097);
		static_cast<void>(excessive.NormalizeNeutralBeforeRecomposition(font));
	} catch (const std::runtime_error&) {
		rejected = true;
	}
	Require(rejected, "normalization exceeded the bounded resident record profile");
}

static std::uint32_t ReadWord() {
	std::uint32_t value = 0;
	for (std::uint32_t shift = 0; shift < 32; shift += 8) {
		const auto byte = std::cin.get();
		Require(byte != std::char_traits<char>::eof(), "truncated neutral normalization proof input");
		value |= static_cast<std::uint32_t>(byte) << shift;
	}

	return value;
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
		WriteWord(GlyphWord(glyph));
		WriteWord(glyph.CharacterState);
	}
}

static void RunNative() {
	const auto font_length = ReadWord();
	Require(font_length > 0 && font_length <= 32 * 1024 * 1024, "neutral normalization proof font exceeds limit");
	std::vector<std::uint8_t> bytes(font_length);
	for (auto& byte : bytes) {
		const auto value = std::cin.get();
		Require(value != std::char_traits<char>::eof(), "truncated neutral normalization font");
		byte = static_cast<std::uint8_t>(value);
	}
	const sgre::resource::SfntFont font(bytes);
	while (std::cin.peek() != std::char_traits<char>::eof()) {
		const auto maximum = ReadWord();
		auto flags = static_cast<sgre::text::UnicodeBufferScratchFlags>(ReadWord());
		const auto level = static_cast<sgre::text::GlyphClusterLevel>(ReadWord());
		const auto initialize = ReadWord();
		const auto count = ReadWord();
		Require(maximum <= 4096 && count <= maximum + 1 && initialize <= 1, "neutral normalization proof bounds invalid");
		std::vector<sgre::text::SubstitutionGlyph> input(count);
		for (auto& glyph : input) {
			glyph.GlyphIndex = ReadWord();
			glyph.Mask = ReadWord();
			glyph.Cluster = ReadWord();
			const auto word = ReadWord();
			glyph.GlyphProperties = static_cast<std::uint16_t>(word & 0xffffU);
			glyph.PrivateState = static_cast<std::uint16_t>(word >> 16);
			glyph.CharacterState = ReadWord();
		}
		if (initialize != 0) {
			flags = sgre::text::InitializeUnicodeGlyphProperties(input, flags);
		}
		sgre::text::UnicodeNormalizationBuffer buffer(input, maximum, flags);
		std::uint32_t result = 0;
		try {
			result = buffer.NormalizeNeutralBeforeRecomposition(font, level) ? 1U : 0U;
		} catch (const std::runtime_error& error) {
			if (std::string_view(error.what()).find("extra-selector copy cannot advance") == std::string_view::npos) {
				throw;
			}
			result = 2;
		}
		const auto state = buffer.State();
		for (const auto word : {result, state.InputCursor, state.InputCount, state.OutputCount, state.ResidentCapacity, state.MaximumLength, static_cast<std::uint32_t>(state.ScratchFlags), static_cast<std::uint32_t>(state.OutputActive), static_cast<std::uint32_t>(state.Failed), static_cast<std::uint32_t>(state.OutputAliasesInput)}) {
			WriteWord(word);
		}
		WriteGlyphs(buffer.InputGlyphs());
		WriteGlyphs(buffer.OutputGlyphs());
		std::cout.flush();
	}
}

int main(int argc, char** argv) {
	try {
		if (argc == 2 && std::string_view(argv[1]) == "--native") {
			RunNative();
		} else if (argc == 3 && std::string_view(argv[1]) == "--fixture") {
			const auto bytes = MakeFont(static_cast<std::uint32_t>(std::stoul(argv[2])));
			WriteWord(static_cast<std::uint32_t>(bytes.size()));
			for (const auto byte : bytes) {
				std::cout.put(static_cast<char>(byte));
			}
		} else if (argc == 1) {
			TestSegmentPreferencesAndOrdering();
			TestOriginalAndSpacePolicies();
			TestSelectorsAndBackfill();
			TestFailureAndUnsupportedNonprogress();
			TestEmptyAndLimits();
			std::cout << "PASS 5 neutral normalization groups\n";
		} else {
			throw std::runtime_error("unknown neutral normalization proof arguments");
		}
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
