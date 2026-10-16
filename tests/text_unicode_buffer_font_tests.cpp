#include "sgre/text/recursive_decomposition.hpp"

#include "sgre/text/vector_font_normalization_font.hpp"
#include "vector_font_normalization_fixture.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

static std::uint32_t ReadFixtureBig(std::span<const std::uint8_t> bytes, std::size_t offset, std::size_t count) {
	std::uint32_t value = 0;
	for (std::size_t index = 0; index < count; ++index) {
		value = (value << 8) | bytes[offset + index];
	}

	return value;
}

static std::vector<std::uint8_t> MakeExpansionFont() {
	const auto initial = MakeFont(0);
	const auto count = ReadFixtureBig(initial, 4, 2);
	const std::array<std::string_view, 7> tags = {"head", "maxp", "hhea", "hmtx", "glyf", "loca", "cmap"};
	if (count != tags.size()) {
		throw std::runtime_error("authored expansion fixture table count changed");
	}
	std::vector<NamedTableFixture> tables(count);
	for (std::size_t index = 0; index < tables.size(); ++index) {
		const auto entry = 12 + index * 16;
		tables[index].Tag = tags[index];
		const auto offset = ReadFixtureBig(initial, entry + 8, 4);
		const auto length = ReadFixtureBig(initial, entry + 12, 4);
		tables[index].Bytes.assign(initial.begin() + offset, initial.begin() + offset + length);
		if (tables[index].Tag == "cmap") {
			const std::array<std::uint32_t, 7> points = {0x41, 0x301, 0x30a, 0x1100, 0x1161, 0x11a8, 0x25cc};
			auto& bytes = tables[index].Bytes;
			bytes.assign(12 + 16 + points.size() * 12, 0);
			WriteBig16(bytes, 2, 1);
			WriteBig16(bytes, 6, 3);
			WriteBig32(bytes, 8, 12);
			WriteBig16(bytes, 12, 12);
			WriteBig32(bytes, 16, static_cast<std::uint32_t>(bytes.size() - 12));
			WriteBig32(bytes, 24, static_cast<std::uint32_t>(points.size()));
			for (std::size_t at = 0; at < points.size(); ++at) {
				WriteBig32(bytes, 28 + at * 12, points[at]);
				WriteBig32(bytes, 32 + at * 12, points[at]);
				WriteBig32(bytes, 36 + at * 12, static_cast<std::uint32_t>(at + 1));
			}
		}
	}

	std::vector<std::uint8_t> bytes(12 + tables.size() * 16);
	WriteBig32(bytes, 0, 0x10000);
	WriteBig16(bytes, 4, static_cast<std::uint16_t>(tables.size()));
	for (std::size_t index = 0; index < tables.size(); ++index) {
		while (bytes.size() % 4 != 0) {
			bytes.push_back(0);
		}
		for (std::size_t character = 0; character < 4; ++character) {
			bytes[12 + index * 16 + character] = static_cast<std::uint8_t>(tables[index].Tag[character]);
		}
		WriteBig32(bytes, 20 + index * 16, static_cast<std::uint32_t>(bytes.size()));
		WriteBig32(bytes, 24 + index * 16, static_cast<std::uint32_t>(tables[index].Bytes.size()));
		bytes.insert(bytes.end(), tables[index].Bytes.begin(), tables[index].Bytes.end());
	}

	return bytes;
}

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static void TestSameGeneralOwnerAndFontBinding() {
	const auto bytes = std::make_shared<const std::vector<std::uint8_t>>(MakeFont(0));
	const sgre::resource::SfntFont reader(*bytes);
	sgre::text::VectorFontNormalizationFont font(bytes, reader);
	sgre::text::UnicodeNormalizationBuffer buffer;
	buffer.SetInputDirection(sgre::text::GlyphShapingDirection::LeftToRight);
	buffer.SetInputScript(0x4c61746e);
	buffer.AddUtf16(u"A\ufe00", 0, 2);
	buffer.InitializeInputUnicodeProperties();
	buffer.InitializeInputClusters(sgre::text::GlyphClusterLevel::MonotoneGraphemes);
	const auto original_direction = sgre::text::GlyphShapingDirection::LeftToRight;
	const auto adjusted = buffer.AdjustInputDirection();
	const sgre::text::VectorFontFeatureMap features;
	sgre::text::NeutralGlyphPreparationContext context;
	context.OriginalDirection = original_direction;
	context.BufferDirection = adjusted;
	const auto content = font.PrepareNeutralGlyphs(buffer, features, context);
	Require(content == sgre::text::NormalizedGlyphContent::GlyphIndices && buffer.InputMetadata().Content == sgre::text::UnicodeBufferContent::GlyphIndices && buffer.InputGlyphs().size() == 1 && buffer.InputGlyphs()[0].GlyphIndex == 1, "actual general owner passes selector consumption and GDEF entry through the same real font binding");
	buffer.ClearInput();
	Require(buffer.InputGlyphs().empty() && buffer.State().ResidentCapacity == 32, "engine clear reuses the same general owner after neutral publication");
}

