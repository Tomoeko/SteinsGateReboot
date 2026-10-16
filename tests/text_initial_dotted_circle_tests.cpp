#include "sgre/text/recursive_decomposition.hpp"

#include "sgre/text/nominal_glyph.hpp"

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
	Require(profile < 4, "unknown initial dotted circle font profile");
	std::array<std::vector<std::uint8_t>, 6> tables;
	tables[0].resize(54);
	WriteBig(tables[0], 0, 0x10000, 4);
	WriteBig(tables[0], 12, 0x5f0f3cf5, 4);
	WriteBig(tables[0], 18, 1000, 2);
	tables[1].resize(6);
	WriteBig(tables[1], 0, 0x5000, 4);
	WriteBig(tables[1], 4, 8, 2);
	tables[2].resize(36);
	WriteBig(tables[2], 0, 0x10000, 4);
	WriteBig(tables[2], 34, 1, 2);
	tables[3].resize(18);
	WriteBig(tables[3], 0, 500, 2);
	tables[4] = {1, 0, 4, 4};
	const std::array<std::uint32_t, 4> points = {0x41, 0x300, 0x301, 0x25cc};
	const auto count = profile == 0 ? 3U : 4U;
	tables[5].resize(12 + 16 + count * 12);
	WriteBig(tables[5], 2, 1, 2);
	WriteBig(tables[5], 6, 3, 2);
	WriteBig(tables[5], 8, 12, 4);
	WriteBig(tables[5], 12, 12, 2);
	WriteBig(tables[5], 16, static_cast<std::uint32_t>(tables[5].size() - 12), 4);
	WriteBig(tables[5], 24, count, 4);
	for (std::uint32_t index = 0; index < count; ++index) {
		WriteBig(tables[5], 28 + index * 12, points[index], 4);
		WriteBig(tables[5], 32 + index * 12, points[index], 4);
		auto glyph = index + 1;
		if (index == 3 && profile == 2) {
			glyph = 0;
		} else if (index == 3 && profile == 3) {
			glyph = 7;
		}
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

static sgre::text::SubstitutionGlyph Glyph(std::uint32_t point, std::uint32_t category = 12) {
	sgre::text::SubstitutionGlyph glyph;
	glyph.GlyphIndex = point;
	glyph.Mask = 0xfdaabbee;
	glyph.Cluster = 0xffffffff;
	glyph.GlyphProperties = 0x1234;
	glyph.PrivateState = 0x5678;
	glyph.CharacterState = 0xabcde000 | category;

	return glyph;
}

static void TestEmissionAndPublication() {
	const auto bytes = MakeFont(1);
	const sgre::resource::SfntFont font(bytes);
	const std::array input = {Glyph(0x300), Glyph(0x41, 5)};
	sgre::text::UnicodeNormalizationBuffer buffer(input, 4);
	sgre::text::InitialDottedCircleContext context;
	context.TextFlags = 0xfffffffd;
	buffer.BeginOutput();
	buffer.CopyCurrentInput();
	buffer.InsertInitialDottedCircle(font, context);
	const auto state = buffer.State();
	Require(!state.OutputActive && !state.Failed && state.InputCursor == 0 && state.InputCount == 3 && state.OutputCount == 2 && !state.OutputAliasesInput, "initial dotted circle publication and cursor reset");
	const auto output = buffer.InputGlyphs();
	Require(output[0].GlyphIndex == 0x25cc && output[0].Mask == input[0].Mask && output[0].Cluster == input[0].Cluster && output[0].GlyphProperties == 0 && output[0].PrivateState == 0 && output[0].CharacterState < 0x10000, "dotted circle stays a codepoint with zero nominal word");
	Require(output[1] == input[0] && output[2] == input[1], "dotted circle copies all original record words");
	Require((static_cast<std::uint32_t>(state.ScratchFlags) & 1U) != 0, "dotted circle properties update scratch flags before output");
	buffer.InsertInitialDottedCircle(font, context);
	Require(buffer.State().InputCount == 3 && buffer.InputGlyphs()[0] == output[0], "published circle does not reinsert after category changes");
}

static void TestAvailabilityAndContextGates() {
	const std::array input = {Glyph(0x41)};
	for (std::uint32_t profile = 0; profile < 4; ++profile) {
		const auto bytes = MakeFont(profile);
		const sgre::resource::SfntFont font(bytes);
		for (const auto text_flags : {0U, 1U, 2U, 0xffffffffU}) {
			for (const auto previous : {0U, 1U, 5U, 0xffffffffU}) {
				sgre::text::UnicodeNormalizationBuffer buffer(input, 3, static_cast<sgre::text::UnicodeBufferScratchFlags>(0x8000));
				sgre::text::InitialDottedCircleContext context;
				context.TextFlags = text_flags;
				context.PrecedingContextLength = previous;
				buffer.InsertInitialDottedCircle(font, context);
				const auto accepted = (profile == 1 || profile == 3) && (text_flags & 1U) != 0 && previous == 0;
				Require(buffer.State().InputCount == (accepted ? 2U : 1U), "dotted circle context and zero/last-valid nominal availability gates");
			}
		}
	}
	const auto bytes = MakeFont(1);
	const sgre::resource::SfntFont font(bytes);
	sgre::text::InitialDottedCircleContext context;
	context.TextFlags = 1;
	for (std::uint32_t category = 0; category < 32; ++category) {
		const std::array records = {Glyph(0x300, category)};
		sgre::text::UnicodeNormalizationBuffer buffer(records, 3);
		buffer.InsertInitialDottedCircle(font, context);
		Require(buffer.State().InputCount == (category == 12 ? 2U : 1U), "dotted circle uses supplied category rather than codepoint classification");
	}
}

static void TestResidentFailureAndEmptyInput() {
	const auto bytes = MakeFont(1);
	const sgre::resource::SfntFont font(bytes);
	const std::array input = {Glyph(0x300), Glyph(0x301)};
	sgre::text::InitialDottedCircleContext context;
	context.TextFlags = 1;
	sgre::text::UnicodeNormalizationBuffer buffer(input, 2);
	buffer.InsertInitialDottedCircle(font, context);
	const auto state = buffer.State();
	Require(state.Failed && state.OutputActive && !state.OutputAliasesInput && state.InputCursor == 1 && state.InputCount == 2 && state.OutputCount == 2 && buffer.InputGlyphs()[0] == input[0] && buffer.OutputGlyphs()[0].GlyphIndex == 0x25cc && buffer.OutputGlyphs()[1] == input[0], "failed last copy retains native partial output without publication");
	buffer.InsertInitialDottedCircle(font, context);
	Require(buffer.State().Failed && buffer.State().InputCursor == 0 && buffer.State().OutputCount == 1 && buffer.OutputGlyphs()[0].GlyphIndex == 0x25cc, "sticky failure still permits fitting append before suppressing copies and publication");
	sgre::text::UnicodeNormalizationBuffer zero(std::span<const sgre::text::SubstitutionGlyph>(), 0);
	context.TextFlags = 0;
	zero.InsertInitialDottedCircle(font, context);
	Require(zero.State().InputCount == 0, "disabled empty input is unchanged");
	context.TextFlags = 1;
	bool rejected = false;
	try {
		zero.InsertInitialDottedCircle(font, context);
	} catch (const std::runtime_error&) {
		rejected = true;
	}
	Require(rejected, "enabled empty input violates native live-first-record prerequisite");
}

static std::uint32_t ReadWord() {
	std::uint32_t result = 0;
	for (std::uint32_t shift = 0; shift < 32; shift += 8) {
		const auto value = std::cin.get();
		Require(value != std::char_traits<char>::eof(), "truncated dotted circle proof word");
		result |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(value)) << shift;
	}

	return result;
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
		WriteWord(static_cast<std::uint32_t>(glyph.GlyphProperties) | (static_cast<std::uint32_t>(glyph.PrivateState) << 16));
		WriteWord(glyph.CharacterState);
	}
}

