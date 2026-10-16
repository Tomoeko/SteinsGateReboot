#include "sgre/text/glyph_definition_blob.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <new>
#include <stdexcept>

namespace sgre::text {

	static constexpr std::size_t kMaximumDefinitionBlobBytes = 2 * 1024 * 1024;
	static constexpr std::uint32_t kMaximumDefinitionEditRequests = 32;
	static constexpr std::uint32_t kMaximumDefinitionValidationWork = 1048576;

	struct DefinitionSanitizer {
		std::span<const std::uint8_t> Bytes;
		std::span<std::uint8_t> WritableBytes;
		std::uint32_t Edits = 0;
		std::uint32_t Work = 0;

		bool Range(std::size_t offset, std::size_t length) {
			if (++Work > kMaximumDefinitionValidationWork) {
				throw std::runtime_error("GDEF sanitizer exceeds its validation work limit");
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

		bool EraseOffset(std::size_t field, std::size_t width) {
			if (Edits >= kMaximumDefinitionEditRequests) {
				return false;
			}
			++Edits;
			if (WritableBytes.empty()) {
				return false;
			}
			std::fill_n(WritableBytes.begin() + static_cast<std::ptrdiff_t>(field), width, std::uint8_t(0));

			return true;
		}
	};

	static bool ValidateDefinitionCoverage(DefinitionSanitizer& context, std::size_t table) {
		if (!context.Range(table, 2)) {
			return false;
		}
		const auto format = context.Word(table);
		if (format != 1 && format != 2) {
			return true;
		}

		return context.Range(table + 2, 2) && context.Array(table + 4, format == 1 ? 2U : 6U, context.Word(table + 2));
	}

	static bool ValidateDefinitionCountedWords(DefinitionSanitizer& context, std::size_t table) {
		return context.Range(table, 2) && context.Array(table + 2, 2, context.Word(table));
	}

	static bool ValidateDefinitionClassOffset(DefinitionSanitizer& context, std::size_t field, std::size_t parent) {
		if (!context.Range(field, 2)) {
			return false;
		}
		const auto relative = context.Word(field);
		if (relative == 0) {
			return true;
		}
		if (!context.Range(parent, relative)) {
			return false;
		}
		const auto table = parent + relative;
		bool valid = context.Range(table, 2);
		if (valid) {
			const auto format = context.Word(table);
			if (format == 1) {
				valid = context.Range(table, 6) && ValidateDefinitionCountedWords(context, table + 4);
			} else if (format == 2) {
				valid = context.Range(table + 2, 2) && context.Array(table + 4, 6, context.Word(table + 2));
			}
		}

		return valid || context.EraseOffset(field, 2);
	}

	static bool ValidateDefinitionCoverageOffset(DefinitionSanitizer& context, std::size_t field, std::size_t parent) {
		if (!context.Range(field, 2)) {
			return false;
		}
		const auto relative = context.Word(field);
		if (relative == 0) {
			return true;
		}
		if (!context.Range(parent, relative)) {
			return false;
		}

		return ValidateDefinitionCoverage(context, parent + relative) || context.EraseOffset(field, 2);
	}

	static bool ValidateDefinitionWordOffsets(DefinitionSanitizer& context, std::size_t table, std::size_t parent) {
		if (!ValidateDefinitionCountedWords(context, table)) {
			return false;
		}
		const auto count = context.Word(table);
		for (std::uint32_t index = 0; index < count; ++index) {
			const auto field = table + 2 + std::size_t(index) * 2;
			const auto relative = context.Word(field);
			if (relative == 0) {
				continue;
			}
			if (!context.Range(parent, relative)) {
				return false;
			}
			if (!ValidateDefinitionCountedWords(context, parent + relative) && !context.EraseOffset(field, 2)) {
				return false;
			}
		}

		return true;
	}

	static bool ValidateDefinitionDeviceOffset(DefinitionSanitizer& context, std::size_t field, std::size_t parent) {
		if (!context.Range(field, 2)) {
			return false;
		}
		const auto relative = context.Word(field);
		if (relative == 0) {
			return true;
		}
		if (!context.Range(parent, relative)) {
			return false;
		}
		const auto table = parent + relative;
		bool valid = context.Range(table + 4, 2);
		if (valid) {
			const auto format = context.Word(table + 4);
			if (format >= 1 && format <= 3) {
				valid = context.Range(table, 6);
				if (valid) {
					const auto first = context.Word(table);
					const auto last = context.Word(table + 2);
					const auto size = first <= last ? ((last - first) >> (4 - format)) * 2 + 8 : 6;
					valid = context.Range(table, size);
				}
			} else if (format == 0x8000) {
				valid = context.Range(table, 6);
			}
		}

		return valid || context.EraseOffset(field, 2);
	}

	static bool ValidateDefinitionCaretValues(DefinitionSanitizer& context, std::size_t table, std::size_t parent) {
		if (!ValidateDefinitionCountedWords(context, table)) {
			return false;
		}
		const auto count = context.Word(table);
		for (std::uint32_t index = 0; index < count; ++index) {
			const auto field = table + 2 + std::size_t(index) * 2;
			const auto relative = context.Word(field);
			if (relative == 0) {
				continue;
			}
			if (!context.Range(parent, relative)) {
				return false;
			}
			const auto caret = parent + relative;
			bool valid = context.Range(caret, 2);
			if (valid) {
				const auto format = context.Word(caret);
				if (format == 1 || format == 2) {
					valid = context.Range(caret, 4);
				} else if (format == 3) {
					valid = context.Range(caret, 6) && ValidateDefinitionDeviceOffset(context, caret + 4, caret);
				}
			}
			if (!valid && !context.EraseOffset(field, 2)) {
				return false;
			}
		}

		return true;
	}

	static bool ValidateDefinitionLigatureOffset(DefinitionSanitizer& context, std::size_t field, std::size_t parent) {
		if (!context.Range(field, 2)) {
			return false;
		}
		const auto relative = context.Word(field);
		if (relative == 0) {
			return true;
		}
		if (!context.Range(parent, relative)) {
			return false;
		}
		const auto table = parent + relative;
		bool valid = ValidateDefinitionCoverageOffset(context, table, table) && ValidateDefinitionCountedWords(context, table + 2);
		if (valid) {
			const auto count = context.Word(table + 2);
			for (std::uint32_t index = 0; index < count; ++index) {
				const auto item_field = table + 4 + std::size_t(index) * 2;
				const auto item_relative = context.Word(item_field);
				if (item_relative == 0) {
					continue;
				}
				if (!context.Range(table, item_relative) || (!ValidateDefinitionCaretValues(context, table + item_relative, table + item_relative) && !context.EraseOffset(item_field, 2))) {
					valid = false;
					break;
				}
			}
		}

		return valid || context.EraseOffset(field, 2);
	}

	static bool ValidateDefinitionMarkSets(DefinitionSanitizer& context, std::size_t table) {
		if (!context.Range(table, 2)) {
			return false;
		}
		if (context.Word(table) != 1) {
			return true;
		}
		if (!context.Range(table + 2, 2) || !context.Array(table + 4, 4, context.Word(table + 2))) {
			return false;
		}
		const auto count = context.Word(table + 2);
		for (std::uint32_t index = 0; index < count; ++index) {
			const auto field = table + 4 + std::size_t(index) * 4;
			const auto relative = context.Word(field, 4);
			if (relative == 0) {
				continue;
			}
			if (!context.Range(table, relative) || (!ValidateDefinitionCoverage(context, table + relative) && !context.EraseOffset(field, 4))) {
				return false;
			}
		}

		return true;
	}

	static bool ValidateDefinitionVariationData(DefinitionSanitizer& context, std::size_t field, std::size_t parent) {
		if (!context.Range(field, 2) || !context.Array(field + 2, 4, context.Word(field))) {
			return false;
		}
		const auto count = context.Word(field);
		for (std::uint32_t index = 0; index < count; ++index) {
			const auto offset_field = field + 2 + std::size_t(index) * 4;
			const auto relative = context.Word(offset_field, 4);
			if (relative == 0) {
				continue;
			}
			if (!context.Range(parent, relative)) {
				return false;
			}
			const auto data = parent + relative;
			bool valid = context.Range(data, 6) && ValidateDefinitionCountedWords(context, data + 4);
			if (valid) {
				const auto short_count = context.Word(data + 2);
				const auto region_count = context.Word(data + 4);
				valid = short_count <= region_count && context.Array(data + 6 + std::size_t(region_count) * 2, short_count + region_count, context.Word(data));
			}
			if (!valid && !context.EraseOffset(offset_field, 4)) {
				return false;
			}
		}

		return true;
	}

	static bool ValidateDefinitionVariationOffset(DefinitionSanitizer& context, std::size_t field, std::size_t parent) {
		if (!context.Range(field, 4)) {
			return false;
		}
		const auto relative = context.Word(field, 4);
		if (relative == 0) {
			return true;
		}
		if (!context.Range(parent, relative)) {
			return false;
		}
		const auto store = parent + relative;
		bool valid = context.Range(store, 8) && context.Word(store) == 1;
		if (valid) {
			const auto region_relative = context.Word(store + 2, 4);
			if (region_relative != 0) {
				if (!context.Range(store, region_relative)) {
					valid = false;
				} else {
					const auto regions = store + region_relative;
					if ((!context.Range(regions, 4) || !context.Array(regions + 4, 6, context.Word(regions) * context.Word(regions + 2))) && !context.EraseOffset(store + 2, 4)) {
						valid = false;
					}
				}
			}
			valid = valid && ValidateDefinitionVariationData(context, store + 6, store);
		}

		return valid || context.EraseOffset(field, 4);
	}

	static bool ValidateDefinitionTable(DefinitionSanitizer& context) {
		if (!context.Range(0, 4) || context.Word(0) != 1 || !ValidateDefinitionClassOffset(context, 4, 0)) {
			return false;
		}
		if (!context.Range(6, 2)) {
			return false;
		}
		const auto attachment = context.Word(6);
		if (attachment != 0) {
			if (!context.Range(0, attachment)) {
				return false;
			}
			if ((!ValidateDefinitionCoverageOffset(context, attachment, attachment) || !ValidateDefinitionWordOffsets(context, attachment + 2, attachment)) && !context.EraseOffset(6, 2)) {
				return false;
			}
		}
		if (!ValidateDefinitionLigatureOffset(context, 8, 0) || !ValidateDefinitionClassOffset(context, 10, 0)) {
			return false;
		}
		const auto version = context.Word(0, 4);
		if (version >= 0x10002) {
			if (!context.Range(12, 2)) {
				return false;
			}
			const auto mark_sets = context.Word(12);
			if (mark_sets != 0 && (!context.Range(0, mark_sets) || (!ValidateDefinitionMarkSets(context, mark_sets) && !context.EraseOffset(12, 2)))) {
				return false;
			}
		}

		return version < 0x10003 || ValidateDefinitionVariationOffset(context, 14, 0);
	}

	GlyphDefinitionBlob::GlyphDefinitionBlob(const resource::SfntFont& font)
	    : GlyphDefinitionBlob(font.FindTable("GDEF").value_or(std::span<const std::uint8_t>())) {
	}

	GlyphDefinitionBlob::GlyphDefinitionBlob(std::span<const std::uint8_t> bytes, bool initially_immutable) {
		if (bytes.size() > kMaximumDefinitionBlobBytes) {
			throw std::runtime_error("GDEF blob exceeds its supported admission limit");
		}
		if (bytes.empty()) {
			return;
		}
		DefinitionSanitizer context;
		context.Bytes = bytes;
		bool valid = ValidateDefinitionTable(context);
		if (context.Edits != 0 && !initially_immutable) {
			try {
				m_RepairedBytes.assign(bytes.begin(), bytes.end());
			} catch (const std::bad_alloc&) {
				// Native repair-copy allocation failure replaces this table with the empty blob.
				m_Status = GlyphDefinitionBlobStatus::Rejected;

				return;
			}
			context.Bytes = m_RepairedBytes;
			context.WritableBytes = m_RepairedBytes;
			context.Edits = 0;
			valid = ValidateDefinitionTable(context);
			if (valid && context.Edits != 0) {
				context.Edits = 0;
				valid = ValidateDefinitionTable(context) && context.Edits == 0;
			}
		}
		if (!valid) {
			std::vector<std::uint8_t>().swap(m_RepairedBytes);
			m_Status = GlyphDefinitionBlobStatus::Rejected;

			return;
		}
		m_Bytes = m_RepairedBytes.empty() ? bytes : std::span<const std::uint8_t>(m_RepairedBytes);
		m_Status = m_RepairedBytes.empty() ? GlyphDefinitionBlobStatus::Accepted : GlyphDefinitionBlobStatus::Repaired;
	}

	GlyphDefinitionBlobStatus GlyphDefinitionBlob::Status() const {
		return m_Status;
	}

	std::span<const std::uint8_t> GlyphDefinitionBlob::Bytes() const {
		return m_Bytes;
	}

	std::uint32_t GlyphDefinitionBlob::Length() const {
		return static_cast<std::uint32_t>(m_Bytes.size());
	}

	static std::uint16_t DefinitionClass(std::span<const std::uint8_t> bytes, std::size_t field, std::uint32_t glyph_index) {
		if (bytes.empty()) {
			return 0;
		}
		DefinitionSanitizer reader;
		reader.Bytes = bytes;
		const auto table = reader.Word(field);
		if (table == 0) {
			return 0;
		}
		const auto format = reader.Word(table);
		if (format == 1) {
			const auto index = glyph_index - reader.Word(table + 2);
			if (index >= reader.Word(table + 4)) {
				return 0;
			}

			return static_cast<std::uint16_t>(reader.Word(table + 6 + std::size_t(index) * 2));
		}
		if (format != 2) {
			return 0;
		}
		const auto count = reader.Word(table + 2);
		std::uint32_t first = 0;
		std::uint32_t last = count;
		while (first < last) {
			const auto middle = first + (last - first - 1) / 2;
			const auto entry = table + 4 + std::size_t(middle) * 6;
			if (glyph_index < reader.Word(entry)) {
				last = middle;
			} else if (glyph_index > reader.Word(entry + 2)) {
				first = middle + 1;
			} else {
				return static_cast<std::uint16_t>(reader.Word(entry + 4));
			}
		}

		return 0;
	}

	bool GlyphDefinitionBlob::HasGlyphClasses() const {
		return !m_Suppressed && !m_Bytes.empty() && (m_Bytes[4] != 0 || m_Bytes[5] != 0);
	}

	std::uint16_t GlyphDefinitionBlob::GlyphClass(std::uint32_t glyph_index) const {
		return m_Suppressed ? 0 : DefinitionClass(m_Bytes, 4, glyph_index);
	}

	std::uint16_t GlyphDefinitionBlob::GlyphProperties(std::uint32_t glyph_index) const {
		switch (GlyphClass(glyph_index)) {
		case 1:
			return 2;
		case 2:
			return 4;
		case 3:
			return static_cast<std::uint16_t>(8U | (static_cast<std::uint32_t>(DefinitionClass(m_Bytes, 10, glyph_index)) << 8));
		default:
			return 0;
		}
	}

	struct DefinitionSuppressionLengths {
		std::uint32_t Definition;
		std::uint32_t Substitution;
		std::uint32_t Positioning;
	};

	static constexpr std::array<DefinitionSuppressionLengths, 28> kSuppressedDefinitions = {{
	    {0x382, 0x310a, 0xb586},
	    {0x38e, 0x3116, 0xba74},
	    {0x3a0, 0x5b02, 0xe7c4},
	    {0x3ac, 0x5b0e, 0xed3c},
	    {0x3c4, 0x5d1c, 0xeaa8},
	    {0x3d0, 0x5d18, 0xf010},
	    {0x3e2, 0x5f9a, 0xebb0},
	    {0x3ee, 0x5f96, 0xf12c},
	    {0x3ee, 0x6000, 0xefa2},
	    {0x3ee, 0x6000, 0xefa8},
	    {0x3fa, 0x5ffc, 0xf56c},
	    {0x3fa, 0x5ffc, 0xf572},
	    {0x340, 0x1c9c, 0xb83a},
	    {0x34c, 0x1c86, 0xb1a2},
	    {0xb4, 0x32fe, 0x1c56},
	    {0xc0, 0x315e, 0x1c56},
	    {0xc0, 0x3192, 0x1c56},
	    {0xbc, 0xf8, 0xf0c},
	    {0xbc, 0x108, 0xd62},
	    {0x422, 0xb7b8, 0x2e2a},
	    {0x422, 0x11874, 0x4182},
	    {0x422, 0x11872, 0x446a},
	    {0x416, 0xb7b6, 0x3138},
	    {0x416, 0x1186e, 0x45c6},
	    {0x416, 0x1186c, 0x42d8},
	    {0x532, 0x1ad50, 0xe252},
	    {0x532, 0x1ad50, 0xe65c},
	    {0x3ec, 0xe6d4, 0x39f4},
	}};
	static constexpr std::array<DefinitionSuppressionLengths, 6> kConditionalDefinitions = {{
	    {0x1ba, 0xb3a, 0xa436},
	    {0x1ba, 0xb3a, 0x98cc},
	    {0x1ae, 0xb3a, 0x9ed6},
	    {0x1ae, 0xb3a, 0x99ce},
	    {0x1ea, 0xbe6, 0xa2a6},
	    {0x1de, 0xbe6, 0xa3ae},
	}};

	void GlyphDefinitionBlob::ApplyLayoutSuppression(std::uint32_t substitution_length, std::uint32_t positioning_length) {
		const auto matches = [this, substitution_length, positioning_length](const auto& lengths) {
			return lengths.Definition == Length() && lengths.Substitution == substitution_length && lengths.Positioning == positioning_length;
		};
		m_Suppressed = std::any_of(kSuppressedDefinitions.begin(), kSuppressedDefinitions.end(), matches) || (std::any_of(kConditionalDefinitions.begin(), kConditionalDefinitions.end(), matches) && DefinitionClass(m_Bytes, 4, 5) == 3);
	}

	bool GlyphDefinitionBlob::IsSuppressed() const {
		return m_Suppressed;
	}

} // namespace sgre::text
