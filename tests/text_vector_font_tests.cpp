#include "sgre/text/recursive_decomposition.hpp"
#include "sgre/text/vector_font.hpp"
#include "sgre/text/vector_font_normalization_font.hpp"

#include "vector_font_fixture.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static void RequireFailure(const std::function<void()>& operation, std::string_view context) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("VectorFont fixture expected failure");
}

static void TestActualModesAndRawOwnership() {
	for (std::int32_t mode = 0; mode <= 4; ++mode) {
		auto bytes = std::make_shared<const std::vector<std::uint8_t>>(vector_font_test::MakeFont());
		const std::weak_ptr<const std::vector<std::uint8_t>> weak = bytes;
		sgre::text::VectorFont font(bytes, mode, "ja");
		bytes.reset();
		Require(&font.NormalizationFont().Font() == &font.Font() && font.NormalizationFont().NominalGlyph(0x41) == 1 && !font.NormalizationFont().VariationGlyph(0x41, 0xfe0f), "owned normalization binding lost the same resident CFF reader or installed glyph callbacks");
		Require(!weak.expired() && font.Font().GlyphCount() == 3 && font.MapCharacter(0x41) == 1 && font.MapCharacter(0x42) == 2 && font.MapCharacter(0x43) == 0, "VectorFont metadata lost its actual character map or raw resource owner");
		const auto expected_script = mode == 1 ? sgre::text::VectorFontScript::Cyrillic : mode == 2 ? sgre::text::VectorFontScript::Arabic
		    : mode >= 3                                                                             ? sgre::text::VectorFontScript::Han
		                                                                                            : sgre::text::VectorFontScript::Latin;
		const auto expected_direction = mode == 2 ? sgre::text::VectorFontDirection::RightToLeft : mode == 4 ? sgre::text::VectorFontDirection::TopToBottom
		                                                                                                     : sgre::text::VectorFontDirection::LeftToRight;
		Require(font.Configuration().Script == expected_script && font.Configuration().Direction == expected_direction && font.Configuration().Language == "ja", "native shaping mode/script/direction or language mapping differs");
	}

	for (const auto mode : {-1, 5}) {
		RequireFailure([&] {
			static_cast<void>(sgre::text::ResolveVectorFontConfiguration(mode, "en"));
		},
		    "recovered range");
	}
	RequireFailure([] {
		static_cast<void>(sgre::text::VectorFont({}));
	},
	    "raw bytes");
	RequireFailure([] {
		static_cast<void>(sgre::text::ResolveVectorFontConfiguration(0, std::string("en\0tail", 7)));
	},
	    "language");
	const auto canonical = sgre::text::ResolveVectorFontConfiguration(0, "EN_US@Variant.UTF-8");
	Require(canonical.HasLanguage && canonical.Language == "en-us-variant", "native language canonical table must lowercase ASCII, map underscores/@ and terminate at invalid bytes");
	const auto empty = sgre::text::ResolveVectorFontConfiguration(0, "");
	const auto invalid = sgre::text::ResolveVectorFontConfiguration(0, "!");
	Require(!empty.HasLanguage && invalid.HasLanguage && empty.Language.empty() && invalid.Language.empty(), "null native language and interned empty canonical language must remain distinct");
	Require(sgre::text::ResolveVectorFontConfiguration(0, std::string(64, 'A')).Language == std::string(63, 'a'), "native language input must be bounded to its original63-byte stack copy");
}

static void TestSizeWordsAndOriginalCffPreparation() {
	const auto bytes = std::make_shared<const std::vector<std::uint8_t>>(vector_font_test::MakeFont());
	sgre::text::VectorFont first(bytes);
	sgre::text::VectorFont second(bytes);
	Require(first.SizeState().Word == 0xffffffff && !first.SizeState().RequestedHeight26Dot6 && first.Size() == 4294967296.0f, "native initial size sentinel or unsigned getter differs");
	first.SetSize(-1.0f);
	Require(!first.SizeState().RequestedHeight26Dot6, "unchanged native -1 sentinel must skip size request");
	RequireFailure([&] {
		static_cast<void>(first.PrepareGlyph(0));
	},
	    "applied character size");
	first.SetSize(24.9f);
	Require(first.SizeState().Word == 24 && first.SizeState().RequestedHeight26Dot6 == 1536 && first.Size() == 24 && second.SizeState().Word == 0xffffffff, "private font sizes must truncate and remain independent");
	const auto glyph = first.PrepareGlyph(0);
	Require(first.NormalizationFont().Face().PrepareLayout() && first.NormalizationFont().Face().LayoutCache() != nullptr && !first.NormalizationFont().Face().HasPositioning(), "CFF owner failed to couple its real blob face and empty layout cache");
	Require(glyph.PixelSize == 24 && glyph.Outline.Points.size() == 4 && glyph.Outline.Points[1].X26Dot6 == 1536 && glyph.Metrics.Width26Dot6 == 1536, "VectorFont did not dispatch actual CFF Type2/default hints and metrics");
	first.SetSize(0);
	Require(first.SizeState().RequestedHeight26Dot6 == 0 && first.Size() == 0 && first.PrepareGlyph(0).Metrics.Width26Dot6 == 64, "native zero request must preserve reported word while applying one-pixel minimum");
	first.SetSize(-2.0f);
	Require(first.SizeState().Word == 0xfffffffe && first.SizeState().RequestedHeight26Dot6 == std::numeric_limits<std::int32_t>::min() && first.Size() == 4294967296.0f && first.PrepareGlyph(0).PixelSize == 1, "unsigned size wrapping, CVTTSD2SI indefinite or FT minimum differs");
	first.SetSize(2001);
	RequireFailure([&] {
		static_cast<void>(first.PrepareGlyph(0));
	},
	    "recovered range");
}

