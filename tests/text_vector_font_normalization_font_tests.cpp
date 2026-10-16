#include "sgre/text/vector_font_normalization_font.hpp"

#include "sgre/text/blob_font_layout_cache.hpp"
#include "sgre/text/unicode_preprocess.hpp"
#include "vector_font_normalization_fixture.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static void TestLiveReaderAndNativeQueryProfile() {
	for (std::uint32_t mode : {0U, 1U, 2U}) {
		const auto pin = std::make_shared<const std::vector<std::uint8_t>>(MakeFont(mode));
		const sgre::resource::SfntFont reader(*pin);
		sgre::text::VectorFontNormalizationFont font(pin, reader);
		Require(&font.Font() == &reader && font.Face().WholeFontBytes().data() == pin->data(), "actual callback userdata borrows the same live reader and face resource");
		Require(font.NominalGlyph(0x41).value_or(0) == (mode == 1 ? 4U : 1U) && !font.NominalGlyph(0xffffffff), "installed Unicode nominal profile retains full-width missing policy");
		Require(font.VariationGlyph(0, 0xfe01).value_or(0) == 0xffff && !font.NominalGlyph(0), "explicit variation glyph bypasses nominal glyph-count clamp");
		Require(font.VariationGlyph(0x41, 0xfe00) == font.NominalGlyph(0x41) && !font.VariationGlyph(0x44, 0xfe00), "default variation results do not use explicit fallback after nominal zero");
		Require(font.Face().PrepareLayout() && font.Face().LayoutCache() != nullptr, "actual resource face prepares its real admitted layout cache");
	}
}

static void TestBindingIdentityAndScopedPins() {
	auto pin = std::make_shared<const std::vector<std::uint8_t>>(MakeFont(0));
	const auto duplicate = std::make_shared<const std::vector<std::uint8_t>>(*pin);
	const sgre::resource::SfntFont reader(*pin);
	bool rejected = false;
	try {
		sgre::text::VectorFontNormalizationFont mismatch(duplicate, reader);
	} catch (const std::runtime_error&) {
		rejected = true;
	}
	Require(rejected, "a different resource allocation cannot silently supply a borrowed reader's face");
	rejected = false;
	try {
		sgre::text::VectorFontNormalizationFont absent({}, reader);
	} catch (const std::runtime_error&) {
		rejected = true;
	}
	Require(rejected, "absent bytes do not produce a usable normalization binding");
	const std::weak_ptr<const std::vector<std::uint8_t>> observer = pin;
	sgre::text::FontFaceTableBlob retained;
	{
		sgre::text::VectorFontNormalizationFont font(pin, reader);
		retained = font.Face().ReferenceTable(0);
		pin.reset();
		Require(!observer.expired() && font.NominalGlyph(0x41) == 1, "face resource pin protects the borrowed reader while callbacks run");
	}
	Require(!observer.expired() && retained.Length() != 0, "an external table pin retains resource bytes after the font binding retires");
	retained = {};
	Require(observer.expired(), "last table release retires the original resource allocation");
}

static void TestOwnedNeutralEntry() {
	const auto pin = std::make_shared<const std::vector<std::uint8_t>>(MakeFont(0));
	const sgre::resource::SfntFont reader(*pin);
	sgre::text::VectorFontNormalizationFont font(pin, reader);
	std::array<sgre::text::SubstitutionGlyph, 2> records;
	records[0].GlyphIndex = 0x41;
	records[0].Cluster = 7;
	records[0].Mask = 0x13570001;
	records[1].GlyphIndex = 0xfe00;
	records[1].Cluster = 8;
	records[1].Mask = 0x24680001;
	const auto flags = sgre::text::InitializeUnicodeGlyphProperties(records);
	sgre::text::UnicodeNormalizationBuffer buffer(records, 8, flags);
	sgre::text::VectorFontFeatureMap features;
	sgre::text::NeutralGlyphPreparationContext context;
	const auto content = font.PrepareNeutralGlyphs(buffer, features, context);
	Require(content == sgre::text::NormalizedGlyphContent::GlyphIndices && buffer.InputGlyphs().size() == 1 && buffer.InputGlyphs()[0].GlyphIndex == 1, "real nominal/variation binding supplies selector consumption and final nominal mapping");
	Require(buffer.InputGlyphs()[0].GlyphProperties == 2 && buffer.InputGlyphs()[0].PrivateState == 0, "missing GDEF classes reach the actual final category-based admission");
	Require(font.Face().LayoutState() == sgre::text::FontFaceLayoutState::Ready && !font.Face().HasPositioning(), "neutral fallback is derived from the genuine effective cache");
	for (std::uint32_t mode : {7U, 8U, 9U}) {
		const auto layout_pin = std::make_shared<const std::vector<std::uint8_t>>(MakeFont(mode));
		const sgre::resource::SfntFont layout_reader(*layout_pin);
		sgre::text::VectorFontNormalizationFont layout_font(layout_pin, layout_reader);
		std::array<sgre::text::SubstitutionGlyph, 1> marks;
		marks[0].GlyphIndex = 0xe31;
		marks[0].CharacterState = 0xaabb140c;
		marks[0].GlyphProperties = 0xffff;
		marks[0].PrivateState = 0xffff;
		sgre::text::UnicodeNormalizationBuffer mark_buffer(marks, 8, sgre::text::UnicodeBufferScratchFlags::HasNonAscii);
		const auto mark_content = layout_font.PrepareNeutralGlyphs(mark_buffer, features, context);
		Require(mark_content == sgre::text::NormalizedGlyphContent::GlyphIndices && mark_buffer.InputGlyphs()[0].GlyphIndex == 0, "missing nominal marks retain their genuine zero glyph mapping");
		Require(((mark_buffer.InputGlyphs()[0].CharacterState >> 8) & 0xffU) == (mode == 9 ? 20U : 220U), "effective GPOS admission supplies the original modified-class fallback gate");
		Require(mark_buffer.InputGlyphs()[0].GlyphProperties == (mode == 8 ? 0 : 8) && mark_buffer.InputGlyphs()[0].PrivateState == 0, "classful and missing-class GDEF continue in the original order");
	}
}

