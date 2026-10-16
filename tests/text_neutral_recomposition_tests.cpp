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
	Require(profile < 6, "unknown neutral font fixture profile");
	const std::array<std::uint32_t, 19> points = {0x20, 0x41, 0x42, 0xc5, 0x1fa, 0x300, 0x301, 0x30a, 0x327, 0x1100, 0x1161, 0x11a8, 0x2009, 0x2010, 0xac00, 0x1d157, 0x1d165, 0x1f600, 0xe0100};
	std::vector<std::uint32_t> selected;
	for (const auto point : points) {
		if ((profile != 3 && profile != 5 && (point == 0x1fa || point == 0x2009 || point == 0xe0100)) || (profile == 1 && point == 0x20) || (profile == 2 && point == 0x2010) || (profile == 4 && point == 0xc5)) {
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

static void TestCandidateAcceptanceAndMarkGates() {
	for (const auto profile : {0U, 3U, 4U}) {
		const auto bytes = MakeFont(profile);
		const sgre::resource::SfntFont font(bytes);
		std::array input = {Glyph(0x41), Glyph(0x30a), Glyph(0x301)};
		const auto flags = Initialize(input);
		sgre::text::UnicodeNormalizationBuffer buffer(input, 8, flags);
		Require(buffer.NormalizeNeutral(font), "neutral recomposition failed");
		const auto output = buffer.InputGlyphs();
		Require(output.size() == (profile == 3 ? 1U : profile == 4 ? 3U
		                                                           : 2U),
		    "composition candidate nominal acceptance differs");
		Require(output[0].GlyphIndex == (profile == 3 ? 0x1faU : profile == 4 ? 0x41U
		                                                                      : 0xc5U),
		    "recomposed codepoint differs");
		Require(GlyphWord(output[0]) == (profile == 3 ? 5U : profile == 4 ? 2U
		                                                                  : 4U) &&
		        (output[0].CharacterState & 0xffff0000U) == 0xcafe0000U,
		    "composed nominal or inherited state differs");
	}
	const auto bytes = MakeFont(0);
	const sgre::resource::SfntFont font(bytes);
	std::array hangul = {Glyph(0x1100), Glyph(0x1161)};
	const auto flags = Initialize(hangul);
	sgre::text::UnicodeNormalizationBuffer buffer(hangul, 8, flags);
	Require(buffer.NormalizeNeutral(font) && buffer.InputGlyphs().size() == 2 && buffer.InputGlyphs()[0].GlyphIndex == 0x1100 && buffer.InputGlyphs()[1].GlyphIndex == 0x1161, "neutral mode2 treated nonmark Hangul as a generic NFC pass");
}

static void TestModifiedClassBlocking() {
	const auto bytes = MakeFont(0);
	const sgre::resource::SfntFont font(bytes);
	std::array equal = {Glyph(0x41), Glyph(0x300), Glyph(0x30a)};
	const auto flags = Initialize(equal);
	sgre::text::UnicodeNormalizationBuffer blocked(equal, 8, flags);
	Require(blocked.NormalizeNeutral(font) && blocked.InputGlyphs().size() == 3 && blocked.InputGlyphs()[0].GlyphIndex == 0x41, "equal modified class failed to block later composition");
	std::vector<sgre::text::SubstitutionGlyph> long_run = {Glyph(0x41), Glyph(0x300), Glyph(0x327), Glyph(0x30a)};
	while (long_run.size() < 34) {
		long_run.push_back(Glyph(0x301));
	}
	const auto long_flags = Initialize(long_run);
	sgre::text::UnicodeNormalizationBuffer disorder(long_run, 64, long_flags);
	Require(disorder.NormalizeNeutral(font) && disorder.InputGlyphs().size() == 34 && disorder.InputGlyphs()[0].GlyphIndex == 0x41, "descending class in an unsorted long run did not disable composition");
}

static void TestOutputMergeAndFollowingInput() {
	const auto bytes = MakeFont(0);
	const sgre::resource::SfntFont font(bytes);
	for (const auto level : {sgre::text::GlyphClusterLevel::MonotoneGraphemes, sgre::text::GlyphClusterLevel::MonotoneCharacters, sgre::text::GlyphClusterLevel::Characters}) {
		std::array input = {Glyph(0x41, 9), Glyph(0x327, 2), Glyph(0x30a, 9), Glyph(0x42, 9)};
		const auto flags = Initialize(input);
		sgre::text::UnicodeNormalizationBuffer buffer(input, 8, flags);
		Require(buffer.NormalizeNeutral(font, level), "output cluster merge failed");
		const auto output = buffer.InputGlyphs();
		Require(output.size() == 3 && output[0].GlyphIndex == 0xc5 && output[1].GlyphIndex == 0x327 && output[2].GlyphIndex == 0x42, "composition through intervening lower-class mark differs");
		Require(output[0].Cluster == (level == sgre::text::GlyphClusterLevel::Characters ? 9U : 2U) && output[2].Cluster == (level == sgre::text::GlyphClusterLevel::Characters ? 9U : 2U), "output merge or following-input backfill differs");
		Require((output[0].Mask & 1U) == (level == sgre::text::GlyphClusterLevel::Characters ? 1U : 0U), "output merge changed cluster-level2 safety state");
	}
}

static void TestFailedFirstPublicationAndResidentBounds() {
	const auto bytes = MakeFont(0);
	const sgre::resource::SfntFont font(bytes);
	std::array input = {Glyph(0x1fa)};
	const auto flags = Initialize(input);
	sgre::text::UnicodeNormalizationBuffer failed(input, 1, flags);
	Require(!failed.NormalizeNeutral(font) && failed.State().Failed && failed.State().InputCursor == 2 && failed.State().OutputCount == 1 && failed.State().OutputActive, "failed first publication skipped the unconditional recomposition copy");
	Require(failed.InputGlyphs()[0].GlyphIndex == 0 && failed.InputGlyphs()[0].CharacterState == 0 && GlyphWord(failed.InputGlyphs()[0]) == 0, "resident tail copy after failed first publication differs");
	std::array excessive = {Glyph(0x41), Glyph(0x42), Glyph(0x1fa)};
	const auto excessive_flags = Initialize(excessive);
	sgre::text::UnicodeNormalizationBuffer buffer(excessive, 2, excessive_flags);
	bool rejected = false;
	try {
		static_cast<void>(buffer.NormalizeNeutral(font));
	} catch (const std::runtime_error& error) {
		rejected = std::string_view(error.what()).find("copy exceeds resident input records") != std::string_view::npos;
	}
	Require(rejected && buffer.State().Failed && buffer.State().InputCursor == 3 && buffer.State().OutputCount == 0 && buffer.State().OutputActive, "native read beyond resident records was hidden");
}

static void TestSeparateFinalWordMapping() {
	std::array input = {Glyph(0x41), Glyph(0xfe00), Glyph(0x1fa)};
	input[0].GlyphProperties = 0xffff;
	input[0].PrivateState = 0xabcd;
	input[1].GlyphProperties = 0;
	input[1].PrivateState = 0;
	input[2].GlyphProperties = 0x1234;
	input[2].PrivateState = 0x8000;
	sgre::text::UnicodeNormalizationBuffer buffer(input, 8);
	Require(buffer.PublishNominalGlyphIndices() == sgre::text::NormalizedGlyphContent::GlyphIndices, "final mapping did not report original content tag2");
	const auto output = buffer.InputGlyphs();
	for (std::size_t index = 0; index < input.size(); ++index) {
		Require(output[index].GlyphIndex == GlyphWord(input[index]) && output[index].Mask == input[index].Mask && output[index].Cluster == input[index].Cluster && output[index].GlyphProperties == input[index].GlyphProperties && output[index].PrivateState == input[index].PrivateState && output[index].CharacterState == input[index].CharacterState, "final mapping changed words other than GlyphIndex");
	}
	Require(buffer.State().InputCount == 3 && buffer.State().InputCursor == 0 && !buffer.State().OutputActive, "final mapping changed owner counts or phase");
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
		const auto finalize = ReadWord();
		const auto count = ReadWord();
		Require(maximum <= 4096 && count <= maximum + 1 && initialize <= 1 && finalize <= 1, "neutral normalization proof bounds invalid");
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
			result = buffer.NormalizeNeutral(font, level) ? 1U : 0U;
		} catch (const std::runtime_error& error) {
			if (std::string_view(error.what()).find("extra-selector copy cannot advance") != std::string_view::npos) {
				result = 2;
			} else if (std::string_view(error.what()).find("copy exceeds resident input records") != std::string_view::npos) {
				result = 3;
			} else {
				throw;
			}
		}
		const auto content = finalize != 0 && result < 2 ? static_cast<std::uint32_t>(buffer.PublishNominalGlyphIndices()) : 0U;
		const auto state = buffer.State();
		for (const auto word : {result, state.InputCursor, state.InputCount, state.OutputCount, state.ResidentCapacity, state.MaximumLength, static_cast<std::uint32_t>(state.ScratchFlags), static_cast<std::uint32_t>(state.OutputActive), static_cast<std::uint32_t>(state.Failed), static_cast<std::uint32_t>(state.OutputAliasesInput), content}) {
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
			TestCandidateAcceptanceAndMarkGates();
			TestModifiedClassBlocking();
			TestOutputMergeAndFollowingInput();
			TestFailedFirstPublicationAndResidentBounds();
			TestSeparateFinalWordMapping();
			std::cout << "PASS 5 neutral recomposition groups\n";
		} else {
			throw std::runtime_error("unknown neutral recomposition proof arguments");
		}
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