static void TestInputBufferOwnershipAndReuse() {
	auto bytes = std::make_shared<const std::vector<std::uint8_t>>(vector_font_test::MakeFont());
	const std::weak_ptr<const std::vector<std::uint8_t>> weak = bytes;
	{
		sgre::text::VectorFont first(bytes);
		sgre::text::VectorFont second(bytes);
		bytes.reset();
		auto& input = first.InputBuffer();
		auto& other_input = second.InputBuffer();
		const auto* input_owner = &input;
		auto& normalization = first.NormalizationFont();
		const auto* normalization_owner = &normalization;
		const auto* reader = &first.Font();
		Require(input_owner != &other_input && input.UsesAllocatorGrowth() && other_input.UsesAllocatorGrowth() && input.InputGlyphs().empty() && other_input.InputGlyphs().empty() && input.InputMetadata().Content == sgre::text::UnicodeBufferContent::Unset, "font owners must retain distinct default general buffers");
		other_input.AddUtf16(u"B", 0, 1);
		first.SetSize(24);
		const auto original_cff = first.PrepareGlyph(0);
		const std::array<std::u16string_view, 2> segments = {u"A", u"B"};
		const auto original_direction = sgre::text::GlyphShapingDirection::LeftToRight;
		const sgre::text::VectorFontFeatureMap features;
		for (std::size_t index = 0; index < segments.size(); ++index) {
			input.SetInputDirection(original_direction);
			input.SetInputScript(0x4c61746e);
			input.AddUtf16(segments[index], 0, 1);
			input.InitializeInputUnicodeProperties();
			input.InitializeInputClusters(sgre::text::GlyphClusterLevel::MonotoneGraphemes);
			sgre::text::NeutralGlyphPreparationContext context;
			context.OriginalDirection = original_direction;
			context.BufferDirection = input.AdjustInputDirection();
			const auto content = normalization.PrepareNeutralGlyphs(input, features, context);
			Require(content == sgre::text::NormalizedGlyphContent::GlyphIndices && input.InputMetadata().Content == sgre::text::UnicodeBufferContent::GlyphIndices && input.InputGlyphs().size() == 1 && input.InputGlyphs()[0].GlyphIndex == index + 1 && input.InputGlyphs()[0].Cluster == 0, "same owned input buffer must reach nominal publication through its font binding");
			const auto cff = first.PrepareGlyph(0);
			Require(cff.PixelSize == original_cff.PixelSize && cff.Metrics.Width26Dot6 == original_cff.Metrics.Width26Dot6 && cff.Outline.Points.size() == original_cff.Outline.Points.size(), "buffer normalization must preserve the same font's CFF preparation");
			const auto capacity = input.State().ResidentCapacity;
			const auto allocation = input.InputMetadata();
			input.ClearInput();
			Require(&first.InputBuffer() == input_owner && &first.NormalizationFont() == normalization_owner && &normalization.Font() == reader && !weak.expired(), "clear must retain the same input owner, normalization binding and live resource reader");
			Require(input.InputGlyphs().empty() && input.InputMetadata().Content == sgre::text::UnicodeBufferContent::Unset && input.State().ResidentCapacity == capacity && input.InputMetadata().InputAllocationSlots == allocation.InputAllocationSlots && input.InputMetadata().AuxiliaryAllocationSlots == allocation.AuxiliaryAllocationSlots, "clear must reuse both allocated arrays after nominal publication");
			Require(other_input.InputGlyphs().size() == 1 && other_input.InputGlyphs()[0].GlyphIndex == u'B' && other_input.InputMetadata().Content == sgre::text::UnicodeBufferContent::Unicode && second.SizeState().Word == 0xffffffff, "input reuse and CFF size changes must remain private to their font owner");
		}
	}
	Require(weak.expired(), "font/input/normalization teardown must release the final raw resource pins");
}

static void TestIndependentConversionBoundaries() {
	const std::array<std::array<std::uint32_t, 2>, 16> vectors{{{0, 0}, {0x80000000, 0}, {0x3fc00000, 1}, {0xbfc00000, 0xffffffff}, {0xc0000000, 0xfffffffe}, {0x4f000000, 0x80000000}, {0x4f800000, 0}, {0x4f800001, 512}, {0x5effffff, 0}, {0x5f000000, 0}, {0xdf000000, 0}, {0x7f800000, 0}, {0xff800000, 0}, {0x7fc00001, 0}, {0xffc00001, 0}, {0x3f7fffff, 0}}};
	for (const auto& vector : vectors) {
		Require(sgre::text::VectorFontSizeWord(std::bit_cast<float>(vector[0])) == vector[1], "signed64-to-low32 native size conversion boundary differs");
	}

	Require(sgre::text::VectorFontRequestedHeight26Dot6(0x1ffffff) == 2147483584 && sgre::text::VectorFontRequestedHeight26Dot6(0x2000000) == std::numeric_limits<std::int32_t>::min(), "native height signed32 conversion boundary differs");
}

int main() {
	try {
		TestActualModesAndRawOwnership();
		TestSizeWordsAndOriginalCffPreparation();
		TestInputBufferOwnershipAndReuse();
		TestIndependentConversionBoundaries();
		std::cout << "VectorFont resource ownership, native modes, size words and CFF preparation passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
