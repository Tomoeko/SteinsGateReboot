#include "sgre/text/glyph_substitution_blob.hpp"

#include "layout_blob_sanitizer.hpp"

namespace sgre::text {

	static bool ValidateSubstitutionLigature(LayoutSanitizer& context, std::size_t table) {
		if (!context.Range(table, 2) || !context.Range(table + 2, 2)) {
			return false;
		}
		const auto components = context.Word(table + 2);

		return components == 0 || context.Array(table + 4, 2, components - 1);
	}

	static bool ValidateSubstitutionLigatureSet(LayoutSanitizer& context, std::size_t table) {
		return ValidateLayoutOffsetArray(context, table, table, 2, ValidateSubstitutionLigature);
	}

	static bool ValidateSubstitutionReverseTail(LayoutSanitizer& context, std::size_t table);

	static bool ValidateSubstitutionSubtable(LayoutSanitizer& context, std::size_t table, std::uint32_t type) {
		while (context.Range(table, 2)) {
			const auto format = context.Word(table);
			switch (type) {
			case 1:
				if (format == 1) {
					return ValidateLayoutOffset(context, table + 2, 2, table, ValidateLayoutCoverage) && context.Range(table + 4, 2);
				}
				if (format == 2) {
					return ValidateLayoutOffset(context, table + 2, 2, table, ValidateLayoutCoverage) && ValidateLayoutWords(context, table + 4);
				}
				return true;
			case 2:
			case 3:
				return format != 1 || (ValidateLayoutOffset(context, table + 2, 2, table, ValidateLayoutCoverage) && ValidateLayoutOffsetArray(context, table + 4, table, 2, ValidateLayoutWords));
			case 4:
				return format != 1 || (ValidateLayoutOffset(context, table + 2, 2, table, ValidateLayoutCoverage) && ValidateLayoutOffsetArray(context, table + 4, table, 2, ValidateSubstitutionLigatureSet));
			case 5:
				return ValidateLayoutContext(context, table);
			case 6:
				return ValidateLayoutChainContext(context, table);
			case 7:
				if (format != 1) {
					return true;
				}
				if (!context.Range(table, 8) || context.Word(table + 4, 4) == 0 || !context.Range(table, context.Word(table + 4, 4))) {
					return false;
				}
				type = context.Word(table + 2);
				table += context.Word(table + 4, 4);
				continue;
			case 8:
				if (format != 1) {
					return true;
				}
				if (!ValidateLayoutOffset(context, table + 2, 2, table, ValidateLayoutCoverage) || !ValidateLayoutOffsetArray(context, table + 4, table, 2, ValidateLayoutCoverage)) {
					return false;
				}
				return ValidateSubstitutionReverseTail(context, table);
			default:
				return true;
			}
		}

		return false;
	}

	static bool ValidateSubstitutionReverseTail(LayoutSanitizer& context, std::size_t table) {
		const auto lookahead = table + 6 + std::size_t(context.Word(table + 4)) * 2;
		if (!ValidateLayoutOffsetArray(context, lookahead, table, 2, ValidateLayoutCoverage)) {
			return false;
		}

		return ValidateLayoutWords(context, lookahead + 2 + std::size_t(context.Word(lookahead)) * 2);
	}

	static std::uint32_t SubstitutionExtensionType(const LayoutSanitizer& context, std::size_t table, std::uint32_t index) {
		if (index >= context.Word(table + 4)) {
			return 0;
		}
		const auto relative = context.Word(table + 6 + std::size_t(index) * 2);

		return relative == 0 || context.Word(table + relative) != 1 ? 0 : context.Word(table + relative + 2);
	}

	static bool ValidateSubstitutionLookup(LayoutSanitizer& context, std::size_t table) {
		if (!ValidateLayoutLookupHeader(context, table)) {
			return false;
		}
		const auto type = context.Word(table);
		const auto count = context.Word(table + 4);
		for (std::uint32_t index = 0; index < count; ++index) {
			const auto relative = context.Word(table + 6 + std::size_t(index) * 2);
			// Native null offsets resolve outside the sanitizer range and invalidate the lookup.
			if (relative == 0 || !ValidateSubstitutionSubtable(context, table + relative, type)) {
				return false;
			}
		}
		if (type != 7) {
			return true;
		}
		const auto extension_type = SubstitutionExtensionType(context, table, 0);
		if (extension_type == 7) {
			return false;
		}
		for (std::uint32_t index = 1; index < count; ++index) {
			if (SubstitutionExtensionType(context, table, index) != extension_type) {
				return false;
			}
		}

		return true;
	}

	static bool ValidateSubstitutionLookups(LayoutSanitizer& context, std::size_t table) {
		return ValidateLayoutOffsetArray(context, table, table, 2, ValidateSubstitutionLookup);
	}

	static bool ValidateSubstitutionTable(LayoutSanitizer& context) {
		return ValidateLayoutHeader(context) && ValidateLayoutOffset(context, 8, 2, 0, ValidateSubstitutionLookups);
	}

	GlyphSubstitutionBlob::GlyphSubstitutionBlob(const resource::SfntFont& font)
	    : GlyphSubstitutionBlob(font.FindTable("GSUB").value_or(std::span<const std::uint8_t>())) {
	}

	GlyphSubstitutionBlob::GlyphSubstitutionBlob(std::span<const std::uint8_t> bytes, bool initially_immutable) {
		if (bytes.size() > kMaximumLayoutBlobBytes) {
			throw std::runtime_error("GSUB blob exceeds its supported admission byte limit");
		}
		if (bytes.empty()) {
			return;
		}
		LayoutSanitizer context;
		context.Bytes = bytes;
		bool valid = ValidateSubstitutionTable(context);
		if (context.Edits != 0 && !initially_immutable) {
			try {
				m_RepairedBytes.assign(bytes.begin(), bytes.end());
			} catch (const std::bad_alloc&) {
				// Native repair-copy allocation failure replaces this table with the empty blob.
				m_Status = GlyphSubstitutionBlobStatus::Rejected;

				return;
			}
			context.Bytes = m_RepairedBytes;
			context.WritableBytes = m_RepairedBytes;
			context.Edits = 0;
			valid = ValidateSubstitutionTable(context);
			if (valid && context.Edits != 0) {
				context.Edits = 0;
				valid = ValidateSubstitutionTable(context) && context.Edits == 0;
			}
		}
		if (!valid) {
			std::vector<std::uint8_t>().swap(m_RepairedBytes);
			m_Status = GlyphSubstitutionBlobStatus::Rejected;

			return;
		}
		m_Bytes = m_RepairedBytes.empty() ? bytes : std::span<const std::uint8_t>(m_RepairedBytes);
		m_Status = m_RepairedBytes.empty() ? GlyphSubstitutionBlobStatus::Accepted : GlyphSubstitutionBlobStatus::Repaired;
	}

	GlyphSubstitutionBlobStatus GlyphSubstitutionBlob::Status() const {
		return m_Status;
	}

	std::span<const std::uint8_t> GlyphSubstitutionBlob::Bytes() const {
		return m_Bytes;
	}

	std::uint32_t GlyphSubstitutionBlob::Length() const {
		return static_cast<std::uint32_t>(m_Bytes.size());
	}

} // namespace sgre::text
