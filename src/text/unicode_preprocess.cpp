#include "sgre/text/unicode_preprocess.hpp"

#include "sgre/text/unicode_properties.hpp"

#include <array>
#include <stdexcept>

namespace sgre::text {

	static constexpr std::size_t kMaximumUnicodePreprocessGlyphs = 65536;
	static constexpr std::uint16_t kEmojiModifierProperty = 0x0b;

	struct IgnorableCodepointRange {
		std::uint32_t First = 0;
		std::uint32_t Last = 0;
	};

	// These exact native predicates belong to 0x1403f4670, independently of host Unicode data.
	static constexpr std::array<IgnorableCodepointRange, 13> kDefaultIgnorableRanges = {{
	    {0x00ad, 0x00ad},
	    {0x034f, 0x034f},
	    {0x061c, 0x061c},
	    {0x17b4, 0x17b5},
	    {0x180b, 0x180e},
	    {0x200b, 0x200f},
	    {0x202a, 0x202e},
	    {0x2060, 0x206f},
	    {0xfe00, 0xfe0f},
	    {0xfeff, 0xfeff},
	    {0xfff0, 0xfff8},
	    {0x1d173, 0x1d17a},
	    {0xe0000, 0xe0fff},
	}};

	static bool IsNativeDefaultIgnorable(std::uint32_t codepoint) {
		for (const auto& range : kDefaultIgnorableRanges) {
			if (codepoint >= range.First && codepoint <= range.Last) {
				return true;
			}
		}

		return false;
	}

	static std::uint8_t RemapCanonicalCombiningClass(std::uint8_t combining_class) {
		// Native 0x140621930 differs from identity only at these 28 positions.
		static constexpr std::array<std::uint8_t, 24> kLowCombiningClasses = {
		    22,
		    15,
		    16,
		    17,
		    23,
		    18,
		    19,
		    20,
		    21,
		    14,
		    24,
		    12,
		    25,
		    13,
		    10,
		    11,
		    26,
		    28,
		    29,
		    30,
		    31,
		    32,
		    33,
		    27,
		};
		if (combining_class >= 10 && combining_class <= 33) {
			return kLowCombiningClasses[static_cast<std::size_t>(combining_class - 10)];
		}

		switch (combining_class) {
		case 84:
		case 91:
			return 0;
		case 103:
			return 3;
		case 130:
			return 132;
		case 132:
			return 131;
		default:
			return combining_class;
		}
	}

	static std::uint8_t ModifiedCombiningClass(std::uint32_t codepoint, std::uint8_t canonical_class) {
		switch (codepoint) {
		case 0x1037:
			return RemapCanonicalCombiningClass(QueryUnicodeProperties(0x103a).CombiningClass);
		case 0x1a60:
		case 0x0fc6:
			return 254;
		case 0x0f39:
			return 127;
		default:
			return RemapCanonicalCombiningClass(canonical_class);
		}
	}

	static std::uint16_t IgnorableProperties(std::uint32_t codepoint, std::uint16_t properties) {
		properties |= static_cast<std::uint16_t>(UnicodeGlyphPropertyFlags::DefaultIgnorable);
		if (codepoint == 0x200c) {
			properties |= static_cast<std::uint16_t>(UnicodeGlyphPropertyFlags::ZeroWidthNonJoiner);
		} else if (codepoint == 0x200d) {
			properties |= static_cast<std::uint16_t>(UnicodeGlyphPropertyFlags::ZeroWidthJoiner);
		} else if (codepoint == 0x034f || (codepoint >= 0x180b && codepoint <= 0x180d) || (codepoint >= 0xe0020 && codepoint <= 0xe007f)) {
			properties |= static_cast<std::uint16_t>(UnicodeGlyphPropertyFlags::IgnorableSpecialCase);
		}

		return properties;
	}

	static std::uint16_t CharacterProperties(std::uint32_t codepoint, std::uint32_t& scratch_bits) {
		const auto unicode = QueryUnicodeProperties(codepoint);
		const auto category_bits = static_cast<std::uint16_t>(unicode.Category);
		if (codepoint < 0x80) {
			return category_bits;
		}

		scratch_bits |= static_cast<std::uint32_t>(UnicodeBufferScratchFlags::HasNonAscii);
		if (IsNativeDefaultIgnorable(codepoint)) {
			scratch_bits |= static_cast<std::uint32_t>(UnicodeBufferScratchFlags::HasDefaultIgnorables);

			return IgnorableProperties(codepoint, category_bits);
		}

		if (unicode.Category != UnicodeGeneralCategory::SpacingMark && unicode.Category != UnicodeGeneralCategory::NonSpacingMark && unicode.Category != UnicodeGeneralCategory::ModifierSymbol) {
			return category_bits;
		}

		// The native emoji-modifier override replaces the whole low word after class remapping.
		if (codepoint >= 0x1f3fb && codepoint <= 0x1f3ff) {
			return kEmojiModifierProperty;
		}

		const auto modified_class = ModifiedCombiningClass(codepoint, unicode.CombiningClass);

		return static_cast<std::uint16_t>(category_bits | (static_cast<std::uint16_t>(modified_class) << 8));
	}

	UnicodeBufferScratchFlags InitializeUnicodeGlyphProperties(std::span<SubstitutionGlyph> glyphs, UnicodeBufferScratchFlags scratch_flags) {
		if (glyphs.size() > kMaximumUnicodePreprocessGlyphs) {
			throw std::runtime_error("Unicode glyph preprocessing exceeds its record count limit");
		}

		auto scratch_bits = static_cast<std::uint32_t>(scratch_flags);
		for (auto& glyph : glyphs) {
			const auto properties = CharacterProperties(glyph.GlyphIndex, scratch_bits);
			glyph.CharacterState = (glyph.CharacterState & 0xffff0000U) | properties;
		}

		return static_cast<UnicodeBufferScratchFlags>(scratch_bits);
	}

} // namespace sgre::text
