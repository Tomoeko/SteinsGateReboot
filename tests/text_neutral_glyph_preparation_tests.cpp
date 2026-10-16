#include "sgre/text/neutral_glyph_preparation.hpp"
#include "sgre/text/nominal_glyph.hpp"
#include "sgre/text/shaping_font_map.hpp"

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
	const std::array<std::uint32_t, 23> points = {0x20, 0x28, 0x29, 0x3c, 0x3e, 0x41, 0x42, 0xc5, 0x1fa, 0x300, 0x301, 0x30a, 0x327, 0x1100, 0x1161, 0x11a8, 0x2009, 0x2010, 0xac00, 0x1d157, 0x1d165, 0x1f600, 0xe0100};
	std::vector<std::uint32_t> selected;
	for (const auto point : points) {
		if ((profile != 3 && profile != 5 && (point == 0x1fa || point == 0x2009 || point == 0xe0100)) || (profile == 1 && point == 0x20) || (profile == 2 && point == 0x2010) || (profile == 4 && point == 0xc5)) {
			continue;
		}
		selected.push_back(point);
	}

	std::array<std::vector<std::uint8_t>, 8> tables;
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

	const std::array<std::string_view, 5> feature_tags = {"dnom", "frac", "numr", "rtlm", "test"};
	for (std::size_t table = 6; table < 8; ++table) {
		tables[table].resize(92);
		WriteBig(tables[table], 0, 0x10000, 4);
		WriteBig(tables[table], 4, 10, 2);
		WriteBig(tables[table], 6, 38, 2);
		WriteBig(tables[table], 8, 90, 2);
		WriteBig(tables[table], 10, 1, 2);
		WriteBig(tables[table], 12, 0x6c61746e, 4);
		WriteBig(tables[table], 16, 8, 2);
		WriteBig(tables[table], 18, 4, 2);
		WriteBig(tables[table], 24, 0xffff, 2);
		WriteBig(tables[table], 26, 5, 2);
		WriteBig(tables[table], 38, 5, 2);
		for (std::size_t index = 0; index < feature_tags.size(); ++index) {
			WriteBig(tables[table], 28 + index * 2, static_cast<std::uint32_t>(index), 2);
			for (std::size_t byte = 0; byte < 4; ++byte) {
				tables[table][40 + index * 6 + byte] = static_cast<std::uint8_t>(feature_tags[index][byte]);
			}
			WriteBig(tables[table], 44 + index * 6, static_cast<std::uint32_t>(32 + index * 4), 2);
		}
	}
	const std::array<std::string_view, 8> tags = {"head", "maxp", "hhea", "hmtx", "CFF ", "cmap", "GSUB", "GPOS"};
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

static sgre::text::VectorFontFeatureMap Features(const sgre::resource::SfntFont& font, sgre::text::GlyphShapingDirection direction, std::span<const sgre::text::VectorFontFeatureRequest> requests = {}) {
	sgre::text::VectorFontConfiguration configuration;
	configuration.Direction = static_cast<sgre::text::VectorFontDirection>(direction);
	configuration.HasLanguage = false;

	return sgre::text::PrepareVectorFontFeatureMap(font, configuration, requests);
}

static std::uint32_t OneMask(const sgre::text::VectorFontFeatureMap& features, std::string_view tag) {
	for (const auto& feature : features.Features) {
		if (feature.Tag == tag) {
			return feature.OneMask;
		}
	}

	return 0;
}

static void TestMirrorAcceptanceAndDirectionSeparation() {
	const auto bytes = MakeFont(0);
	const sgre::resource::SfntFont font(bytes);
	const auto features = Features(font, sgre::text::GlyphShapingDirection::RightToLeft);
	sgre::text::NeutralGlyphPreparationContext context;
	context.OriginalDirection = sgre::text::GlyphShapingDirection::RightToLeft;
	context.BufferDirection = sgre::text::GlyphShapingDirection::LeftToRight;
	std::array input = {Glyph(0x28), Glyph(0x41), Glyph(0x2264)};
	const auto scratch = Initialize(input);
	for (auto& glyph : input) {
		glyph.Mask = features.GlobalMask;
	}
	sgre::text::UnicodeNormalizationBuffer buffer(input, 8, scratch);
	Require(sgre::text::PrepareNeutralGlyphIndices(buffer, font, features, context) == sgre::text::NormalizedGlyphContent::GlyphIndices, "caller did not publish glyph content tag2");
	const auto output = buffer.InputGlyphs();
	Require(output[0].GlyphIndex == sgre::text::QueryVectorFontNominalGlyph(font, 0x29).value() && output[1].GlyphIndex == sgre::text::QueryVectorFontNominalGlyph(font, 0x41).value() && output[2].GlyphIndex == 0, "mirror nominal acceptance or missing-glyph policy differs");
	Require((output[0].Mask & OneMask(features, "rtlm")) == 0 && (output[1].Mask & OneMask(features, "rtlm")) != 0 && (output[2].Mask & OneMask(features, "rtlm")) != 0, "rtlm mask was not retained for unchanged and rejected mirrors");
}

