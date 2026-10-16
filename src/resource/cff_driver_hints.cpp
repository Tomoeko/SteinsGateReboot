#include "sgre/resource/cff_driver_hints.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <limits>
#include <stdexcept>

namespace sgre::resource {

	static constexpr std::size_t kMaximumPrivateBytes = 65536;
	static constexpr std::size_t kMaximumPrivateOperators = 256;
	static constexpr std::size_t kMaximumPrivateOperands = 96;
	static constexpr std::size_t kMaximumRealNibbles = 64;
	static constexpr std::uint32_t kMantissaGrowthLimit = 0x0ccccccc;
	static constexpr std::array<std::uint32_t, 10> kDecimalPowers{1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000, 1000000000};
	static constexpr std::int32_t kFixedUnit = 65536;

	struct CffDecimalDigits {
		std::uint32_t Mantissa = 0;
		std::int32_t IntegerDigits = 0;
		std::int32_t FractionDigits = 0;
		std::int32_t ExponentAdjustment = 0;
		std::int32_t ExplicitExponent = 0;
		bool Negative = false;
		bool ExponentTooLarge = false;
	};

	static std::uint8_t PrivateByte(std::span<const std::uint8_t> bytes, std::size_t offset) {
		if (offset >= bytes.size()) {
			throw std::runtime_error("CFF driver hint number or operator is truncated");
		}

		return bytes[offset];
	}

	static std::uint32_t ReadBigPrivateWord(std::span<const std::uint8_t> bytes, std::size_t offset, std::size_t count) {
		std::uint32_t value = 0;
		for (std::size_t index = 0; index < count; ++index) {
			value = (value << 8) | PrivateByte(bytes, offset + index);
		}

		return value;
	}

	static std::size_t EncodedPrivateNumberSize(std::span<const std::uint8_t> bytes, std::size_t offset) {
		const auto first = PrivateByte(bytes, offset);
		if (first == 30) {
			std::size_t cursor = offset + 1;
			for (std::size_t count = 0; count < (kMaximumRealNibbles + 1) / 2; ++count) {
				const auto byte = PrivateByte(bytes, cursor++);
				if ((byte >> 4) == 15 || (byte & 15) == 15) {
					return cursor - offset;
				}
			}

			throw std::runtime_error("CFF driver hint real exceeds its digit limit");
		}

		std::size_t count = 0;
		if (first == 28) {
			count = 3;
		} else if (first == 29) {
			count = 5;
		} else if (first >= 32 && first <= 246) {
			count = 1;
		} else if (first >= 247 && first <= 254) {
			count = 2;
		} else {
			throw std::runtime_error("CFF driver hint number encoding is unsupported");
		}
		if (count > bytes.size() - offset) {
			throw std::runtime_error("CFF driver hint integer is truncated");
		}

		return count;
	}

	static std::int32_t DecodePrivateInteger(std::span<const std::uint8_t> bytes) {
		const auto first = PrivateByte(bytes, 0);
		if (first == 28) {
			return std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(ReadBigPrivateWord(bytes, 1, 2)));
		}
		if (first == 29) {
			return std::bit_cast<std::int32_t>(ReadBigPrivateWord(bytes, 1, 4));
		}
		if (first >= 32 && first <= 246) {
			return std::int32_t(first) - 139;
		}
		if (first >= 247 && first <= 250) {
			return (std::int32_t(first) - 247) * 256 + PrivateByte(bytes, 1) + 108;
		}
		if (first >= 251 && first <= 254) {
			return -(std::int32_t(first) - 251) * 256 - PrivateByte(bytes, 1) - 108;
		}

