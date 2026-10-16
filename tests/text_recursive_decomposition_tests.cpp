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
	const std::array<std::uint32_t, 18> codepoints = {0x3f, 0x41, 0xc5, 0xe5, 0x300, 0x301, 0x308, 0x30a, 0x323, 0x327, 0x3b1, 0x1100, 0x1161, 0x11a8, 0xac00, 0x1d157, 0x1d165, 0x1f600};
	std::vector<std::uint32_t> selected;
	for (const auto codepoint : codepoints) {
		if ((profile == 1 && codepoint == 0x30a) || (profile == 2 && codepoint == 0x301) || (profile == 3 && codepoint == 0x41)) {
			continue;
		}
		selected.push_back(codepoint);
	}

	std::array<std::vector<std::uint8_t>, 6> tables;
	tables[0].resize(54);
	WriteBig(tables[0], 0, 0x10000, 4);
	WriteBig(tables[0], 12, 0x5f0f3cf5, 4);
	WriteBig(tables[0], 18, 1000, 2);
	tables[1].resize(6);
	WriteBig(tables[1], 0, 0x5000, 4);
	WriteBig(tables[1], 4, 32, 2);
	tables[2].resize(36);
	WriteBig(tables[2], 0, 0x10000, 4);
	WriteBig(tables[2], 34, 1, 2);
	tables[3].resize(66);
	WriteBig(tables[3], 0, 500, 2);
	tables[4].resize(4);
	tables[4][0] = 1;
	tables[4][2] = 4;
	tables[4][3] = 4;
	tables[5].resize(12 + 16 + selected.size() * 12);
	WriteBig(tables[5], 2, 1, 2);
	WriteBig(tables[5], 6, 3, 2);
	WriteBig(tables[5], 8, 12, 4);
	WriteBig(tables[5], 12, 12, 2);
	WriteBig(tables[5], 16, static_cast<std::uint32_t>(tables[5].size() - 12), 4);
	WriteBig(tables[5], 24, static_cast<std::uint32_t>(selected.size()), 4);
	for (std::size_t index = 0; index < selected.size(); ++index) {
		const auto found = std::find(codepoints.begin(), codepoints.end(), selected[index]);
		const auto glyph = static_cast<std::uint32_t>(found - codepoints.begin() + 1);
		WriteBig(tables[5], 28 + index * 12, selected[index], 4);
		WriteBig(tables[5], 32 + index * 12, selected[index], 4);
		WriteBig(tables[5], 36 + index * 12, glyph, 4);
	}

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

static sgre::text::SubstitutionGlyph Glyph(std::uint32_t codepoint) {
	sgre::text::SubstitutionGlyph result;
	result.GlyphIndex = codepoint;
	result.Mask = 0xffaaccef;
	result.Cluster = 7;
	result.GlyphProperties = 0x1234;
	result.PrivateState = 0xabcd;
	result.CharacterState = 0xcafe4321;

	return result;
}

static std::uint32_t GlyphWord(const sgre::text::SubstitutionGlyph& glyph) {
	return glyph.GlyphProperties | (static_cast<std::uint32_t>(glyph.PrivateState) << 16);
}

static void TestRecursiveMappedOrderAndPublication() {
	const auto bytes = MakeFont(0);
	const sgre::resource::SfntFont font(bytes);
	const std::array input = {Glyph(0x1fa)};
	for (const auto preference : {sgre::text::UnicodeDecompositionPreference::RecursiveFirst, sgre::text::UnicodeDecompositionPreference::MappedFirst}) {
		sgre::text::UnicodeNormalizationBuffer buffer(input, 8);
		buffer.BeginOutput();
		const auto count = buffer.TryDecomposeCurrentInput(font, preference);
		Require(count == (preference == sgre::text::UnicodeDecompositionPreference::RecursiveFirst ? 3U : 2U), "recursive versus mapped emission count differs");
		const auto output = buffer.OutputGlyphs();
		Require(output.front().GlyphIndex == (count == 3 ? 0x41U : 0xc5U) && output.back().GlyphIndex == 0x301, "canonical output order differs");
		if (count == 3) {
			Require(output[1].GlyphIndex == 0x30a && GlyphWord(output[1]) == 8 && (output[1].CharacterState & 0xffffU) == 0xe60c, "combining emission or glyph property refresh differs");
		}
		for (const auto& glyph : output) {
			Require(glyph.Mask == input[0].Mask && glyph.Cluster == 7 && (glyph.CharacterState & 0xffff0000U) == 0xcafe0000, "emission lost inherited record state");
		}
		Require(GlyphWord(buffer.InputGlyphs()[0]) == 6 && buffer.State().InputCursor == 1 && !buffer.State().OutputAliasesInput, "current nominal word, input consumption, or alternate-buffer transition differs");
		Require(buffer.PublishOutput(), "successful canonical output was not published");
		Require(buffer.State().InputCount == count && buffer.State().OutputCount == 1 && buffer.State().InputCursor == 0 && !buffer.State().OutputActive && buffer.OutputGlyphs()[0].GlyphIndex == 0x1fa, "published counts or retained input differs");
	}
}