static void TestFractionMasksAndUserOrder() {
	const auto bytes = MakeFont(0);
	const sgre::resource::SfntFont font(bytes);
	sgre::text::VectorFontFeatureRequest request;
	request.Tag = "numr";
	request.Value = 0;
	request.StartCluster = 2;
	request.EndCluster = 4;
	const std::array requests = {request};
	const auto features = Features(font, sgre::text::GlyphShapingDirection::LeftToRight, requests);
	std::array input = {Glyph(0x31, 2), Glyph(0x2044, 3), Glyph(0x32, 4)};
	const auto scratch = Initialize(input);
	for (auto& glyph : input) {
		glyph.Mask = features.GlobalMask;
	}
	sgre::text::NeutralGlyphPreparationContext context;
	sgre::text::UnicodeNormalizationBuffer buffer(input, 8, scratch);
	static_cast<void>(sgre::text::PrepareNeutralGlyphIndices(buffer, font, features, context, requests));
	const auto output = buffer.InputGlyphs();
	Require((output[0].Mask & OneMask(features, "frac")) != 0 && (output[0].Mask & OneMask(features, "numr")) == 0 && (output[1].Mask & OneMask(features, "frac")) != 0 && (output[2].Mask & OneMask(features, "dnom")) != 0, "user masks did not follow fraction masks with half-open cluster bounds");
	Require((output[0].Mask & 1U) == 0 && (output[1].Mask & 1U) != 0 && (output[2].Mask & 1U) != 0 && (static_cast<std::uint32_t>(buffer.State().ScratchFlags) & 0x10U) != 0, "fraction range unsafe flags or clusters differ");
}

static void TestFallbackMarkClasses() {
	const auto bytes = MakeFont(0);
	const sgre::resource::SfntFont font(bytes);
	const auto features = Features(font, sgre::text::GlyphShapingDirection::LeftToRight);
	std::array input = {Glyph(0xe31), Glyph(0xe3a), Glyph(0xebc), Glyph(0x9999), Glyph(0xaaaa)};
	for (auto& glyph : input) {
		glyph.Mask = features.GlobalMask;
		glyph.CharacterState = 0xcafe000c;
	}
	input[1].CharacterState |= 3U << 8;
	input[3].CharacterState |= 13U << 8;
	input[4].CharacterState |= 250U << 8;
	sgre::text::NeutralGlyphPreparationContext context;
	context.FallbackMarkPositioning = true;
	sgre::text::UnicodeNormalizationBuffer buffer(input, 8);
	static_cast<void>(sgre::text::PrepareNeutralGlyphIndices(buffer, font, features, context));
	const auto output = buffer.InputGlyphs();
	const std::array<std::uint32_t, 5> classes = {232, 222, 220, 214, 250};
	Require(output.size() == classes.size(), "fallback mark-class rewriting removed records");
	for (std::size_t index = 0; index < classes.size(); ++index) {
		Require((output[index].CharacterState & 0xffffU) == (classes[index] << 8 | 12U) && (output[index].CharacterState & 0xffff0000U) == 0xcafe0000U, "Thai/Lao or canonical fallback mark class differs");
	}
}

