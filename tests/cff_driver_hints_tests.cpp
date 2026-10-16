#include "sgre/resource/cff_driver_hints.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace sgre::resource::test {

	static void Require(bool condition, std::string_view message) {
		if (!condition) {
			throw std::runtime_error(std::string(message));
		}
	}

	template <typename Operation>
	static void RequireError(Operation operation, std::string_view message) {
		try {
			operation();
		} catch (const std::runtime_error&) {
			return;
		}

		throw std::runtime_error(std::string(message));
	}

	static void CheckNativeDecimalPrecision() {
		const std::array<std::uint8_t, 6> blue{30, 0x0a, 0x03, 0x96, 0x25, 0xff};
		Require(DecodeCffHintFixedNumber(blue) == 2597 && DecodeCffHintFixedNumber(blue, CffHintDecimalScale::Times1000) == 2596864, "default BlueScale retains native scale-dependent decimal quantization");
		const std::array<std::uint8_t, 4> short_fraction{30, 0x0a, 0x06, 0xff};
		Require(DecodeCffHintFixedNumber(short_fraction) == 0 && DecodeCffHintFixedNumber(short_fraction, CffHintDecimalScale::Times1000) == 3932160, "a short fraction retains native significand truncation before fixed division");
		const std::array<std::uint8_t, 5> exponent{30, 0x1c, 0x2f, 0xff, 0xff};
		RequireError([&] {
			(void)DecodeCffHintFixedNumber(exponent);
		},
		    "a number with bytes after its terminator must fail");
		const std::array<std::uint8_t, 3> negative_exponent{30, 0x1c, 0x2f};
		Require(DecodeCffHintFixedNumber(negative_exponent) == 0 && DecodeCffHintFixedNumber(negative_exponent, CffHintDecimalScale::Times1000) == 655360, "negative exponents use the CFF E-minus nibble and native precision");
		const std::array<std::uint8_t, 3> negative_half{30, 0xea, 0x5f};
		Require(DecodeCffHintFixedNumber(negative_half) == -32768, "negative fixed half retains its signed value");
		Require(NormalizeCffDriverBlueScale(2596864) == 2597 && NormalizeCffDriverBlueScale(-1500) == -2, "BlueScale normalization rounds signed halves away from zero");
	}

	static void CheckIntegerClampsAndByteFields() {
		const std::array<std::uint8_t, 3> negative_short{28, 0xff, 0xff};
		Require(DecodeCffHintFixedNumber(negative_short) == -65536, "big-endian short operands are sign extended");
		const std::array<std::uint8_t, 3> positive_limit{28, 0x7f, 0xff};
		Require(DecodeCffHintFixedNumber(positive_limit) == 2147418112, "the largest integer inside the fixed clamp shifts exactly");
		const std::array<std::uint8_t, 5> saturated{29, 0, 0, 0x80, 0};
		Require(DecodeCffHintFixedNumber(saturated) == std::numeric_limits<std::int32_t>::max(), "an integer above the fixed clamp saturates to full signed maximum");
		const std::array<std::uint8_t, 5> minimum{29, 0x80, 0, 0, 0};
		Require(DecodeCffHintFixedNumber(minimum) == -std::numeric_limits<std::int32_t>::max() && DecodeCffHintFixedNumber(minimum, CffHintDecimalScale::Times1000) == 0, "BlueScale wraps its 32-bit multiplication before saturation");
		const std::array<std::uint8_t, 5> fractional_integer{30, 0xea, 0x5f, 12, 10};
		Require(ReadCffDriverHintParameters(fractional_integer).BlueShiftFontUnits == -1, "real-to-integer conversion floors a negative fixed fraction");
		const std::array<std::uint8_t, 3> byte_boolean{138, 12, 14};
		Require(ReadCffDriverHintParameters(byte_boolean).ForceBoldByte == 255, "ForceBold preserves the native low byte rather than normalizing a boolean");
	}

	static void CheckPrivateDefaultsAndArrays() {
		const auto defaults = ReadCffDriverHintParameters({});
		Require(defaults.BlueScaleTimes1000In16Dot16 == 2596864 && defaults.BlueShiftFontUnits == 7 && defaults.BlueFuzzFontUnits == 1 && defaults.ExpansionFactor16Dot16 == 3932 && defaults.DefaultWidthFontUnits == 0 && defaults.NominalWidthFontUnits == 0, "native private-loader fixed/scalar defaults differ");
		Require(defaults.BlueValuesFontUnits.empty() && defaults.HorizontalStemSnapFontUnits.empty() && defaults.StandardHorizontalStemWidthFontUnits == 0 && defaults.LanguageGroup == 0 && defaults.ForceBoldByte == 0, "absent native private arrays/scalars remain initialized");
		const std::array<std::uint8_t, 7> blue{129, 149, 249, 80, 149, 6, 0};
		Require(ReadCffDriverHintParameters(blue).BlueValuesFontUnits == std::vector<std::int32_t>{-10, 0, 700, 710}, "an unrelated private operator does not change decoded hint fields");
		const std::array<std::uint8_t, 6> deltas{129, 149, 249, 80, 149, 6};
		Require(ReadCffDriverHintParameters(deltas).BlueValuesFontUnits == std::vector<std::int32_t>{-10, 0, 700, 710}, "integer blue deltas accumulate into absolute native font units");
		const std::array<std::uint8_t, 6> real_deltas{30, 0xea, 0x5f, 140, 12, 12};
		Require(ReadCffDriverHintParameters(real_deltas).HorizontalStemSnapFontUnits == std::vector<std::int32_t>{-1, 0}, "delta arrays apply native negative-floor conversion before accumulation");
		std::vector<std::uint8_t> capacity(13, 140);
		capacity.push_back(12);
		capacity.push_back(12);
		Require(ReadCffDriverHintParameters(capacity).HorizontalStemSnapFontUnits.size() == 13, "native driver storage permits thirteen stem-snap entries");
		capacity.insert(capacity.begin(), 140);
		RequireError([&] {
			(void)ReadCffDriverHintParameters(capacity);
		},
		    "stem-snap entries beyond native driver capacity must fail");
	}

	static void CheckMalformedInput() {
		RequireError([] {
			(void)DecodeCffHintFixedNumber({});
		},
		    "empty encoded numbers must fail");
		const std::array<std::uint8_t, 2> truncated{28, 0};
		RequireError([&] {
			(void)DecodeCffHintFixedNumber(truncated);
		},
		    "truncated short operands must fail");
		const std::array<std::uint8_t, 2> reserved{30, 0xdf};
		RequireError([&] {
			(void)DecodeCffHintFixedNumber(reserved);
		},
		    "reserved decimal nibbles must fail");
		const std::array<std::uint8_t, 2> malformed{30, 0xbf};
		RequireError([&] {
			(void)DecodeCffHintFixedNumber(malformed);
		},
		    "an exponent without a significand must fail");
		const std::array<std::uint8_t, 1> zero{139};
		RequireError([&] {
			(void)DecodeCffHintFixedNumber(zero, static_cast<CffHintDecimalScale>(1));
		},
		    "unsupported decimal scales must fail");
		const std::array<std::uint8_t, 4> scalar_count{139, 140, 12, 9};
		RequireError([&] {
			(void)ReadCffDriverHintParameters(scalar_count);
		},
		    "multiple scalar operands must fail");
		const std::array<std::uint8_t, 6> duplicate{139, 12, 9, 140, 12, 9};
		RequireError([&] {
			(void)ReadCffDriverHintParameters(duplicate);
		},
		    "duplicate private operators must fail");
		const std::array<std::uint8_t, 2> odd_blue{139, 6};
		RequireError([&] {
			(void)ReadCffDriverHintParameters(odd_blue);
		},
		    "unpaired blue arrays must fail at the supported boundary");
		const std::array<std::uint8_t, 1> unused{139};
		RequireError([&] {
			(void)ReadCffDriverHintParameters(unused);
		},
		    "unused private operands must fail");
		const std::vector<std::uint8_t> excessive(65537, 0);
		RequireError([&] {
			(void)ReadCffDriverHintParameters(excessive);
		},
		    "private dictionaries beyond the byte limit must fail");
	}

} // namespace sgre::resource::test

int main() {
	try {
		sgre::resource::test::CheckNativeDecimalPrecision();
		sgre::resource::test::CheckIntegerClampsAndByteFields();
		sgre::resource::test::CheckPrivateDefaultsAndArrays();
		sgre::resource::test::CheckMalformedInput();
		std::cout << "CFF driver hint tests passed\n";
		return 0;
	} catch (const std::exception& exception) {
		std::cerr << exception.what() << '\n';
		return 1;
	}
}