static void TestMissingSecondAndFirstFallback() {
	const std::array input = {Glyph(0x1fa)};
	for (const auto profile : {1U, 2U, 3U}) {
		const auto bytes = MakeFont(profile);
		const sgre::resource::SfntFont font(bytes);
		sgre::text::UnicodeNormalizationBuffer buffer(input, 8);
		buffer.BeginOutput();
		const auto count = buffer.TryDecomposeCurrentInput(font, sgre::text::UnicodeDecompositionPreference::RecursiveFirst);
		if (profile == 2) {
			Require(count == 0 && buffer.State().InputCursor == 0 && buffer.State().OutputCount == 0 && GlyphWord(buffer.InputGlyphs()[0]) == 0xabcd1234, "missing second glyph emitted or consumed input");
			buffer.CopyCurrentInput();
			Require(buffer.State().OutputAliasesInput && buffer.OutputGlyphs()[0].GlyphIndex == 0x1fa, "unchanged missing decomposition was not copied in place");
		} else {
			Require(count == 2 && buffer.OutputGlyphs()[0].GlyphIndex == 0xc5 && buffer.OutputGlyphs()[1].GlyphIndex == 0x301, "failed deeper decomposition did not retain mapped-first fallback");
		}
	}
}

static void TestStickyLengthFailureAndPartialRecords() {
	const auto bytes = MakeFont(0);
	const sgre::resource::SfntFont font(bytes);
	const std::array input = {Glyph(0x1fa)};
	for (const auto limit : {0U, 1U, 2U}) {
		sgre::text::UnicodeNormalizationBuffer buffer(input, limit);
		buffer.BeginOutput();
		Require(buffer.TryDecomposeCurrentInput(font, sgre::text::UnicodeDecompositionPreference::RecursiveFirst) == 3, "logical emission count was replaced by capacity failure");
		const auto state = buffer.State();
		Require(state.Failed && state.OutputActive && state.InputCursor == 1 && state.OutputCount == limit && GlyphWord(buffer.InputGlyphs()[0]) == 6, "partial output or final nominal scratch word differs on failure");
		if (limit == 0) {
			Require(state.OutputAliasesInput && (buffer.InputGlyphs()[0].CharacterState & 0xffffU) == 9, "zero-output failure did not refresh aliased record zero");
		}
		Require(!buffer.PublishOutput() && buffer.State().InputCount == 1 && buffer.State().OutputCount == limit && buffer.State().InputCursor == 1, "failed output changed published pointers or counts");
	}

	const std::array mixed = {Glyph(0x1fa), Glyph(0x301)};
	sgre::text::UnicodeNormalizationBuffer failed_copy(mixed, 2);
	failed_copy.BeginOutput();
	Require(failed_copy.TryDecomposeCurrentInput(font, sgre::text::UnicodeDecompositionPreference::RecursiveFirst) == 3, "failure fixture did not emit logically");
	Require(failed_copy.TryDecomposeCurrentInput(font, sgre::text::UnicodeDecompositionPreference::RecursiveFirst) == 0, "combining leaf unexpectedly decomposed");
	failed_copy.CopyCurrentInput();
	Require(failed_copy.State().InputCursor == 1 && failed_copy.State().OutputCount == 2 && failed_copy.State().Failed, "failed passthrough copy consumed input");
}

static void TestResidentPhaseAndInvalidBounds() {
	const std::array input = {Glyph(0x41), Glyph(0x3f)};
	sgre::text::UnicodeNormalizationBuffer buffer(input, 4);
	buffer.CopyCurrentInput();
	buffer.BeginOutput();
	buffer.CopyCurrentInput();
	Require(buffer.State().OutputAliasesInput && buffer.State().OutputCount == 1 && buffer.InputGlyphs()[0].GlyphIndex == 0x3f, "begin-output reset cursor or alias copy changed");
	Require(buffer.PublishOutput() && buffer.InputGlyphs().size() == 1 && buffer.OutputGlyphs().size() == 2, "aliased publication lost previous count");
	bool rejected = false;
	try {
		const sgre::text::UnicodeNormalizationBuffer invalid(input, 0);
		static_cast<void>(invalid);
	} catch (const std::runtime_error&) {
		rejected = true;
	}
	Require(rejected, "resident input count exceeded its storage");
}

