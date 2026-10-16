#pragma once

#include "sgre/resource/sfnt.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace sgre::resource {

	struct CffHintParameters {
		// Delta-encoded private arrays are exposed as absolute font-unit values.
		std::vector<double> BlueValuesFontUnits;
		std::vector<double> OtherBluesFontUnits;
		std::vector<double> FamilyBluesFontUnits;
		std::vector<double> FamilyOtherBluesFontUnits;
		std::vector<double> HorizontalStemSnapFontUnits;
		std::vector<double> VerticalStemSnapFontUnits;
		std::optional<double> StandardHorizontalStemWidthFontUnits;
		std::optional<double> StandardVerticalStemWidthFontUnits;
		double BlueScale = 0.039625;
		double BlueShiftFontUnits = 7;
		double BlueFuzzFontUnits = 1;
		double ExpansionFactor = 0.06;
		std::int32_t LanguageGroup = 0;
		std::int32_t InitialRandomSeed = 0;
		bool ForceBold = false;
	};

	struct CffFontDictionary {
		double DefaultWidth = 0;
		double NominalWidth = 0;
		std::optional<std::array<double, 6>> FontMatrix;
		std::vector<std::span<const std::uint8_t>> LocalSubroutines;
		CffHintParameters Hinting;
		// Original encoded Private DICT bytes borrow the same immutable sfnt owner as the programs.
		std::span<const std::uint8_t> PrivateDictionaryBytes;
	};

	class CffFont {
	public:
		// Program and subroutine spans borrow the sfnt font's immutable resource bytes.
		explicit CffFont(const SfntFont& font);
		[[nodiscard]] bool IsCid() const;
		[[nodiscard]] const std::array<double, 6>& FontMatrix() const;
		[[nodiscard]] std::span<const std::span<const std::uint8_t>> GlobalSubroutines() const;
		[[nodiscard]] std::span<const CffFontDictionary> FontDictionaries() const;
		[[nodiscard]] std::span<const std::uint8_t> GlyphProgram(std::uint32_t glyph_index) const;
		[[nodiscard]] std::size_t GlyphFontDictionary(std::uint32_t glyph_index) const;

	private:
		std::span<const std::uint8_t> m_Bytes;
		bool m_IsCid = false;
		std::array<double, 6> m_FontMatrix{0.001, 0, 0, 0.001, 0, 0};
		std::vector<std::span<const std::uint8_t>> m_GlyphPrograms;
		std::vector<std::span<const std::uint8_t>> m_GlobalSubroutines;
		std::vector<CffFontDictionary> m_FontDictionaries;
		std::vector<std::uint8_t> m_GlyphDictionaries;
	};

} // namespace sgre::resource
