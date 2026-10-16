#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace sgre::text {

	static constexpr std::size_t kMaximumLayoutBlobBytes = 2 * 1024 * 1024;
	static constexpr std::uint32_t kMaximumLayoutEditRequests = 32;
	static constexpr std::uint32_t kMaximumLayoutValidationWork = 1048576;

	struct LayoutSanitizer {
		std::string_view Name = "GSUB";
		std::span<const std::uint8_t> Bytes;
		std::span<std::uint8_t> WritableBytes;
		std::uint32_t Edits = 0;
		std::uint32_t Work = 0;

		bool Range(std::size_t offset, std::size_t length) {
			if (++Work > kMaximumLayoutValidationWork) {
				throw std::runtime_error(std::string(Name) + " sanitizer exceeds its supported validation work limit");
			}

			return offset <= Bytes.size() && length <= Bytes.size() - offset;
		}

		bool Array(std::size_t offset, std::uint32_t width, std::uint32_t count) {
			if (width != 0 && count >= std::numeric_limits<std::uint32_t>::max() / width) {
				return false;
			}

			return Range(offset, std::size_t(width) * count);
		}

		std::uint32_t Word(std::size_t offset, std::size_t width = 2) const {
			std::uint32_t value = 0;
			for (std::size_t index = 0; index < width; ++index) {
				value = (value << 8) | Bytes[offset + index];
			}

			return value;
		}

		bool ChangeOffset(std::size_t field, std::size_t width, std::uint32_t value = 0) {
			if (Edits >= kMaximumLayoutEditRequests) {
				return false;
			}
			++Edits;
			if (WritableBytes.empty()) {
				return false;
			}
			for (std::size_t index = 0; index < width; ++index) {
				WritableBytes[field + width - index - 1] = static_cast<std::uint8_t>(value & 0xffU);
				value >>= 8;
			}

			return true;
		}
	};

	using LayoutValidator = bool (*)(LayoutSanitizer&, std::size_t);

	static bool ValidateLayoutOffset(LayoutSanitizer& context, std::size_t field, std::size_t width, std::size_t parent, LayoutValidator validate) {
		if (!context.Range(field, width)) {
			return false;
		}
		const auto relative = context.Word(field, width);
		if (relative == 0) {
			return true;
		}
		if (!context.Range(parent, relative)) {
			return false;
		}

		return validate(context, parent + relative) || context.ChangeOffset(field, width);
	}

	static bool ValidateLayoutCountedArray(LayoutSanitizer& context, std::size_t table, std::uint32_t width) {
		return context.Range(table, 2) && context.Array(table + 2, width, context.Word(table));
	}

	static bool ValidateLayoutWords(LayoutSanitizer& context, std::size_t table) {
		return ValidateLayoutCountedArray(context, table, 2);
	}

	static bool ValidateLayoutOffsetArray(LayoutSanitizer& context, std::size_t count_field, std::size_t parent, std::uint32_t offset_width, LayoutValidator validate) {
		if (!ValidateLayoutCountedArray(context, count_field, offset_width)) {
			return false;
		}
		const auto count = context.Word(count_field);
		for (std::uint32_t index = 0; index < count; ++index) {
			if (!ValidateLayoutOffset(context, count_field + 2 + std::size_t(index) * offset_width, offset_width, parent, validate)) {
				return false;
			}
		}

		return true;
	}

	static bool ValidateLayoutCoverage(LayoutSanitizer& context, std::size_t table) {
		if (!context.Range(table, 2)) {
			return false;
		}
		const auto format = context.Word(table);
		if (format != 1 && format != 2) {
			return true;
		}

		return context.Range(table + 2, 2) && context.Array(table + 4, format == 1 ? 2U : 6U, context.Word(table + 2));
	}

	static bool ValidateLayoutClass(LayoutSanitizer& context, std::size_t table) {
		if (!context.Range(table, 2)) {
			return false;
		}
		const auto format = context.Word(table);
		if (format == 1) {
			return context.Range(table, 6) && ValidateLayoutWords(context, table + 4);
		}
		if (format == 2) {
			return context.Range(table + 2, 2) && context.Array(table + 4, 6, context.Word(table + 2));
		}

		return true;
	}

	static bool ValidateLayoutLanguage(LayoutSanitizer& context, std::size_t table) {
		return context.Range(table, 6) && ValidateLayoutWords(context, table + 4);
	}

	static bool ValidateLayoutScript(LayoutSanitizer& context, std::size_t table) {
		if (!ValidateLayoutOffset(context, table, 2, table, ValidateLayoutLanguage) || !ValidateLayoutCountedArray(context, table + 2, 6)) {
			return false;
		}
		const auto count = context.Word(table + 2);
		for (std::uint32_t index = 0; index < count; ++index) {
			if (!ValidateLayoutOffset(context, table + 8 + std::size_t(index) * 6, 2, table, ValidateLayoutLanguage)) {
				return false;
			}
		}

		return true;
	}

	static bool ValidateLayoutScripts(LayoutSanitizer& context, std::size_t table) {
		if (!ValidateLayoutCountedArray(context, table, 6)) {
			return false;
		}
		const auto count = context.Word(table);
		for (std::uint32_t index = 0; index < count; ++index) {
			if (!ValidateLayoutOffset(context, table + 6 + std::size_t(index) * 6, 2, table, ValidateLayoutScript)) {
				return false;
			}
		}

		return true;
	}

	static bool ValidateLayoutFeatureParameters(LayoutSanitizer& context, std::size_t table, std::uint32_t tag) {
		if (tag == 0x73697a65) {
			if (!context.Range(table, 10) || context.Word(table) == 0) {
				return false;
			}
			if (context.Word(table + 2) == 0 && context.Word(table + 4) == 0 && context.Word(table + 6) == 0 && context.Word(table + 8) == 0) {
				return true;
			}
			const auto design_size = context.Word(table);

			return design_size >= context.Word(table + 6) && design_size <= context.Word(table + 8) && (context.Word(table + 4) - 256U) <= 0x7eff;
		}
		if ((tag & 0xffff0000U) == 0x73730000) {
			return context.Range(table, 4);
		}
		if ((tag & 0xffff0000U) == 0x63760000) {
			return context.Range(table, 14) && context.Range(table + 12, 2) && context.Array(table + 14, 3, context.Word(table + 12));
		}

		return true;
	}

	static bool ValidateLayoutFeatureParameterOffset(LayoutSanitizer& context, std::size_t table, std::uint32_t tag) {
		if (!context.Range(table, 2)) {
			return false;
		}
		const auto relative = context.Word(table);
		if (relative == 0) {
			return true;
		}
		if (!context.Range(table, relative)) {
			return false;
		}

		return ValidateLayoutFeatureParameters(context, table + relative, tag) || context.ChangeOffset(table, 2);
	}

	static bool ValidateLayoutFeature(LayoutSanitizer& context, std::size_t table, std::uint32_t tag = 0, std::size_t feature_list = 0) {
		if (!context.Range(table, 4) || !ValidateLayoutCountedArray(context, table + 2, 2)) {
			return false;
		}
		const auto original_parameter = context.Word(table);
		if (!ValidateLayoutFeatureParameterOffset(context, table, tag)) {
			return false;
		}
		if (original_parameter == 0 || context.Word(table) != 0 || tag != 0x73697a65 || feature_list >= table) {
			return true;
		}

		// Native legacy 'size' repair first erases the invalid feature-relative offset.
		const auto adjusted = static_cast<std::uint32_t>(feature_list) - static_cast<std::uint32_t>(table) + original_parameter;
		if (adjusted <= 0xffff && context.ChangeOffset(table, 2, adjusted) && !ValidateLayoutFeatureParameterOffset(context, table, tag)) {
			return false;
		}
		if (context.Edits > 1) {
			--context.Edits;
		}

		return true;
	}

	static bool ValidateLayoutFeatures(LayoutSanitizer& context, std::size_t table) {
		if (!ValidateLayoutCountedArray(context, table, 6)) {
			return false;
		}
		const auto count = context.Word(table);
		for (std::uint32_t index = 0; index < count; ++index) {
			const auto record = table + 2 + std::size_t(index) * 6;
			const auto relative = context.Word(record + 4);
			if (relative == 0) {
				continue;
			}
			if (!context.Range(table, relative)) {
				return false;
			}
			if (!ValidateLayoutFeature(context, table + relative, context.Word(record, 4), table) && !context.ChangeOffset(record + 4, 2)) {
				return false;
			}
		}

		return true;
	}

	static bool ValidateLayoutLookupHeader(LayoutSanitizer& context, std::size_t table) {
		if (!context.Range(table, 6) || !ValidateLayoutCountedArray(context, table + 4, 2)) {
			return false;
		}

		return (context.Word(table + 2) & 0x10U) == 0 || context.Range(table + 6 + std::size_t(context.Word(table + 4)) * 2, 2);
	}

	static bool ValidateLayoutGenericLookups(LayoutSanitizer& context, std::size_t table) {
		return ValidateLayoutOffsetArray(context, table, table, 2, ValidateLayoutLookupHeader);
	}

	static bool ValidateLayoutCondition(LayoutSanitizer& context, std::size_t table) {
		return context.Range(table, 2) && (context.Word(table) != 1 || context.Range(table, 8));
	}

	static bool ValidateLayoutConditions(LayoutSanitizer& context, std::size_t table) {
		return ValidateLayoutOffsetArray(context, table, table, 4, ValidateLayoutCondition);
	}

	static bool ValidateLayoutAlternativeFeatures(LayoutSanitizer& context, std::size_t table) {
		if (!context.Range(table, 4) || context.Word(table) != 1 || !ValidateLayoutCountedArray(context, table + 4, 6)) {
			return false;
		}
		const auto count = context.Word(table + 4);
		for (std::uint32_t index = 0; index < count; ++index) {
			const auto field = table + 8 + std::size_t(index) * 6;
			const auto relative = context.Word(field, 4);
			if (relative == 0) {
				continue;
			}
			if (!context.Range(table, relative)) {
				return false;
			}
			if (!ValidateLayoutFeature(context, table + relative) && !context.ChangeOffset(field, 4)) {
				return false;
			}
		}

		return true;
	}

	static bool ValidateLayoutFeatureVariations(LayoutSanitizer& context, std::size_t table) {
		if (!context.Range(table, 4) || context.Word(table) != 1 || !context.Range(table + 4, 4) || !context.Array(table + 8, 8, context.Word(table + 4, 4))) {
			return false;
		}
		const auto count = context.Word(table + 4, 4);
		for (std::uint32_t index = 0; index < count; ++index) {
			const auto record = table + 8 + std::size_t(index) * 8;
			if (!ValidateLayoutOffset(context, record, 4, table, ValidateLayoutConditions) || !ValidateLayoutOffset(context, record + 4, 4, table, ValidateLayoutAlternativeFeatures)) {
				return false;
			}
		}

		return true;
	}

	static bool ValidateLayoutHeader(LayoutSanitizer& context) {
		if (!context.Range(0, 4) || context.Word(0) != 1 || !ValidateLayoutOffset(context, 4, 2, 0, ValidateLayoutScripts) || !ValidateLayoutOffset(context, 6, 2, 0, ValidateLayoutFeatures) || !ValidateLayoutOffset(context, 8, 2, 0, ValidateLayoutGenericLookups)) {
			return false;
		}

		return context.Word(0, 4) < 0x10001 || ValidateLayoutOffset(context, 10, 4, 0, ValidateLayoutFeatureVariations);
	}

	static bool ValidateLayoutContextRule(LayoutSanitizer& context, std::size_t table) {
		if (!context.Range(table, 2) || !context.Range(table + 2, 2)) {
			return false;
		}

		// Native checks glyphCount + 2*lookupCount words, including the first glyph.
		return context.Range(table + 4, std::size_t(context.Word(table) + 2 * context.Word(table + 2)) * 2);
	}

	static bool ValidateLayoutContextRuleSet(LayoutSanitizer& context, std::size_t table) {
		return ValidateLayoutOffsetArray(context, table, table, 2, ValidateLayoutContextRule);
	}

	static bool ValidateLayoutContext(LayoutSanitizer& context, std::size_t table) {
		if (!context.Range(table, 2)) {
			return false;
		}
		const auto format = context.Word(table);
		if (format == 1 || format == 2) {
			if (!ValidateLayoutOffset(context, table + 2, 2, table, ValidateLayoutCoverage)) {
				return false;
			}
			if (format == 2 && !ValidateLayoutOffset(context, table + 4, 2, table, ValidateLayoutClass)) {
				return false;
			}

			return ValidateLayoutOffsetArray(context, table + (format == 1 ? 4 : 6), table, 2, ValidateLayoutContextRuleSet);
		}
		if (format != 3) {
			return true;
		}
		if (!context.Range(table, 6) || context.Word(table + 2) == 0 || !context.Array(table + 6, 2, context.Word(table + 2))) {
			return false;
		}
		const auto count = context.Word(table + 2);
		for (std::uint32_t index = 0; index < count; ++index) {
			if (!ValidateLayoutOffset(context, table + 6 + std::size_t(index) * 2, 2, table, ValidateLayoutCoverage)) {
				return false;
			}
		}

		return context.Array(table + 6 + std::size_t(count) * 2, 4, context.Word(table + 4));
	}

	static bool ValidateLayoutChainRule(LayoutSanitizer& context, std::size_t table) {
		if (!ValidateLayoutWords(context, table)) {
			return false;
		}
		auto cursor = table + 2 + std::size_t(context.Word(table)) * 2;
		if (!context.Range(cursor, 2)) {
			return false;
		}
		const auto input_count = context.Word(cursor);
		if (input_count != 0 && !context.Array(cursor + 2, 2, input_count - 1)) {
			return false;
		}
		cursor += 2 + std::size_t(input_count == 0 ? 0 : input_count - 1) * 2;
		if (!ValidateLayoutWords(context, cursor)) {
			return false;
		}
		cursor += 2 + std::size_t(context.Word(cursor)) * 2;

		return ValidateLayoutCountedArray(context, cursor, 4);
	}

	static bool ValidateLayoutChainRuleSet(LayoutSanitizer& context, std::size_t table) {
		return ValidateLayoutOffsetArray(context, table, table, 2, ValidateLayoutChainRule);
	}

	static bool ValidateLayoutChainContext(LayoutSanitizer& context, std::size_t table) {
		if (!context.Range(table, 2)) {
			return false;
		}
		const auto format = context.Word(table);
		if (format == 1 || format == 2) {
			if (!ValidateLayoutOffset(context, table + 2, 2, table, ValidateLayoutCoverage)) {
				return false;
			}
			if (format == 2 && (!ValidateLayoutOffset(context, table + 4, 2, table, ValidateLayoutClass) || !ValidateLayoutOffset(context, table + 6, 2, table, ValidateLayoutClass) || !ValidateLayoutOffset(context, table + 8, 2, table, ValidateLayoutClass))) {
				return false;
			}

			return ValidateLayoutOffsetArray(context, table + (format == 1 ? 4 : 10), table, 2, ValidateLayoutChainRuleSet);
		}
		if (format != 3) {
			return true;
		}
		if (!ValidateLayoutOffsetArray(context, table + 2, table, 2, ValidateLayoutCoverage)) {
			return false;
		}
		auto cursor = table + 4 + std::size_t(context.Word(table + 2)) * 2;
		if (!ValidateLayoutOffsetArray(context, cursor, table, 2, ValidateLayoutCoverage) || context.Word(cursor) == 0) {
			return false;
		}
		cursor += 2 + std::size_t(context.Word(cursor)) * 2;
		if (!ValidateLayoutOffsetArray(context, cursor, table, 2, ValidateLayoutCoverage)) {
			return false;
		}
		cursor += 2 + std::size_t(context.Word(cursor)) * 2;

		return ValidateLayoutCountedArray(context, cursor, 4);
	}

} // namespace sgre::text