static std::uint32_t ReadWord() {
	std::uint32_t value = 0;
	for (std::uint32_t shift = 0; shift < 32; shift += 8) {
		const auto byte = std::cin.get();
		Require(byte != std::char_traits<char>::eof(), "truncated recursive normalization proof input");
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

static void WriteSnapshot(const sgre::text::UnicodeNormalizationBuffer& buffer, std::uint32_t result) {
	const auto state = buffer.State();
	WriteWord(result);
	WriteWord(state.InputCursor);
	WriteWord(state.InputCount);
	WriteWord(state.OutputCount);
	WriteWord(state.ResidentCapacity);
	WriteWord(state.MaximumLength);
	WriteWord(static_cast<std::uint32_t>(state.ScratchFlags));
	WriteWord(state.OutputActive);
	WriteWord(state.Failed);
	WriteWord(state.OutputAliasesInput);
	WriteGlyphs(buffer.InputGlyphs());
	WriteGlyphs(buffer.OutputGlyphs());
}

static void RunNativeDriver() {
	const auto font_size = ReadWord();
	Require(font_size <= 64 * 1024 * 1024, "recursive normalization proof font exceeds its byte limit");
	std::vector<std::uint8_t> bytes(font_size);
	for (auto& byte : bytes) {
		const auto value = std::cin.get();
		Require(value != std::char_traits<char>::eof(), "truncated recursive normalization proof font");
		byte = static_cast<std::uint8_t>(value);
	}
	const sgre::resource::SfntFont font(bytes);
	while (std::cin.peek() != std::char_traits<char>::eof()) {
		const auto maximum_length = ReadWord();
		const auto flags = ReadWord();
		const auto input_count = ReadWord();
		const auto operation_count = ReadWord();
		Require(input_count <= 4096 && operation_count <= 4096, "recursive normalization proof counts exceed their limits");
		std::vector<sgre::text::SubstitutionGlyph> glyphs(input_count);
		for (auto& glyph : glyphs) {
			glyph.GlyphIndex = ReadWord();
			glyph.Mask = ReadWord();
			glyph.Cluster = ReadWord();
			const auto word = ReadWord();
			glyph.GlyphProperties = static_cast<std::uint16_t>(word & 0xffffU);
			glyph.PrivateState = static_cast<std::uint16_t>(word >> 16);
			glyph.CharacterState = ReadWord();
		}
		sgre::text::UnicodeNormalizationBuffer buffer(glyphs, maximum_length, static_cast<sgre::text::UnicodeBufferScratchFlags>(flags));
		for (std::uint32_t index = 0; index < operation_count; ++index) {
			const auto operation = ReadWord();
			const auto parameter = ReadWord();
			std::uint32_t result = 1;
			switch (operation) {
			case 0:
				buffer.BeginOutput();
				break;
			case 1:
				Require(parameter <= 1, "unsupported recursive normalization preference");
				result = buffer.TryDecomposeCurrentInput(font, static_cast<sgre::text::UnicodeDecompositionPreference>(parameter));
				break;
			case 2:
				buffer.CopyCurrentInput();
				break;
			case 3:
				result = buffer.PublishOutput();
				break;
			default:
				throw std::runtime_error("unsupported recursive normalization operation");
			}
			WriteSnapshot(buffer, result);
		}
		std::cout.flush();
	}
}

int main(int argc, char** argv) {
	try {
		if (argc == 2 && std::string_view(argv[1]) == "--native") {
			RunNativeDriver();

			return 0;
		}
		if (argc == 3 && std::string_view(argv[1]) == "--fixture") {
			Require(argv[2][0] >= '0' && argv[2][0] <= '3' && argv[2][1] == '\0', "unsupported recursive normalization font profile");
			const auto bytes = MakeFont(static_cast<std::uint32_t>(argv[2][0] - '0'));
			WriteWord(static_cast<std::uint32_t>(bytes.size()));
			for (const auto byte : bytes) {
				std::cout.put(static_cast<char>(byte));
			}

			return 0;
		}
		Require(argc == 1, "unsupported recursive normalization arguments");
		TestRecursiveMappedOrderAndPublication();
		TestMissingSecondAndFirstFallback();
		TestStickyLengthFailureAndPartialRecords();
		TestResidentPhaseAndInvalidBounds();
		std::cout << "Unicode recursive emission owner: four groups passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