static bool ReadWord(std::uint32_t& value, bool allow_end = false) {
	std::array<char, 4> bytes;
	std::cin.read(bytes.data(), 4);
	if (allow_end && std::cin.gcount() == 0 && std::cin.eof()) {
		return false;
	}
	if (std::cin.gcount() != 4) {
		throw std::runtime_error("normalization font message has a truncated word");
	}
	value = 0;
	for (std::size_t index = 0; index < bytes.size(); ++index) {
		value |= std::uint32_t(static_cast<unsigned char>(bytes[index])) << (index * 8);
	}

	return true;
}

static std::vector<std::uint8_t> ReadBytes(std::uint32_t length) {
	std::vector<std::uint8_t> bytes(length);
	std::array<char, 4096> chunk;
	for (std::size_t offset = 0; offset < bytes.size();) {
		const auto count = std::min(chunk.size(), bytes.size() - offset);
		std::cin.read(chunk.data(), static_cast<std::streamsize>(count));
		if (std::cin.gcount() != static_cast<std::streamsize>(count)) {
			throw std::runtime_error("normalization font message has truncated bytes");
		}
		for (std::size_t index = 0; index < count; ++index) {
			bytes[offset + index] = static_cast<std::uint8_t>(static_cast<unsigned char>(chunk[index]));
		}
		offset += count;
	}

	return bytes;
}

static void WriteWord(std::uint32_t value) {
	for (std::uint32_t index = 0; index < 4; ++index) {
		std::cout.put(std::bit_cast<char>(static_cast<std::uint8_t>(value & 0xffU)));
		value >>= 8;
	}
}

static void NativeMessages() {
	std::uint32_t length = 0;
	while (ReadWord(length, true)) {
		std::uint32_t query_count = 0;
		ReadWord(query_count);
		if (length > 64 * 1024 * 1024 || query_count > 4096) {
			throw std::runtime_error("normalization font message exceeds its input limit");
		}
		const auto pin = std::make_shared<const std::vector<std::uint8_t>>(ReadBytes(length));
		const sgre::resource::SfntFont reader(*pin);
		sgre::text::VectorFontNormalizationFont font(pin, reader);
		WriteWord(reader.UnitsPerEm());
		WriteWord(static_cast<std::uint32_t>(reader.SelectedCharacterMap()));
		WriteWord(font.Face().FaceIndex());
		WriteWord(font.Face().ParentIsImmutable());
		for (std::uint32_t index = 0; index < query_count; ++index) {
			std::uint32_t kind = 0;
			std::uint32_t codepoint = 0;
			std::uint32_t selector = 0;
			ReadWord(kind);
			ReadWord(codepoint);
			ReadWord(selector);
			if (kind > 1) {
				throw std::runtime_error("normalization font message has an invalid query kind");
			}
			const auto glyph = kind == 0 ? font.NominalGlyph(codepoint) : font.VariationGlyph(codepoint, selector);
			WriteWord(glyph.has_value());
			WriteWord(glyph.value_or(0xabcd1234));
		}
		WriteWord(font.Face().PrepareLayout());
		WriteWord(font.Face().HasPositioning());
		WriteWord(font.Face().HasGlyphClasses());
		WriteWord(font.Face().LayoutCache()->Definition().Length());
		WriteWord(font.Face().LayoutCache()->Substitution().Length());
		WriteWord(font.Face().LayoutCache()->Positioning().Length());
		WriteWord(static_cast<std::uint32_t>(font.Face().LayoutCache()->SubstitutionLookups().size()));
		WriteWord(static_cast<std::uint32_t>(font.Face().LayoutCache()->PositioningLookups().size()));
		std::cout.flush();
	}
}

