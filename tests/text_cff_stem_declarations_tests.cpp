#include "sgre/text/cff_stem_declarations.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace sgre::text::test {

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

	static CffCharstringNumber Integer(std::int32_t value) {
		CffCharstringNumber number;
		number.Value = value;
		number.Kind = CffCharstringNumberKind::Integer;

		return number;
	}

	static void CheckWidthAndDeltaOrder() {
		CffDeclaredStemBuffer horizontal;
		CffDeclaredStemBuffer vertical;
		CffCharstringWidth width;
		width.Width16Dot16 = 500 * 65536;
		CffStemDeclarationConfiguration configuration;
		configuration.NominalWidthFontUnits = 100;
		configuration.Origin16Dot16 = 10 * 65536;
		const auto result = AppendCffDeclaredStems(horizontal, width, std::array{Integer(50), Integer(20), Integer(5), Integer(10), Integer(-20)}, configuration);
		Require(width.Read && width.Width16Dot16 == 150 * 65536 && result.RemainingOperandCount == 0 && horizontal.Count == 2, "initial odd stem operands consume the shared width and clear the stack");
		Require(horizontal.Stems[0].Start16Dot16 == 30 * 65536 && horizontal.Stems[0].End16Dot16 == 35 * 65536 && horizontal.Stems[1].Start16Dot16 == 45 * 65536 && horizontal.Stems[1].End16Dot16 == 25 * 65536, "stem deltas accumulate from the provided origin without normalizing a ghost width");
		Require(!horizontal.Stems[0].HasCachedDevicePositions && horizontal.Stems[0].CachedLowerDevice16Dot16 == 0 && horizontal.Stems[0].CachedUpperDevice16Dot16 == 0, "new declarations initialize their private device-position cache");
		configuration.Origin16Dot16 = 0;
		(void)AppendCffDeclaredStems(vertical, width, std::array{Integer(900), Integer(5), Integer(10)}, configuration);
		Require(width.Width16Dot16 == 150 * 65536 && vertical.Count == 1 && vertical.Stems[0].Start16Dot16 == 5 * 65536, "an odd leading operand remains excluded after the shared width was read");
		(void)AppendCffDeclaredStems(horizontal, width, std::array{Integer(2), Integer(3)}, configuration);
		Require(horizontal.Count == 3 && horizontal.Stems[2].Start16Dot16 == 2 * 65536 && horizontal.Stems[2].End16Dot16 == 5 * 65536, "each declaration starts from its supplied origin while appending to the same axis");
	}

	static void CheckEarlyWidthBranch() {
		CffDeclaredStemBuffer stems;
		CffCharstringWidth width;
		width.Width16Dot16 = 700 * 65536;
		CffStemDeclarationConfiguration configuration;
		configuration.AdvanceOnly = true;
		configuration.NominalWidthFontUnits = 200;
		const auto result = AppendCffDeclaredStems(stems, width, std::array{Integer(-50), Integer(10), Integer(20)}, configuration);
		Require(width.Read && width.Width16Dot16 == 150 * 65536 && stems.Count == 0 && result.RemainingOperandCount == 3, "native early width mode leaves the operand stack and stem storage untouched");
		CffCharstringWidth default_width;
		default_width.Width16Dot16 = 700 * 65536;
		const auto empty = AppendCffDeclaredStems(stems, default_width, {});
		Require(default_width.Read && default_width.Width16Dot16 == 700 * 65536 && empty.RemainingOperandCount == 0, "an empty declaration marks the width read while preserving its default");
	}

	static void CheckTaggedNumbers() {
		CffCharstringNumber fraction;
		fraction.Kind = CffCharstringNumberKind::Fraction2Dot30;
		fraction.Value = 8192;
		Require(ConvertCffCharstringNumberTo16Dot16(fraction) == 1, "positive fractional half rounds upward");
		fraction.Value = -8192;
		Require(ConvertCffCharstringNumberTo16Dot16(fraction) == -1, "negative fractional half rounds away from zero");
		fraction.Value = 8191;
		Require(ConvertCffCharstringNumberTo16Dot16(fraction) == 0, "sub-half fractions remain zero");
		fraction.Value = std::numeric_limits<std::int32_t>::max();
		Require(ConvertCffCharstringNumberTo16Dot16(fraction) == -131072, "fraction rounding preserves the native overflowing intermediate addition");
		fraction.Value = std::numeric_limits<std::int32_t>::min();
		Require(ConvertCffCharstringNumberTo16Dot16(fraction) == 131072, "negative fractional conversion preserves native subtraction wrapping");
		Require(ConvertCffCharstringNumberTo16Dot16(Integer(65536)) == 0 && ConvertCffCharstringNumberTo16Dot16(Integer(-1)) == -65536, "integer operands retain explicit 32-bit shift wrapping");
		CffCharstringNumber fixed;
		fixed.Value = -1234567;
		Require(ConvertCffCharstringNumberTo16Dot16(fixed) == fixed.Value, "fixed operands preserve their exact bits");
		fixed.Kind = static_cast<CffCharstringNumberKind>(9);
		RequireError([&] {
			(void)ConvertCffCharstringNumberTo16Dot16(fixed);
		},
		    "unknown prepared number tags must fail");
	}

	static void CheckBoundsBeforeMutation() {
		CffDeclaredStemBuffer stems;
		CffCharstringWidth width;
		stems.Count = stems.Stems.size();
		RequireError([&] {
			(void)AppendCffDeclaredStems(stems, width, std::array{Integer(1), Integer(1)});
		},
		    "declarations beyond the axis storage limit must fail");
		Require(!width.Read && stems.Count == stems.Stems.size(), "a capacity failure must preserve the glyph owner");
		stems.Count = 0;
		std::array<CffCharstringNumber, 49> excessive{};
		RequireError([&] {
			(void)AppendCffDeclaredStems(stems, width, excessive);
		},
		    "operand stacks beyond the Type2 limit must fail");
		CffCharstringNumber large;
		large.Value = std::numeric_limits<std::int32_t>::max();
		RequireError([&] {
			(void)AppendCffDeclaredStems(stems, width, std::array{large, Integer(1)});
		},
		    "out-of-range fixed coordinates must fail at the supported boundary");
		Require(!width.Read && stems.Count == 0, "a coordinate failure must preserve declarations and width state");
	}

} // namespace sgre::text::test

int main() {
	try {
		sgre::text::test::CheckWidthAndDeltaOrder();
		sgre::text::test::CheckEarlyWidthBranch();
		sgre::text::test::CheckTaggedNumbers();
		sgre::text::test::CheckBoundsBeforeMutation();
		std::cout << "CFF stem declaration tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
