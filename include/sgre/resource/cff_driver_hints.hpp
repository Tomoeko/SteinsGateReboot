#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace sgre::resource {

	enum class CffHintDecimalScale : std::uint8_t {
		None = 0,
		Times1000 = 3
	};

	struct CffDriverHintParameters {
		std::vector<std::int32_t> BlueValuesFontUnits;
		std::vector<std::int32_t> OtherBluesFontUnits;
		std::vector<std::int32_t> FamilyBluesFontUnits;
		std::vector<std::int32_t> FamilyOtherBluesFontUnits;
		std::vector<std::int32_t> HorizontalStemSnapFontUnits;
		std::vector<std::int32_t> VerticalStemSnapFontUnits;
		std::int32_t BlueScaleTimes1000In16Dot16 = 2596864;
		std::int32_t BlueShiftFontUnits = 7;
		std::int32_t BlueFuzzFontUnits = 1;
		std::int32_t StandardHorizontalStemWidthFontUnits = 0;
		std::int32_t StandardVerticalStemWidthFontUnits = 0;
		std::int32_t LanguageGroup = 0;
		std::int32_t ExpansionFactor16Dot16 = 3932;
		std::int32_t InitialRandomSeed = 0;
		std::int32_t DefaultWidthFontUnits = 0;
		std::int32_t NominalWidthFontUnits = 0;
		std::uint8_t ForceBoldByte = 0;
	};

	// Reproduces private-dictionary fixed-number conversion, including native decimal precision and saturation.
	[[nodiscard]] std::int32_t DecodeCffHintFixedNumber(std::span<const std::uint8_t> encoded_number, CffHintDecimalScale scale = CffHintDecimalScale::None);
	[[nodiscard]] std::int32_t NormalizeCffDriverBlueScale(std::int32_t blue_scale_times_1000_in_16_dot_16);
	// Bytes borrow the original CFF Private DICT for this call; returned parameters own their arrays.
	[[nodiscard]] CffDriverHintParameters ReadCffDriverHintParameters(std::span<const std::uint8_t> private_dictionary);

} // namespace sgre::resource
