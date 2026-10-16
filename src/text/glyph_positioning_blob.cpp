#include "sgre/text/glyph_positioning_blob.hpp"

#include "layout_blob_sanitizer.hpp"

#include <bit>

namespace sgre::text {

	static bool ValidatePositioningDevice(LayoutSanitizer& context, std::size_t table) {
		if (!context.Range(table + 4, 2)) {
			return false;
		}
		const auto format = context.Word(table + 4);
		if (format >= 1 && format <= 3) {
			if (!context.Range(table, 6)) {
				return false;
			}
			const auto first = context.Word(table);
			const auto last = context.Word(table + 2);

			return context.Range(table, first <= last ? ((last - first) >> (4 - format)) * 2 + 8 : 6);
		}

		return format != 0x8000 || context.Range(table, 6);
	}

	static bool ValidatePositioningAnchor(LayoutSanitizer& context, std::size_t table) {
		if (!context.Range(table, 2)) {
			return false;
		}
		const auto format = context.Word(table);
		if (format == 1 || format == 2) {
			return context.Range(table, format == 1 ? 6 : 8);
		}
		if (format == 3) {
			return context.Range(table, 10) && ValidateLayoutOffset(context, table + 6, 2, table, ValidatePositioningDevice) && ValidateLayoutOffset(context, table + 8, 2, table, ValidatePositioningDevice);
		}

		return true;
	}

	static bool ValidatePositioningMarkArray(LayoutSanitizer& context, std::size_t table) {
		if (!ValidateLayoutCountedArray(context, table, 4)) {
			return false;
		}
		const auto count = context.Word(table);
		for (std::uint32_t index = 0; index < count; ++index) {
			if (!ValidateLayoutOffset(context, table + 4 + std::size_t(index) * 4, 2, table, ValidatePositioningAnchor)) {
				return false;
			}
		}

		return true;
	}

	static bool ValidatePositioningAnchorMatrix(LayoutSanitizer& context, std::size_t table, std::uint32_t class_count) {
		if (!context.Range(table, 2)) {
			return false;
		}
		const auto rows = context.Word(table);
		if (class_count != 0 && rows >= std::numeric_limits<std::uint32_t>::max() / class_count) {
			return false;
		}
		const auto count = rows * class_count;
		if (!context.Array(table + 2, 2, count)) {
			return false;
		}
		for (std::uint32_t index = 0; index < count; ++index) {
			if (!ValidateLayoutOffset(context, table + 2 + std::size_t(index) * 2, 2, table, ValidatePositioningAnchor)) {
				return false;
			}
		}

		return true;
	}

	static bool ValidatePositioningMatrixOffset(LayoutSanitizer& context, std::size_t field, std::size_t parent, std::uint32_t class_count) {
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

		return ValidatePositioningAnchorMatrix(context, parent + relative, class_count) || context.ChangeOffset(field, 2);
	}

	static bool ValidatePositioningLigatures(LayoutSanitizer& context, std::size_t table, std::uint32_t class_count) {
		if (!ValidateLayoutWords(context, table)) {
			return false;
		}
		const auto count = context.Word(table);
		for (std::uint32_t index = 0; index < count; ++index) {
			if (!ValidatePositioningMatrixOffset(context, table + 2 + std::size_t(index) * 2, table, class_count)) {
				return false;
			}
		}

		return true;
	}

	static bool ValidatePositioningLigatureOffset(LayoutSanitizer& context, std::size_t field, std::size_t parent, std::uint32_t class_count) {
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

		return ValidatePositioningLigatures(context, parent + relative, class_count) || context.ChangeOffset(field, 2);
	}

	static bool ValidatePositioningValueDevices(LayoutSanitizer& context, std::size_t record, std::size_t parent, std::uint32_t format) {
		auto cursor = record + std::size_t(std::popcount(format & 0xfU)) * 2;
		for (std::uint32_t bit = 0x10; bit <= 0x80; bit <<= 1) {
			if ((format & bit) != 0) {
				if (!ValidateLayoutOffset(context, cursor, 2, parent, ValidatePositioningDevice)) {
					return false;
				}
				cursor += 2;
			}
		}

		return true;
	}

	static bool ValidatePositioningValueArray(LayoutSanitizer& context, std::size_t records, std::size_t parent, std::uint32_t format, std::uint32_t count, std::uint32_t stride_words) {
		if ((format & 0xf0U) == 0) {
			return true;
		}
		for (std::uint32_t index = 0; index < count; ++index) {
			if (!ValidatePositioningValueDevices(context, records + std::size_t(index) * stride_words * 2, parent, format)) {
				return false;
			}
		}

		return true;
	}