static void WriteGlyph(const sgre::text::SubstitutionGlyph& glyph) {
	WriteWord(glyph.GlyphIndex);
	WriteWord(glyph.Mask);
	WriteWord(glyph.Cluster);
	WriteWord(std::uint32_t(glyph.GlyphProperties) | (std::uint32_t(glyph.PrivateState) << 16));
	WriteWord(glyph.CharacterState);
}

static void NativeEntryMessages() {
	std::uint32_t length = 0;
	while (ReadWord(length, true)) {
		std::uint32_t case_count = 0;
		ReadWord(case_count);
		if (length > 64 * 1024 * 1024 || case_count > 4096) {
			throw std::runtime_error("normalization font entry message exceeds its input limit");
		}
		const auto pin = std::make_shared<const std::vector<std::uint8_t>>(ReadBytes(length));
		const sgre::resource::SfntFont reader(*pin);
		sgre::text::VectorFontNormalizationFont font(pin, reader);
		for (std::uint32_t case_index = 0; case_index < case_count; ++case_index) {
			std::array<std::uint32_t, 7> fields;
			for (auto& value : fields) {
				ReadWord(value);
			}
			if (fields[0] > 4096 || fields[6] > 4096) {
				throw std::runtime_error("normalization font entry record work exceeds its input limit");
			}
			std::vector<sgre::text::SubstitutionGlyph> records(fields[6]);
			for (auto& glyph : records) {
				std::uint32_t private_word = 0;
				ReadWord(glyph.GlyphIndex);
				ReadWord(glyph.Mask);
				ReadWord(glyph.Cluster);
				ReadWord(private_word);
				glyph.GlyphProperties = static_cast<std::uint16_t>(private_word & 0xffffU);
				glyph.PrivateState = static_cast<std::uint16_t>(private_word >> 16);
				ReadWord(glyph.CharacterState);
			}
			auto flags = static_cast<sgre::text::UnicodeBufferScratchFlags>(fields[1]);
			if (fields[2] != 0) {
				flags = sgre::text::InitializeUnicodeGlyphProperties(records, flags);
			}
			sgre::text::UnicodeNormalizationBuffer buffer(records, fields[0], flags);
			sgre::text::NeutralGlyphPreparationContext context;
			context.ClusterLevel = static_cast<sgre::text::GlyphClusterLevel>(fields[3]);
			context.OriginalDirection = static_cast<sgre::text::GlyphShapingDirection>(fields[4]);
			context.BufferDirection = static_cast<sgre::text::GlyphShapingDirection>(fields[5]);
			// These proof messages use the authored zero-lookup layout profile. General plan ownership remains open.
			if (font.Face().PrepareLayout() && (!font.Face().LayoutCache()->SubstitutionLookups().empty() || !font.Face().LayoutCache()->PositioningLookups().empty())) {
				throw std::runtime_error("normalization font entry proof requires its zero-lookup fixture profile");
			}
			const sgre::text::VectorFontFeatureMap features;
			const auto content = font.PrepareNeutralGlyphs(buffer, features, context);
			const auto state = buffer.State();
			WriteWord(static_cast<std::uint32_t>(content));
			WriteWord(state.InputCursor);
			WriteWord(state.InputCount);
			WriteWord(state.OutputCount);
			WriteWord(state.ResidentCapacity);
			WriteWord(state.MaximumLength);
			WriteWord(static_cast<std::uint32_t>(state.ScratchFlags));
			WriteWord(state.OutputActive);
			WriteWord(state.Failed);
			WriteWord(state.OutputAliasesInput);
			for (const auto& glyph : buffer.InputGlyphs()) {
				WriteGlyph(glyph);
			}
			for (const auto& glyph : buffer.OutputGlyphs()) {
				WriteGlyph(glyph);
			}
			std::cout.flush();
		}
	}
}

int main(int argc, char** argv) {
	try {
		if (argc == 2 && std::string_view(argv[1]) == "--native") {
			NativeMessages();

			return 0;
		}
		if (argc == 2 && std::string_view(argv[1]) == "--entry") {
			NativeEntryMessages();

			return 0;
		}
		if ((argc == 2 || argc == 3) && std::string_view(argv[1]) == "--fixture") {
			std::uint32_t mode = 0;
			if (argc == 3) {
				const std::string_view argument(argv[2]);
				const auto result = std::from_chars(argument.data(), argument.data() + argument.size(), mode);
				if (result.ec != std::errc{} || result.ptr != argument.data() + argument.size() || mode > 9) {
					throw std::runtime_error("normalization font fixture mode is outside its supported range");
				}
			}
			for (const auto byte : MakeFont(mode)) {
				std::cout.put(std::bit_cast<char>(byte));
			}

			return 0;
		}
		if (argc != 1) {
			throw std::runtime_error("unknown normalization font proof argument");
		}
		TestLiveReaderAndNativeQueryProfile();
		TestBindingIdentityAndScopedPins();
		TestOwnedNeutralEntry();
		std::cout << "PASS 3 normalization font groups\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << "text_vector_font_normalization_font_tests: " << error.what() << '\n';

		return 1;
	}
}