		throw std::runtime_error("CFF driver hint integer has a noninteger encoding");
	}

	static std::size_t ReadRealNibbles(std::span<const std::uint8_t> bytes, std::array<std::uint8_t, kMaximumRealNibbles>& nibbles) {
		std::size_t count = 0;
		for (std::size_t index = 1; index < bytes.size(); ++index) {
			for (const auto nibble : {std::uint8_t(bytes[index] >> 4), std::uint8_t(bytes[index] & 15)}) {
				if (nibble == 15) {
					if (index + 1 != bytes.size()) {
						throw std::runtime_error("CFF driver hint number has trailing bytes");
					}

					return count;
				}
				if (count == nibbles.size() || nibble == 13) {
					throw std::runtime_error("CFF driver hint real has excessive or reserved nibbles");
				}

				nibbles[count++] = nibble;
			}
		}

		throw std::runtime_error("CFF driver hint real has no terminator");
	}

	static CffDecimalDigits DecodeDecimalDigits(std::span<const std::uint8_t> bytes) {
		std::array<std::uint8_t, kMaximumRealNibbles> nibbles{};
		const auto count = ReadRealNibbles(bytes, nibbles);
		std::size_t cursor = 0;
		CffDecimalDigits digits;
		if (cursor < count && nibbles[cursor] == 14) {
			digits.Negative = true;
			++cursor;
		}

		bool has_digit = false;
		while (cursor < count && nibbles[cursor] <= 9) {
			has_digit = true;
			const auto digit = nibbles[cursor++];
			if (digits.Mantissa < kMantissaGrowthLimit) {
				if (digit == 0 && digits.Mantissa == 0) {
					continue;
				}

				++digits.IntegerDigits;
				digits.Mantissa = digits.Mantissa * 10 + digit;
			} else {
				++digits.ExponentAdjustment;
			}
		}
		if (cursor < count && nibbles[cursor] == 10) {
			++cursor;
			while (cursor < count && nibbles[cursor] <= 9) {
				has_digit = true;
				const auto digit = nibbles[cursor++];
				if (digit == 0 && digits.Mantissa == 0) {
					--digits.ExponentAdjustment;
					continue;
				}
				if (digits.Mantissa < kMantissaGrowthLimit && digits.FractionDigits < 9) {
					++digits.FractionDigits;
					digits.Mantissa = digits.Mantissa * 10 + digit;
				}
			}
		}
		if (!has_digit) {
			throw std::runtime_error("CFF driver hint real has no significand digits");
		}
		if (cursor < count && (nibbles[cursor] == 11 || nibbles[cursor] == 12)) {
			const bool negative_exponent = nibbles[cursor++] == 12;
			const auto exponent_start = cursor;
			while (cursor < count && nibbles[cursor] <= 9) {
				if (digits.ExplicitExponent <= 1000) {
					digits.ExplicitExponent = digits.ExplicitExponent * 10 + nibbles[cursor];
				} else {
					digits.ExponentTooLarge = true;
				}

				++cursor;
			}
			if (cursor == exponent_start) {
				throw std::runtime_error("CFF driver hint real has no exponent digits");
			}
			if (negative_exponent) {
				digits.ExplicitExponent = -digits.ExplicitExponent;
			}
		}
		if (cursor != count) {
			throw std::runtime_error("CFF driver hint real nibble grammar is malformed");
		}

		return digits;
	}

	static std::uint32_t DecimalPower(std::int32_t exponent) {
		if (exponent < 0 || std::uint32_t(exponent) >= kDecimalPowers.size()) {
			throw std::runtime_error("CFF driver hint decimal exponent exceeds its power table");
		}

		return kDecimalPowers[static_cast<std::size_t>(exponent)];
	}

	static std::int32_t QuantizeDecimalDigits(const CffDecimalDigits& digits, std::int32_t decimal_scale) {
		std::int32_t magnitude = 0;
		if (digits.Mantissa != 0) {
			const auto exponent = digits.ExplicitExponent + digits.ExponentAdjustment + decimal_scale;
			const auto integer_exponent = exponent + digits.IntegerDigits;
			if ((digits.ExponentTooLarge && digits.ExplicitExponent >= 0) || (!digits.ExponentTooLarge && integer_exponent > 5)) {
				magnitude = std::numeric_limits<std::int32_t>::max();
			} else if (!digits.ExponentTooLarge && integer_exponent >= -5) {
				auto mantissa = digits.Mantissa;
				auto decimal_places = digits.FractionDigits - exponent;
				if (integer_exponent < 0) {
					mantissa /= DecimalPower(-integer_exponent);
					decimal_places += integer_exponent;
				}
				if (decimal_places == 10) {
					mantissa /= 10;
					decimal_places = 9;
				}
				if (decimal_places > 0) {
					const auto divisor = DecimalPower(decimal_places);
					if (mantissa / divisor <= 32767) {
						const auto fixed = (std::uint64_t(mantissa) * kFixedUnit + divisor / 2) / divisor;
						if (fixed > std::uint64_t(std::numeric_limits<std::int32_t>::max())) {
							throw std::runtime_error("CFF driver hint fixed division exceeds signed storage");
						}

						magnitude = static_cast<std::int32_t>(fixed);
					}
				} else {
					const auto integer = std::uint64_t(mantissa) * DecimalPower(-decimal_places);
					magnitude = integer > 32767 ? std::numeric_limits<std::int32_t>::max() : static_cast<std::int32_t>(integer * kFixedUnit);
				}
			}
		}

		return digits.Negative ? -magnitude : magnitude;
	}

	std::int32_t DecodeCffHintFixedNumber(std::span<const std::uint8_t> encoded_number, CffHintDecimalScale scale) {
		if (scale != CffHintDecimalScale::None && scale != CffHintDecimalScale::Times1000) {
			throw std::runtime_error("CFF driver hint decimal scale is unsupported");
		}
		if (EncodedPrivateNumberSize(encoded_number, 0) != encoded_number.size()) {
			throw std::runtime_error("CFF driver hint number has trailing bytes");
		}
		if (encoded_number[0] == 30) {
			return QuantizeDecimalDigits(DecodeDecimalDigits(encoded_number), static_cast<std::int32_t>(scale));
		}

		auto integer = DecodePrivateInteger(encoded_number);
		if (scale == CffHintDecimalScale::Times1000) {
			// The native handler wraps the 32-bit multiply before applying its signed fixed-point clamp.
			integer = std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(integer) * 1000U);
		}
		if (integer > 32767) {
			return std::numeric_limits<std::int32_t>::max();
		}
		if (integer < -32767) {
			return -std::numeric_limits<std::int32_t>::max();
		}

		return integer * kFixedUnit;
	}

	std::int32_t NormalizeCffDriverBlueScale(std::int32_t blue_scale_times_1000_in_16_dot_16) {
		const auto magnitude = blue_scale_times_1000_in_16_dot_16 < 0 ? -std::int64_t(blue_scale_times_1000_in_16_dot_16) : blue_scale_times_1000_in_16_dot_16;
		const auto normalized = static_cast<std::int32_t>((magnitude + 500) / 1000);

		return blue_scale_times_1000_in_16_dot_16 < 0 ? -normalized : normalized;
	}

	static std::int32_t DecodeHintInteger(std::span<const std::uint8_t> number) {
		if (number[0] != 30) {
			return DecodePrivateInteger(number);
		}

		const auto fixed = DecodeCffHintFixedNumber(number);
		if (fixed >= 0) {
			return fixed / kFixedUnit;
		}

		return static_cast<std::int32_t>(-((-std::int64_t(fixed) + kFixedUnit - 1) / kFixedUnit));
	}

	static void RequireScalarOperand(std::size_t count) {
		if (count != 1) {
			throw std::runtime_error("CFF driver hint scalar has an invalid operand count");
		}
	}

	static std::vector<std::int32_t> DecodeHintArray(std::span<const std::span<const std::uint8_t>> operands, std::size_t limit, bool paired) {
		if (operands.size() > limit || (paired && operands.size() % 2 != 0)) {
			throw std::runtime_error("CFF driver hint array has an excessive or unpaired count");
		}

		std::vector<std::int32_t> result;
		result.reserve(operands.size());
		std::int64_t current = 0;
		for (const auto number : operands) {
			current += DecodeHintInteger(number);
			if (current < std::numeric_limits<std::int32_t>::min() || current > std::numeric_limits<std::int32_t>::max()) {
				throw std::runtime_error("CFF driver hint array delta accumulation overflows signed storage");
			}

			result.push_back(static_cast<std::int32_t>(current));
		}

		return result;
	}

	static void ReadDriverHintField(CffDriverHintParameters& hints, std::uint16_t operation, std::span<const std::span<const std::uint8_t>> operands) {
		switch (operation) {
		case 6:
			hints.BlueValuesFontUnits = DecodeHintArray(operands, 14, true);
			return;
		case 7:
			hints.OtherBluesFontUnits = DecodeHintArray(operands, 10, true);
			return;
		case 8:
			hints.FamilyBluesFontUnits = DecodeHintArray(operands, 14, true);
			return;
		case 9:
			hints.FamilyOtherBluesFontUnits = DecodeHintArray(operands, 10, true);
			return;
		case 0x0c0c:
			hints.HorizontalStemSnapFontUnits = DecodeHintArray(operands, 13, false);
			return;
		case 0x0c0d:
			hints.VerticalStemSnapFontUnits = DecodeHintArray(operands, 13, false);
			return;
		case 10:
		case 11:
		case 20:
		case 21:
		case 0x0c09:
		case 0x0c0a:
		case 0x0c0b:
		case 0x0c0e:
		case 0x0c11:
		case 0x0c12:
		case 0x0c13:
			break;
		default:
			return;
		}

		RequireScalarOperand(operands.size());
		const auto number = operands[0];
		switch (operation) {
		case 10:
			hints.StandardHorizontalStemWidthFontUnits = DecodeHintInteger(number);
			break;
		case 11:
			hints.StandardVerticalStemWidthFontUnits = DecodeHintInteger(number);
			break;
		case 20:
			hints.DefaultWidthFontUnits = DecodeHintInteger(number);
			break;
		case 21:
			hints.NominalWidthFontUnits = DecodeHintInteger(number);
			break;
		case 0x0c09:
			hints.BlueScaleTimes1000In16Dot16 = DecodeCffHintFixedNumber(number, CffHintDecimalScale::Times1000);
			break;
		case 0x0c0a:
			hints.BlueShiftFontUnits = DecodeHintInteger(number);
			break;
		case 0x0c0b:
			hints.BlueFuzzFontUnits = DecodeHintInteger(number);
			break;
		case 0x0c0e:
			hints.ForceBoldByte = static_cast<std::uint8_t>(DecodeHintInteger(number));
			break;
		case 0x0c11:
			hints.LanguageGroup = DecodeHintInteger(number);
			break;
		case 0x0c12:
			hints.ExpansionFactor16Dot16 = DecodeCffHintFixedNumber(number);
			break;
		case 0x0c13:
			hints.InitialRandomSeed = DecodeHintInteger(number);
			break;
		default:
			break;
		}
	}

	CffDriverHintParameters ReadCffDriverHintParameters(std::span<const std::uint8_t> private_dictionary) {
		if (private_dictionary.size() > kMaximumPrivateBytes) {
			throw std::runtime_error("CFF driver hint private dictionary exceeds its byte limit");
		}

		CffDriverHintParameters hints;
		std::array<std::span<const std::uint8_t>, kMaximumPrivateOperands> operands{};
		std::array<std::uint16_t, kMaximumPrivateOperators> seen_operators{};
		std::size_t operand_count = 0;
		std::size_t operator_count = 0;
		std::size_t offset = 0;
		while (offset < private_dictionary.size()) {
			const auto first = private_dictionary[offset];
			if (first == 28 || first == 29 || first == 30 || (first >= 32 && first <= 254)) {
				if (operand_count == operands.size()) {
					throw std::runtime_error("CFF driver hint operand stack exceeds its count limit");
				}

				const auto length = EncodedPrivateNumberSize(private_dictionary, offset);
				const auto number = private_dictionary.subspan(offset, length);
				if (first == 30) {
					(void)DecodeDecimalDigits(number);
				}

				operands[operand_count++] = number;
				offset += length;
				continue;
			}
			if (first > 21) {
				throw std::runtime_error("CFF driver hint dictionary operator is reserved");
			}

			++offset;
			const auto operation = static_cast<std::uint16_t>(first == 12 ? 0x0c00 | PrivateByte(private_dictionary, offset++) : first);
			if (operator_count == seen_operators.size() || std::find(seen_operators.begin(), seen_operators.begin() + operator_count, operation) != seen_operators.begin() + operator_count) {
				throw std::runtime_error("CFF driver hint dictionary has excessive or duplicate operators");
			}

			seen_operators[operator_count++] = operation;
			ReadDriverHintField(hints, operation, std::span<const std::span<const std::uint8_t>>(operands).first(operand_count));
			operand_count = 0;
		}
		if (operand_count != 0) {
			throw std::runtime_error("CFF driver hint dictionary ends with unused operands");
		}

		return hints;
	}

} // namespace sgre::resource