	static bool ValidatePositioningSingle(LayoutSanitizer& context, std::size_t table) {
		if (!context.Range(table, 2)) {
			return false;
		}
		const auto format = context.Word(table);
		if (format != 1 && format != 2) {
			return true;
		}
		if (!context.Range(table, format == 1 ? 6 : 8) || !ValidateLayoutOffset(context, table + 2, 2, table, ValidateLayoutCoverage)) {
			return false;
		}
		const auto value_format = context.Word(table + 4);
		const auto width_words = static_cast<std::uint32_t>(std::popcount(value_format));
		if (format == 1) {
			return context.Range(table + 6, std::size_t(width_words) * 2) && ValidatePositioningValueDevices(context, table + 6, table, value_format);
		}
		const auto count = context.Word(table + 6);

		return context.Array(table + 8, width_words * 2, count) && ValidatePositioningValueArray(context, table + 8, table, value_format, count, width_words);
	}

	static bool ValidatePositioningPairSet(LayoutSanitizer& context, std::size_t table, std::size_t parent, std::uint32_t first_format, std::uint32_t second_format) {
		if (!context.Range(table, 2)) {
			return false;
		}
		const auto first_words = static_cast<std::uint32_t>(std::popcount(first_format));
		const auto stride = 1 + first_words + static_cast<std::uint32_t>(std::popcount(second_format));
		const auto count = context.Word(table);

		return context.Array(table + 2, stride * 2, count) && ValidatePositioningValueArray(context, table + 4, parent, first_format, count, stride) && ValidatePositioningValueArray(context, table + 4 + std::size_t(first_words) * 2, parent, second_format, count, stride);
	}

	static bool ValidatePositioningPairSets(LayoutSanitizer& context, std::size_t table) {
		if (!context.Range(table, 10) || !ValidateLayoutOffset(context, table + 2, 2, table, ValidateLayoutCoverage) || !ValidateLayoutWords(context, table + 8)) {
			return false;
		}
		const auto count = context.Word(table + 8);
		for (std::uint32_t index = 0; index < count; ++index) {
			const auto field = table + 10 + std::size_t(index) * 2;
			const auto relative = context.Word(field);
			if (relative == 0) {
				continue;
			}
			if (!context.Range(table, relative)) {
				return false;
			}
			if (!ValidatePositioningPairSet(context, table + relative, table, context.Word(table + 4), context.Word(table + 6)) && !context.ChangeOffset(field, 2)) {
				return false;
			}
		}

		return true;
	}

	static bool ValidatePositioningPairClasses(LayoutSanitizer& context, std::size_t table) {
		if (!context.Range(table, 16) || !ValidateLayoutOffset(context, table + 2, 2, table, ValidateLayoutCoverage) || !ValidateLayoutOffset(context, table + 8, 2, table, ValidateLayoutClass) || !ValidateLayoutOffset(context, table + 10, 2, table, ValidateLayoutClass)) {
			return false;
		}
		const auto first_format = context.Word(table + 4);
		const auto second_format = context.Word(table + 6);
		const auto first_words = static_cast<std::uint32_t>(std::popcount(first_format));
		const auto stride = first_words + static_cast<std::uint32_t>(std::popcount(second_format));
		const auto count = context.Word(table + 12) * context.Word(table + 14);

		return context.Array(table + 16, stride * 2, count) && ValidatePositioningValueArray(context, table + 16, table, first_format, count, stride) && ValidatePositioningValueArray(context, table + 16 + std::size_t(first_words) * 2, table, second_format, count, stride);
	}

	static bool ValidatePositioningPair(LayoutSanitizer& context, std::size_t table) {
		if (!context.Range(table, 2)) {
			return false;
		}
		const auto format = context.Word(table);
		if (format == 1) {
			return ValidatePositioningPairSets(context, table);
		}

		return format != 2 || ValidatePositioningPairClasses(context, table);
	}

	static bool ValidatePositioningCursive(LayoutSanitizer& context, std::size_t table) {
		if (!ValidateLayoutOffset(context, table + 2, 2, table, ValidateLayoutCoverage) || !ValidateLayoutCountedArray(context, table + 4, 4)) {
			return false;
		}
		const auto count = context.Word(table + 4);
		for (std::uint32_t index = 0; index < count; ++index) {
			const auto record = table + 6 + std::size_t(index) * 4;
			if (!ValidateLayoutOffset(context, record, 2, table, ValidatePositioningAnchor) || !ValidateLayoutOffset(context, record + 2, 2, table, ValidatePositioningAnchor)) {
				return false;
			}
		}

		return true;
	}