static void TestPreflightAndFailedPublication() {
	const auto bytes = MakeFont(0);
	const sgre::resource::SfntFont font(bytes);
	const auto features = Features(font, sgre::text::GlyphShapingDirection::LeftToRight);
	std::array input = {Glyph(0x1fa)};
	const auto scratch = Initialize(input);
	sgre::text::UnicodeNormalizationBuffer failed(input, 1, scratch);
	sgre::text::NeutralGlyphPreparationContext context;
	Require(sgre::text::PrepareNeutralGlyphIndices(failed, font, features, context) == sgre::text::NormalizedGlyphContent::GlyphIndices && failed.State().Failed && failed.State().OutputActive, "sticky error prevented native final mapping or changed publication");
	context.ClusterLevel = static_cast<sgre::text::GlyphClusterLevel>(3);
	sgre::text::UnicodeNormalizationBuffer invalid(input, 8, scratch);
	bool rejected = false;
	try {
		static_cast<void>(sgre::text::PrepareNeutralGlyphIndices(invalid, font, features, context));
	} catch (const std::runtime_error&) {
		rejected = true;
	}
	Require(rejected && invalid.InputGlyphs()[0].GlyphIndex == input[0].GlyphIndex && !invalid.State().OutputActive, "invalid caller context mutated records before rejection");
}

static std::uint32_t ReadWord() {
	std::uint32_t value = 0;
	for (std::uint32_t shift = 0; shift < 32; shift += 8) {
		const auto byte = std::cin.get();
		Require(byte != std::char_traits<char>::eof(), "truncated neutral caller proof input");
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
	Require(font_length > 0 && font_length <= 32 * 1024 * 1024, "neutral caller proof font exceeds limit");
	std::vector<std::uint8_t> bytes(font_length);
	for (auto& byte : bytes) {
		const auto value = std::cin.get();
		Require(value != std::char_traits<char>::eof(), "truncated neutral caller font");
		byte = static_cast<std::uint8_t>(value);
	}
	const sgre::resource::SfntFont font(bytes);
	const auto original_direction = static_cast<sgre::text::GlyphShapingDirection>(ReadWord());
	const auto request_count = ReadWord();
	Require(request_count <= 128, "neutral caller proof request count exceeds limit");
	std::vector<sgre::text::VectorFontFeatureRequest> requests(request_count);
	for (auto& request : requests) {
		const auto tag = ReadWord();
		for (std::uint32_t index = 0; index < 4; ++index) {
			request.Tag.push_back(static_cast<char>((tag >> (24 - index * 8)) & 0xffU));
		}
		request.Value = ReadWord();
		request.StartCluster = ReadWord();
		request.EndCluster = ReadWord();
	}
	const auto features = Features(font, original_direction, requests);
	WriteWord(features.GlobalMask);
	WriteWord(static_cast<std::uint32_t>(features.Features.size()));
	for (const auto& feature : features.Features) {
		std::uint32_t tag = 0;
		for (const auto byte : feature.Tag) {
			tag = (tag << 8) | static_cast<std::uint8_t>(byte);
		}
		for (const auto value : {tag, feature.Shift, feature.Mask, feature.OneMask}) {
			WriteWord(value);
		}
	}
	std::cout.flush();
	while (std::cin.peek() != std::char_traits<char>::eof()) {
		const auto maximum = ReadWord();
		auto flags = static_cast<sgre::text::UnicodeBufferScratchFlags>(ReadWord());
		sgre::text::NeutralGlyphPreparationContext context;
		context.OriginalDirection = original_direction;
		context.ClusterLevel = static_cast<sgre::text::GlyphClusterLevel>(ReadWord());
		const auto initialize = ReadWord();
		context.BufferDirection = static_cast<sgre::text::GlyphShapingDirection>(ReadWord());
		const auto fallback = ReadWord();
		context.FallbackMarkPositioning = fallback != 0;
		const auto count = ReadWord();
		Require(maximum <= 4096 && count <= maximum + 1 && initialize <= 1 && fallback <= 1, "neutral caller proof bounds invalid");
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
		std::uint32_t result = 1;
		std::uint32_t content = 0;
		try {
			content = static_cast<std::uint32_t>(sgre::text::PrepareNeutralGlyphIndices(buffer, font, features, context, requests));
			result = buffer.State().Failed ? 0U : 1U;
		} catch (const std::runtime_error& error) {
			if (std::string_view(error.what()).find("extra-selector copy cannot advance") != std::string_view::npos) {
				result = 2;
			} else if (std::string_view(error.what()).find("copy exceeds resident input records") != std::string_view::npos) {
				result = 3;
			} else {
				throw;
			}
		}
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
			TestMirrorAcceptanceAndDirectionSeparation();
			TestFractionMasksAndUserOrder();
			TestFallbackMarkClasses();
			TestPreflightAndFailedPublication();
			std::cout << "PASS 4 neutral glyph caller groups\n";
		} else {
			throw std::runtime_error("unknown neutral caller proof arguments");
		}
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