static void RunNative() {
	const auto length = ReadWord();
	Require(length > 0 && length <= 32 * 1024 * 1024, "dotted circle proof font exceeds limit");
	std::vector<std::uint8_t> bytes(length);
	for (auto& byte : bytes) {
		const auto value = std::cin.get();
		Require(value != std::char_traits<char>::eof(), "truncated dotted circle proof font");
		byte = static_cast<std::uint8_t>(value);
	}
	const sgre::resource::SfntFont font(bytes);
	while (std::cin.peek() != std::char_traits<char>::eof()) {
		const auto maximum = ReadWord();
		auto flags = static_cast<sgre::text::UnicodeBufferScratchFlags>(ReadWord());
		sgre::text::InitialDottedCircleContext context;
		context.TextFlags = ReadWord();
		context.PrecedingContextLength = ReadWord();
		const auto initialize = ReadWord();
		const auto repeats = ReadWord();
		const auto count = ReadWord();
		const auto preface_count = ReadWord();
		Require(maximum <= 64 && count <= maximum + 1 && initialize <= 1 && repeats > 0 && repeats <= 3 && preface_count <= 8, "dotted circle proof case exceeds bounds");
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
		for (std::uint32_t index = 0; index < preface_count; ++index) {
			switch (ReadWord()) {
			case 0:
				buffer.BeginOutput();
				break;
			case 2:
				buffer.CopyCurrentInput();
				break;
			case 3:
				(void)buffer.PublishOutput();
				break;
			default:
				throw std::runtime_error("unknown dotted circle proof preface");
			}
		}
		for (std::uint32_t index = 0; index < repeats; ++index) {
			buffer.InsertInitialDottedCircle(font, context);
		}
		const auto state = buffer.State();
		for (const auto word : {state.Failed ? 0U : 1U, state.InputCursor, state.InputCount, state.OutputCount, state.ResidentCapacity, state.MaximumLength, static_cast<std::uint32_t>(state.ScratchFlags), static_cast<std::uint32_t>(state.OutputActive), static_cast<std::uint32_t>(state.Failed), static_cast<std::uint32_t>(state.OutputAliasesInput)}) {
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
			TestEmissionAndPublication();
			TestAvailabilityAndContextGates();
			TestResidentFailureAndEmptyInput();
			std::cout << "PASS 3 initial dotted circle groups\n";
		} else {
			throw std::runtime_error("unknown initial dotted circle proof arguments");
		}
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