	static bool ValidatePositioningMarks(LayoutSanitizer& context, std::size_t table, bool ligatures) {
		if (!context.Range(table, 12) || !ValidateLayoutOffset(context, table + 2, 2, table, ValidateLayoutCoverage) || !ValidateLayoutOffset(context, table + 4, 2, table, ValidateLayoutCoverage) || !ValidateLayoutOffset(context, table + 8, 2, table, ValidatePositioningMarkArray)) {
			return false;
		}
		const auto class_count = context.Word(table + 6);

		return ligatures ? ValidatePositioningLigatureOffset(context, table + 10, table, class_count) : ValidatePositioningMatrixOffset(context, table + 10, table, class_count);
	}

	static bool ValidatePositioningSubtable(LayoutSanitizer& context, std::size_t table, std::uint32_t type) {
		while (context.Range(table, 2)) {
			const auto format = context.Word(table);
			switch (type) {
			case 1:
				return ValidatePositioningSingle(context, table);
			case 2:
				return ValidatePositioningPair(context, table);
			case 3:
				return format != 1 || ValidatePositioningCursive(context, table);
			case 4:
			case 5:
			case 6:
				return format != 1 || ValidatePositioningMarks(context, table, type == 5);
			case 7:
				return ValidateLayoutContext(context, table);
			case 8:
				return ValidateLayoutChainContext(context, table);
			case 9:
				if (format != 1) {
					return true;
				}
				if (!context.Range(table, 8) || context.Word(table + 4, 4) == 0 || !context.Range(table, context.Word(table + 4, 4))) {
					return false;
				}
				type = context.Word(table + 2);
				table += context.Word(table + 4, 4);
				continue;
			default:
				return true;
			}
		}

		return false;
	}

	static bool ValidatePositioningLookup(LayoutSanitizer& context, std::size_t table) {
		if (!ValidateLayoutLookupHeader(context, table)) {
			return false;
		}
		const auto type = context.Word(table);
		const auto count = context.Word(table + 4);
		for (std::uint32_t index = 0; index < count; ++index) {
			const auto relative = context.Word(table + 6 + std::size_t(index) * 2);
			if (relative == 0 || !ValidatePositioningSubtable(context, table + relative, type)) {
				return false;
			}
		}

		return true;
	}

	static bool ValidatePositioningLookups(LayoutSanitizer& context, std::size_t table) {
		return ValidateLayoutOffsetArray(context, table, table, 2, ValidatePositioningLookup);
	}

	static bool ValidatePositioningTable(LayoutSanitizer& context) {
		return ValidateLayoutHeader(context) && ValidateLayoutOffset(context, 8, 2, 0, ValidatePositioningLookups);
	}

	GlyphPositioningBlob::GlyphPositioningBlob(const resource::SfntFont& font)
	    : GlyphPositioningBlob(font.FindTable("GPOS").value_or(std::span<const std::uint8_t>())) {
	}

	GlyphPositioningBlob::GlyphPositioningBlob(std::span<const std::uint8_t> bytes, bool initially_immutable) {
		if (bytes.size() > kMaximumLayoutBlobBytes) {
			throw std::runtime_error("GPOS blob exceeds its supported admission byte limit");
		}
		if (bytes.empty()) {
			return;
		}
		LayoutSanitizer context;
		context.Name = "GPOS";
		context.Bytes = bytes;
		bool valid = ValidatePositioningTable(context);
		if (context.Edits != 0 && !initially_immutable) {
			try {
				m_RepairedBytes.assign(bytes.begin(), bytes.end());
			} catch (const std::bad_alloc&) {
				// Native repair-copy allocation failure replaces this table with the empty blob.
				m_Status = GlyphPositioningBlobStatus::Rejected;

				return;
			}
			context.Bytes = m_RepairedBytes;
			context.WritableBytes = m_RepairedBytes;
			context.Edits = 0;
			valid = ValidatePositioningTable(context);
			if (valid && context.Edits != 0) {
				context.Edits = 0;
				valid = ValidatePositioningTable(context) && context.Edits == 0;
			}
		}
		if (!valid) {
			std::vector<std::uint8_t>().swap(m_RepairedBytes);
			m_Status = GlyphPositioningBlobStatus::Rejected;

			return;
		}
		m_Bytes = m_RepairedBytes.empty() ? bytes : std::span<const std::uint8_t>(m_RepairedBytes);
		m_Status = m_RepairedBytes.empty() ? GlyphPositioningBlobStatus::Accepted : GlyphPositioningBlobStatus::Repaired;
	}

	GlyphPositioningBlobStatus GlyphPositioningBlob::Status() const {
		return m_Status;
	}

	std::span<const std::uint8_t> GlyphPositioningBlob::Bytes() const {
		return m_Bytes;
	}

	std::uint32_t GlyphPositioningBlob::Length() const {
		return static_cast<std::uint32_t>(m_Bytes.size());
	}

} // namespace sgre::text
