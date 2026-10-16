#pragma once

#include <cstdint>

namespace sgre::text {

	enum class UnicodeGeneralCategory : std::uint8_t {
		Control = 0,
		Format = 1,
		Unassigned = 2,
		PrivateUse = 3,
		Surrogate = 4,
		LowercaseLetter = 5,
		ModifierLetter = 6,
		OtherLetter = 7,
		TitlecaseLetter = 8,
		UppercaseLetter = 9,
		SpacingMark = 10,
		EnclosingMark = 11,
		NonSpacingMark = 12,
		DecimalNumber = 13,
		LetterNumber = 14,
		OtherNumber = 15,
		ConnectorPunctuation = 16,
		DashPunctuation = 17,
		ClosePunctuation = 18,
		FinalPunctuation = 19,
		InitialPunctuation = 20,
		OtherPunctuation = 21,
		OpenPunctuation = 22,
		CurrencySymbol = 23,
		ModifierSymbol = 24,
		MathSymbol = 25,
		OtherSymbol = 26,
		LineSeparator = 27,
		ParagraphSeparator = 28,
		SpaceSeparator = 29,
	};

	struct UnicodeCharacterProperties {
		UnicodeGeneralCategory Category = UnicodeGeneralCategory::Unassigned;
		std::uint8_t CombiningClass = 0;
	};

	// Exact category/combining data from the reference executable; no host Unicode-version substitution.
	[[nodiscard]] UnicodeCharacterProperties QueryUnicodeProperties(std::uint32_t codepoint);

} // namespace sgre::text