static void TestGrowthDuringCanonicalEmission() {
	const auto bytes = std::make_shared<const std::vector<std::uint8_t>>(MakeExpansionFont());
	const sgre::resource::SfntFont reader(*bytes);
	sgre::text::VectorFontNormalizationFont font(bytes, reader);
	sgre::text::UnicodeNormalizationBuffer buffer;
	const std::u16string text(31, 0xc1);
	buffer.SetInputDirection(sgre::text::GlyphShapingDirection::LeftToRight);
	buffer.SetInputScript(0x4c61746e);
	buffer.AddUtf16(text, 0, static_cast<std::uint32_t>(text.size()));
	Require(buffer.State().ResidentCapacity == 32, "input begins just below the original first capacity boundary");
	buffer.InitializeInputUnicodeProperties();
	buffer.InitializeInputClusters(sgre::text::GlyphClusterLevel::MonotoneGraphemes);
	const auto adjusted = buffer.AdjustInputDirection();
	const sgre::text::VectorFontFeatureMap features;
	sgre::text::NeutralGlyphPreparationContext context;
	context.BufferDirection = adjusted;
	const auto content = font.PrepareNeutralGlyphs(buffer, features, context);
	Require(content == sgre::text::NormalizedGlyphContent::GlyphIndices && buffer.InputGlyphs().size() == 62 && buffer.State().ResidentCapacity == 80 && !buffer.State().Failed, "canonical emission grows both actual arrays across the resident boundary");
	for (std::size_t index = 0; index < buffer.InputGlyphs().size(); ++index) {
		Require(buffer.InputGlyphs()[index].GlyphIndex == (index % 2 == 0 ? 1 : 2) && buffer.InputGlyphs()[index].Cluster == index / 2, "unmapped precomposed character retains native emitted glyph order and original clusters");
	}
}

static bool ReadWord(std::uint32_t& value, bool allow_end = false) {
	std::array<char, 4> bytes;
	std::cin.read(bytes.data(), 4);
	if (allow_end && std::cin.gcount() == 0 && std::cin.eof()) {
		return false;
	}
	if (std::cin.gcount() != 4) {
		throw std::runtime_error("general font bridge message has a truncated word");
	}
	value = 0;
	for (std::size_t index = 0; index < bytes.size(); ++index) {
		value |= std::uint32_t(static_cast<unsigned char>(bytes[index])) << (index * 8);
	}

	return true;
}

static void WriteWord(std::uint32_t value) {
	for (std::uint32_t index = 0; index < 4; ++index) {
		std::cout.put(std::bit_cast<char>(static_cast<std::uint8_t>(value & 0xffU)));
		value >>= 8;
	}
}

