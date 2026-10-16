#include "sgre/text/cff_stem_declarations.hpp"

#include <bit>
#include <limits>
#include <stdexcept>

namespace sgre::text {

	static constexpr std::size_t kMaximumCharstringOperands = 48;

	static std::int32_t WrapAdd(std::int32_t left, std::int32_t right) {
		return std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(left) + std::bit_cast<std::uint32_t>(right));
	}

	static std::int32_t FloorDivideBy16384(std::int32_t value) {
		if (value >= 0) {
			return value / 16384;
		}

		return static_cast<std::int32_t>(-((-std::int64_t(value) + 16383) / 16384));
	}

	std::int32_t ConvertCffCharstringNumberTo16Dot16(CffCharstringNumber number) {
		switch (number.Kind) {
		case CffCharstringNumberKind::Fixed16Dot16:
			return number.Value;
		case CffCharstringNumberKind::Fraction2Dot30:
			return number.Value >= 0 ? FloorDivideBy16384(WrapAdd(number.Value, 8192)) : -FloorDivideBy16384(std::bit_cast<std::int32_t>(8192U - std::bit_cast<std::uint32_t>(number.Value)));
		case CffCharstringNumberKind::Integer:
			return std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(number.Value) << 16);
		}

		throw std::runtime_error("CFF declared stem number kind is unsupported");
	}

	static std::int32_t AddStemCoordinate(std::int32_t left, std::int32_t right) {
		const auto value = std::int64_t(left) + right;
		if (value < std::numeric_limits<std::int32_t>::min() || value > std::numeric_limits<std::int32_t>::max()) {
			throw std::runtime_error("CFF declared stem coordinate overflows signed fixed storage");
		}

		return static_cast<std::int32_t>(value);
	}

	CffStemDeclarationResult AppendCffDeclaredStems(CffDeclaredStemBuffer& stems, CffCharstringWidth& width, std::span<const CffCharstringNumber> operands, CffStemDeclarationConfiguration configuration) {
		if (operands.size() > kMaximumCharstringOperands || stems.Count > stems.Stems.size()) {
			throw std::runtime_error("CFF declared stem operand or storage count exceeds its limit");
		}

		const auto first_stem_operand = operands.size() % 2;
		const auto added_count = (operands.size() - first_stem_operand) / 2;
		if (!configuration.AdvanceOnly && added_count > stems.Stems.size() - stems.Count) {
			throw std::runtime_error("CFF declared stems exceed their glyph-axis storage");
		}

		auto updated_width = width.Width16Dot16;
		if (first_stem_operand != 0 && !width.Read) {
			CffCharstringNumber nominal;
			nominal.Value = configuration.NominalWidthFontUnits;
			nominal.Kind = CffCharstringNumberKind::Integer;
			updated_width = WrapAdd(ConvertCffCharstringNumberTo16Dot16(nominal), ConvertCffCharstringNumberTo16Dot16(operands[0]));
		}

		std::array<CffStemHint, kMaximumCharstringOperands / 2> additions{};
		if (!configuration.AdvanceOnly) {
			auto position = configuration.Origin16Dot16;
			for (std::size_t index = 0; index < added_count; ++index) {
				const auto operand = first_stem_operand + index * 2;
				auto& stem = additions[index];
				stem.Start16Dot16 = AddStemCoordinate(position, ConvertCffCharstringNumberTo16Dot16(operands[operand]));
				stem.End16Dot16 = AddStemCoordinate(stem.Start16Dot16, ConvertCffCharstringNumberTo16Dot16(operands[operand + 1]));
				position = stem.End16Dot16;
			}
		}

		width.Width16Dot16 = updated_width;
		width.Read = true;
		CffStemDeclarationResult result;
		if (!configuration.AdvanceOnly) {
			for (std::size_t index = 0; index < added_count; ++index) {
				stems.Stems[stems.Count++] = additions[index];
			}
		} else {
			result.RemainingOperandCount = operands.size();
		}

		return result;
	}

} // namespace sgre::text