static void WriteGlyph(const sgre::text::SubstitutionGlyph& glyph) {
	WriteWord(glyph.GlyphIndex);
	WriteWord(glyph.Mask);
	WriteWord(glyph.Cluster);
	WriteWord(std::uint32_t(glyph.GlyphProperties) | (std::uint32_t(glyph.PrivateState) << 16));
	WriteWord(glyph.CharacterState);
}

static void NativeMessages() {
	std::uint32_t mode = 0;
	while (ReadWord(mode, true)) {
		std::array<std::uint32_t, 4> fields;
		for (auto& value : fields) {
			ReadWord(value);
		}
		if (mode > 10 || fields[3] > 4096) {
			throw std::runtime_error("general font bridge profile or text exceeds its input limit");
		}
		const auto bytes = std::make_shared<const std::vector<std::uint8_t>>(mode == 10 ? MakeExpansionFont() : MakeFont(mode));
		const sgre::resource::SfntFont reader(*bytes);
		sgre::text::VectorFontNormalizationFont font(bytes, reader);
		std::u16string text(fields[3], 0);
		for (auto& unit : text) {
			std::uint32_t value = 0;
			ReadWord(value);
			if (value > 0xffff) {
				throw std::runtime_error("general font bridge message has an invalid UTF16 unit");
			}
			unit = static_cast<char16_t>(value);
		}
		sgre::text::UnicodeNormalizationBuffer buffer;
		const auto original_direction = static_cast<sgre::text::GlyphShapingDirection>(fields[1]);
		buffer.SetInputDirection(original_direction);
		buffer.SetInputScript(fields[2]);
		buffer.AddUtf16(text, 0, fields[3]);
		buffer.InitializeInputUnicodeProperties();
		buffer.InitializeInputClusters(static_cast<sgre::text::GlyphClusterLevel>(fields[0]));
		const auto adjusted = buffer.AdjustInputDirection();
		const sgre::text::VectorFontFeatureMap features;
		sgre::text::NeutralGlyphPreparationContext context;
		context.OriginalDirection = original_direction;
		context.BufferDirection = adjusted;
		context.ClusterLevel = static_cast<sgre::text::GlyphClusterLevel>(fields[0]);
		static_cast<void>(font.PrepareNeutralGlyphs(buffer, features, context));
		const auto state = buffer.State();
		const auto metadata = buffer.InputMetadata();
		for (const auto word : std::array<std::uint32_t, 14>{static_cast<std::uint32_t>(metadata.Content), state.InputCursor, state.InputCount, state.OutputCount, state.ResidentCapacity, state.MaximumLength, static_cast<std::uint32_t>(state.ScratchFlags), std::uint32_t(state.OutputActive), std::uint32_t(state.Failed), std::uint32_t(state.OutputAliasesInput), metadata.DirectionWord, metadata.ScriptTag, metadata.InputAllocationSlots, metadata.AuxiliaryAllocationSlots}) {
			WriteWord(word);
		}
		for (const auto& glyph : buffer.InputGlyphs()) {
			WriteGlyph(glyph);
		}
		for (const auto& glyph : buffer.OutputGlyphs()) {
			WriteGlyph(glyph);
		}
		std::cout.flush();
	}
}

int main(int argc, char** argv) {
	try {
		if (argc == 2 && std::string_view(argv[1]) == "--native") {
			NativeMessages();

			return 0;
		}
		if (argc == 2 && std::string_view(argv[1]) == "--expansion-fixture") {
			for (const auto byte : MakeExpansionFont()) {
				std::cout.put(std::bit_cast<char>(byte));
			}

			return 0;
		}
		if (argc != 1) {
			throw std::runtime_error("unknown general font bridge proof argument");
		}
		TestSameGeneralOwnerAndFontBinding();
		TestGrowthDuringCanonicalEmission();
		std::cout << "PASS 2 general-buffer/font bridge groups\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << "text_unicode_buffer_font_tests: " << error.what() << '\n';

		return 1;
	}
}
