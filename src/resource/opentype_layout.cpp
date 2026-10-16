#include "sgre/resource/opentype_layout.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace sgre::resource {

	static constexpr std::size_t kMaximumLayoutRecords = 1024 * 1024;
	static constexpr std::size_t kMaximumLayoutTableBytes = 16 * 1024 * 1024;

	static std::span<const std::uint8_t> LayoutRange(std::span<const std::uint8_t> bytes, std::size_t offset, std::size_t count) {
		if (offset > bytes.size() || count > bytes.size() - offset) {
			throw std::runtime_error("OpenType layout range exceeds its table");
		}

		return bytes.subspan(offset, count);
	}

	static std::uint16_t LayoutWord(std::span<const std::uint8_t> bytes, std::size_t offset) {
		const auto word = LayoutRange(bytes, offset, 2);

		return std::uint16_t((std::uint16_t(word[0]) << 8) | word[1]);
	}

	static std::uint32_t LayoutDoubleWord(std::span<const std::uint8_t> bytes, std::size_t offset) {
		const auto word = LayoutRange(bytes, offset, 4);

		return (std::uint32_t(word[0]) << 24) | (std::uint32_t(word[1]) << 16) | (std::uint32_t(word[2]) << 8) | word[3];
	}

	static std::string LayoutTag(std::span<const std::uint8_t> bytes, std::size_t offset) {
		const auto tag = LayoutRange(bytes, offset, 4);

		return std::string(tag.begin(), tag.end());
	}

	static std::size_t LayoutOffset(std::span<const std::uint8_t> bytes, std::size_t origin, std::uint32_t relative, std::size_t minimum_size) {
		if (relative == 0 || origin > bytes.size() || relative > bytes.size() - origin) {
			throw std::runtime_error("OpenType required layout offset is null or outside its table");
		}

		const auto offset = origin + relative;
		static_cast<void>(LayoutRange(bytes, offset, minimum_size));

		return offset;
	}

	static void AddLayoutRecords(std::size_t& total, std::size_t added) {
		if (added > kMaximumLayoutRecords - total) {
			throw std::runtime_error("OpenType layout record count exceeds its preparation limit");
		}

		total += added;
	}

	static std::size_t ValidateCoverage(std::span<const std::uint8_t> bytes, std::size_t offset, std::uint16_t glyph_count) {
		const auto format = LayoutWord(bytes, offset);
		const auto count = LayoutWord(bytes, offset + 2);
		if (format == 1) {
			static_cast<void>(LayoutRange(bytes, offset + 4, std::size_t(count) * 2));
			std::uint32_t previous = 0;
			for (std::size_t index = 0; index < count; ++index) {
				const auto glyph = LayoutWord(bytes, offset + 4 + index * 2);
				if (glyph >= glyph_count || (index != 0 && glyph <= previous)) {
					throw std::runtime_error("OpenType coverage glyphs are unordered or outside the font");
				}
				previous = glyph;
			}

			return count;
		}
		if (format != 2) {
			throw std::runtime_error("OpenType coverage format is unsupported");
		}

		static_cast<void>(LayoutRange(bytes, offset + 4, std::size_t(count) * 6));
		std::size_t coverage_count = 0;
		std::uint32_t previous_end = 0;
		for (std::size_t index = 0; index < count; ++index) {
			const auto record = offset + 4 + index * 6;
			const auto first = LayoutWord(bytes, record);
			const auto last = LayoutWord(bytes, record + 2);
			const auto coverage_index = LayoutWord(bytes, record + 4);
			if (first > last || last >= glyph_count || (index != 0 && first <= previous_end) || coverage_index != coverage_count) {
				throw std::runtime_error("OpenType coverage ranges are unordered or have inconsistent indices");
			}
			coverage_count += std::size_t(last) - first + 1;
			previous_end = last;
		}

		return coverage_count;
	}

	static std::optional<std::size_t> CoverageIndex(std::span<const std::uint8_t> bytes, std::size_t offset, std::uint16_t glyph) {
		const auto format = LayoutWord(bytes, offset);
		const auto count = LayoutWord(bytes, offset + 2);
		std::size_t first = 0;
		std::size_t last = count;
		while (first < last) {
			const auto middle = first + (last - first) / 2;
			const auto record = offset + 4 + middle * (format == 1 ? 2 : 6);
			const auto begin_glyph = LayoutWord(bytes, record);
			const auto end_glyph = format == 1 ? begin_glyph : LayoutWord(bytes, record + 2);
			if (glyph < begin_glyph) {
				last = middle;
			} else if (glyph > end_glyph) {
				first = middle + 1;
			} else {
				return format == 1 ? middle : std::size_t(LayoutWord(bytes, record + 4)) + glyph - begin_glyph;
			}
		}

		return std::nullopt;
	}

	static void ValidateGlyphClasses(std::span<const std::uint8_t> bytes, std::uint16_t glyph_count, std::uint16_t maximum_class) {
		const auto format = LayoutWord(bytes, 0);
		if (format == 1) {
			const auto first = LayoutWord(bytes, 2);
			const auto count = LayoutWord(bytes, 4);
			if (first > glyph_count || count > glyph_count - first) {
				throw std::runtime_error("OpenType glyph class range exceeds the font");
			}
			static_cast<void>(LayoutRange(bytes, 6, std::size_t(count) * 2));
			for (std::size_t index = 0; index < count; ++index) {
				if (LayoutWord(bytes, 6 + index * 2) > maximum_class) {
					throw std::runtime_error("OpenType glyph class value is invalid");
				}
			}

			return;
		}
		if (format != 2) {
			throw std::runtime_error("OpenType glyph class format is unsupported");
		}

		const auto count = LayoutWord(bytes, 2);
		static_cast<void>(LayoutRange(bytes, 4, std::size_t(count) * 6));
		std::uint32_t previous_end = 0;
		for (std::size_t index = 0; index < count; ++index) {
			const auto record = 4 + index * 6;
			const auto first = LayoutWord(bytes, record);
			const auto last = LayoutWord(bytes, record + 2);
			const auto glyph_class = LayoutWord(bytes, record + 4);
			if (first > last || last >= glyph_count || glyph_class > maximum_class || (index != 0 && first <= previous_end)) {
				throw std::runtime_error("OpenType glyph class ranges or values are invalid");
			}
			previous_end = last;
		}
	}

	OpenTypeLayoutFont::OpenTypeLayoutFont(const SfntFont& font)
	    : OpenTypeLayoutFont(font, OpenTypeLayoutKind::Substitution) {
	}

	OpenTypeLayoutFont::OpenTypeLayoutFont(const SfntFont& font, OpenTypeLayoutKind kind)
	    : m_Kind(kind), m_GlyphCount(font.GlyphCount()) {
		if (kind != OpenTypeLayoutKind::Substitution && kind != OpenTypeLayoutKind::Positioning) {
			throw std::runtime_error("OpenType layout table kind is unsupported");
		}

		m_LayoutBytes = font.FindTable(kind == OpenTypeLayoutKind::Substitution ? "GSUB" : "GPOS").value_or(std::span<const std::uint8_t>{});
		m_DefinitionBytes = font.FindTable("GDEF").value_or(std::span<const std::uint8_t>{});
		if (m_LayoutBytes.size() > kMaximumLayoutTableBytes || m_DefinitionBytes.size() > kMaximumLayoutTableBytes) {
			throw std::runtime_error("OpenType layout table exceeds its size limit");
		}

		ReadLayoutTable();
		ReadDefinitionTable();
	}

	OpenTypeLayoutFont::OpenTypeLayoutFont(AdmittedTablesKey, std::uint16_t glyph_count, OpenTypeLayoutKind kind, std::span<const std::uint8_t> layout, std::span<const std::uint8_t> definition)
	    : m_Kind(kind), m_AdmittedMetadata(true), m_LayoutBytes(layout), m_DefinitionBytes(definition), m_GlyphCount(glyph_count) {
		if ((kind != OpenTypeLayoutKind::Substitution && kind != OpenTypeLayoutKind::Positioning) || glyph_count == 0 || layout.size() > kMaximumLayoutTableBytes || definition.size() > kMaximumLayoutTableBytes) {
			throw std::runtime_error("admitted OpenType metadata kind, glyph count or table size is outside its supported range");
		}

		ReadLayoutTable();
		ReadDefinitionTable();
	}

	void OpenTypeLayoutFont::ReadLayoutTable() {
		if (m_LayoutBytes.empty()) {
			return;
		}

		const auto bytes = m_LayoutBytes;
		const auto version = LayoutDoubleWord(bytes, 0);
		if (version != 0x00010000 && version != 0x00010001) {
			throw std::runtime_error(m_Kind == OpenTypeLayoutKind::Substitution ? "OpenType GSUB version is unsupported" : "OpenType GPOS version is unsupported");
		}
		static_cast<void>(LayoutRange(bytes, 0, version == 0x00010001 ? 14 : 10));
		if (version == 0x00010001) {
			m_FeatureVariationsOffset = LayoutDoubleWord(bytes, 10);
			if (m_FeatureVariationsOffset != 0) {
				static_cast<void>(LayoutRange(bytes, m_FeatureVariationsOffset, 8));
			}
		}

		if (!m_AdmittedMetadata || LayoutWord(bytes, 8) != 0) {
			ReadLookups(LayoutOffset(bytes, 0, LayoutWord(bytes, 8), 2));
		}
		if (!m_AdmittedMetadata || LayoutWord(bytes, 6) != 0) {
			ReadFeatures(LayoutOffset(bytes, 0, LayoutWord(bytes, 6), 2));
		}
		if (!m_AdmittedMetadata || LayoutWord(bytes, 4) != 0) {
			ReadScripts(LayoutOffset(bytes, 0, LayoutWord(bytes, 4), 2));
		}
	}

	static std::optional<std::size_t> ExtensionSingleSubtable(std::span<const std::uint8_t> bytes, std::size_t table) {
		if (LayoutWord(bytes, table) != 1 || LayoutWord(bytes, table + 2) != 1) {
			return std::nullopt;
		}

		return LayoutOffset(bytes, table, LayoutDoubleWord(bytes, table + 4), 2);
	}

	void OpenTypeLayoutFont::ReadLookups(std::size_t offset) {
		const auto bytes = m_LayoutBytes;
		const auto count = LayoutWord(bytes, offset);
		AddLayoutRecords(m_RecordCount, count);
		static_cast<void>(LayoutRange(bytes, offset + 2, std::size_t(count) * 2));
		m_Lookups.reserve(count);
		for (std::size_t index = 0; index < count; ++index) {
			const auto relative = LayoutWord(bytes, offset + 2 + index * 2);
			if (m_AdmittedMetadata && relative == 0) {
				OpenTypeLookup empty;
				empty.Present = false;
				m_Lookups.push_back(std::move(empty));

				continue;
			}
			const auto table = LayoutOffset(bytes, offset, relative, 6);
			OpenTypeLookup lookup;
			lookup.Type = LayoutWord(bytes, table);
			lookup.Flags = LayoutWord(bytes, table + 2);
			const auto subtables = LayoutWord(bytes, table + 4);
			if (lookup.Type == 0 || lookup.Type > (m_Kind == OpenTypeLayoutKind::Substitution ? 8 : 9) || (lookup.Flags & 0x00e0) != 0) {
				throw std::runtime_error("OpenType layout lookup type or reserved flags are invalid");
			}
			AddLayoutRecords(m_RecordCount, subtables);
			static_cast<void>(LayoutRange(bytes, table + 6, std::size_t(subtables) * 2));
			lookup.SubtableOffsets.reserve(subtables);
			for (std::size_t subtable = 0; subtable < subtables; ++subtable) {
				const auto address = LayoutOffset(bytes, table, LayoutWord(bytes, table + 6 + subtable * 2), 2);
				lookup.SubtableOffsets.push_back(static_cast<std::uint32_t>(address));
				if (!m_AdmittedMetadata && m_Kind == OpenTypeLayoutKind::Substitution) {
					if (lookup.Type == 1) {
						ValidateSingleSubtable(address);
					} else if (lookup.Type == 7) {
						if (const auto target = ExtensionSingleSubtable(bytes, address)) {
							ValidateSingleSubtable(*target);
						}
					}
				}
			}
			if ((lookup.Flags & 0x0010) != 0) {
				lookup.MarkFilteringSet = LayoutWord(bytes, table + 6 + std::size_t(subtables) * 2);
			}
			m_Lookups.push_back(std::move(lookup));
		}
	}

	void OpenTypeLayoutFont::ReadFeatures(std::size_t offset) {
		const auto bytes = m_LayoutBytes;
		const auto count = LayoutWord(bytes, offset);
		AddLayoutRecords(m_RecordCount, count);
		static_cast<void>(LayoutRange(bytes, offset + 2, std::size_t(count) * 6));
		m_Features.reserve(count);
		for (std::size_t index = 0; index < count; ++index) {
			const auto record = offset + 2 + index * 6;
			OpenTypeFeature feature;
			feature.Tag = LayoutTag(bytes, record);
			if (!m_Features.empty() && feature.Tag < m_Features.back().Tag) {
				throw std::runtime_error("OpenType feature tags are unordered");
			}
			const auto relative = LayoutWord(bytes, record + 4);
			if (m_AdmittedMetadata && relative == 0) {
				m_Features.push_back(std::move(feature));

				continue;
			}
			const auto table = LayoutOffset(bytes, offset, relative, 4);
			feature.ParameterOffset = LayoutWord(bytes, table);
			if (feature.ParameterOffset != 0) {
				static_cast<void>(LayoutOffset(bytes, table, feature.ParameterOffset, 2));
			}
			const auto lookups = LayoutWord(bytes, table + 2);
			AddLayoutRecords(m_RecordCount, lookups);
			static_cast<void>(LayoutRange(bytes, table + 4, std::size_t(lookups) * 2));
			feature.Lookups.reserve(lookups);
			for (std::size_t lookup = 0; lookup < lookups; ++lookup) {
				const auto lookup_index = LayoutWord(bytes, table + 4 + lookup * 2);
				if (!m_AdmittedMetadata && lookup_index >= m_Lookups.size()) {
					throw std::runtime_error("OpenType feature lookup index exceeds its list");
				}
				feature.Lookups.push_back(lookup_index);
			}
			m_Features.push_back(std::move(feature));
		}
	}

	OpenTypeLanguageSystem OpenTypeLayoutFont::ReadLanguage(std::size_t offset, std::string tag) {
		const auto bytes = m_LayoutBytes;
		if (LayoutWord(bytes, offset) != 0) {
			throw std::runtime_error("OpenType language lookup-order field is not zero");
		}

		OpenTypeLanguageSystem language;
		language.Tag = std::move(tag);
		const auto required = LayoutWord(bytes, offset + 2);
		if (required != 0xffff) {
			if (required >= m_Features.size()) {
				throw std::runtime_error("OpenType required language feature exceeds its list");
			}
			language.RequiredFeature = required;
		}
		const auto count = LayoutWord(bytes, offset + 4);
		AddLayoutRecords(m_RecordCount, count);
		static_cast<void>(LayoutRange(bytes, offset + 6, std::size_t(count) * 2));
		language.Features.reserve(count);
		for (std::size_t index = 0; index < count; ++index) {
			const auto feature = LayoutWord(bytes, offset + 6 + index * 2);
			if (feature >= m_Features.size()) {
				throw std::runtime_error("OpenType language feature index exceeds its list");
			}
			language.Features.push_back(feature);
		}

		return language;
	}

	void OpenTypeLayoutFont::ReadScripts(std::size_t offset) {
		const auto bytes = m_LayoutBytes;
		const auto count = LayoutWord(bytes, offset);
		AddLayoutRecords(m_RecordCount, count);
		static_cast<void>(LayoutRange(bytes, offset + 2, std::size_t(count) * 6));
		m_Scripts.reserve(count);
		for (std::size_t index = 0; index < count; ++index) {
			const auto record = offset + 2 + index * 6;
			OpenTypeScript script;
			script.Tag = LayoutTag(bytes, record);
			if (!m_Scripts.empty() && script.Tag <= m_Scripts.back().Tag) {
				throw std::runtime_error("OpenType script tags are duplicate or unordered");
			}
			const auto relative = LayoutWord(bytes, record + 4);
			if (m_AdmittedMetadata && relative == 0) {
				m_Scripts.push_back(std::move(script));

				continue;
			}
			const auto table = LayoutOffset(bytes, offset, relative, 4);
			const auto default_language = LayoutWord(bytes, table);
			if (default_language != 0) {
				script.DefaultLanguage = ReadLanguage(LayoutOffset(bytes, table, default_language, 6), {});
			}
			const auto languages = LayoutWord(bytes, table + 2);
			AddLayoutRecords(m_RecordCount, languages);
			static_cast<void>(LayoutRange(bytes, table + 4, std::size_t(languages) * 6));
			script.Languages.reserve(languages);
			for (std::size_t language = 0; language < languages; ++language) {
				const auto language_record = table + 4 + language * 6;
				const auto tag = LayoutTag(bytes, language_record);
				if (!script.Languages.empty() && tag <= script.Languages.back().Tag) {
					throw std::runtime_error("OpenType language tags are duplicate or unordered");
				}
				script.Languages.push_back(ReadLanguage(LayoutOffset(bytes, table, LayoutWord(bytes, language_record + 4), 6), tag));
			}
			m_Scripts.push_back(std::move(script));
		}
	}

	void OpenTypeLayoutFont::ReadDefinitionTable() {
		if (m_DefinitionBytes.empty()) {
			return;
		}

		const auto bytes = m_DefinitionBytes;
		const auto version = LayoutDoubleWord(bytes, 0);
		if (version != 0x00010000 && version != 0x00010002 && version != 0x00010003) {
			throw std::runtime_error("OpenType GDEF version is unsupported");
		}
		static_cast<void>(LayoutRange(bytes, 0, version == 0x00010000 ? 12 : version == 0x00010002 ? 14
		                                                                                           : 18));
		for (std::size_t field = 4; field < 12; field += 2) {
			const auto relative = LayoutWord(bytes, field);
			if (relative != 0) {
				static_cast<void>(LayoutOffset(bytes, 0, relative, 2));
			}
		}
		const auto glyph_classes = LayoutWord(bytes, 4);
		if (glyph_classes != 0) {
			m_GlyphClasses = bytes.subspan(glyph_classes);
			ValidateGlyphClasses(m_GlyphClasses, m_GlyphCount, 4);
		}
		const auto mark_classes = LayoutWord(bytes, 10);
		if (mark_classes != 0) {
			m_MarkAttachmentClasses = bytes.subspan(mark_classes);
			ValidateGlyphClasses(m_MarkAttachmentClasses, m_GlyphCount, 0xffff);
		}

		if (version != 0x00010000) {
			const auto sets = LayoutWord(bytes, 12);
			if (sets != 0) {
				static_cast<void>(LayoutOffset(bytes, 0, sets, 4));
			}
		}
		if (version == 0x00010003) {
			const auto variation = LayoutDoubleWord(bytes, 14);
			if (variation != 0) {
				static_cast<void>(LayoutOffset(bytes, 0, variation, 8));
			}
		}
	}

	void OpenTypeLayoutFont::ValidateSingleSubtable(std::size_t offset) {
		const auto bytes = m_LayoutBytes;
		const auto format = LayoutWord(bytes, offset);
		if (format != 1 && format != 2) {
			throw std::runtime_error("OpenType single substitution format is unsupported");
		}
		const auto coverage = LayoutOffset(bytes, offset, LayoutWord(bytes, offset + 2), 4);
		AddLayoutRecords(m_RecordCount, LayoutWord(bytes, coverage + 2));
		const auto count = ValidateCoverage(bytes, coverage, m_GlyphCount);
		if (format == 2) {
			const auto replacements = LayoutWord(bytes, offset + 4);
			if (replacements != count) {
				throw std::runtime_error("OpenType replacement count differs from its coverage");
			}
			static_cast<void>(LayoutRange(bytes, offset + 6, count * 2));
			for (std::size_t index = 0; index < count; ++index) {
				if (LayoutWord(bytes, offset + 6 + index * 2) >= m_GlyphCount) {
					throw std::runtime_error("OpenType replacement glyph exceeds the font");
				}
			}
		} else {
			static_cast<void>(LayoutWord(bytes, offset + 4));
		}
	}

	std::optional<std::uint16_t> OpenTypeLayoutFont::ApplySingleSubtable(std::size_t offset, std::uint16_t glyph_index) const {
		const auto bytes = m_LayoutBytes;
		const auto coverage = LayoutOffset(bytes, offset, LayoutWord(bytes, offset + 2), 4);
		const auto index = CoverageIndex(bytes, coverage, glyph_index);
		if (!index) {
			return std::nullopt;
		}

		const auto replacement = LayoutWord(bytes, offset) == 1 ? std::uint16_t((std::uint32_t(glyph_index) + LayoutWord(bytes, offset + 4)) & 0xffff) : LayoutWord(bytes, offset + 6 + *index * 2);
		if (replacement >= m_GlyphCount) {
			throw std::runtime_error("OpenType single replacement glyph exceeds the font");
		}

		return replacement;
	}

	OpenTypeLayoutKind OpenTypeLayoutFont::Kind() const {
		return m_Kind;
	}

	std::uint32_t OpenTypeLayoutFont::FeatureVariationsOffset() const {
		return m_FeatureVariationsOffset;
	}

	std::span<const OpenTypeLookup> OpenTypeLayoutFont::Lookups() const {
		return m_Lookups;
	}

	std::span<const OpenTypeScript> OpenTypeLayoutFont::Scripts() const {
		return m_Scripts;
	}

	std::span<const OpenTypeFeature> OpenTypeLayoutFont::Features() const {
		return m_Features;
	}

	std::span<const OpenTypeSubstitutionLookup> OpenTypeLayoutFont::SubstitutionLookups() const {
		if (m_Kind != OpenTypeLayoutKind::Substitution) {
			throw std::runtime_error("OpenType GPOS has no substitution lookup catalog");
		}

		return m_Lookups;
	}

	std::optional<std::size_t> OpenTypeLayoutFont::FindScript(std::string_view tag) const {
		const auto found = std::lower_bound(m_Scripts.begin(), m_Scripts.end(), tag, [](const auto& script, std::string_view requested) {
			return script.Tag < requested;
		});
		if (found == m_Scripts.end() || found->Tag != tag) {
			return std::nullopt;
		}

		return static_cast<std::size_t>(found - m_Scripts.begin());
	}

	OpenTypeScriptSelection OpenTypeLayoutFont::SelectScript(std::span<const std::string_view> requested_tags) const {
		if (requested_tags.size() > 128) {
			throw std::runtime_error("OpenType requested script list exceeds its selection limit");
		}

		OpenTypeScriptSelection selection;
		for (const auto tag : requested_tags) {
			const auto index = FindScript(tag);
			if (index) {
				selection.Index = index;
				selection.Tag = m_Scripts[*index].Tag;
				selection.MatchedRequested = true;

				return selection;
			}
		}
		for (const auto tag : {"DFLT", "dflt", "latn"}) {
			const auto index = FindScript(tag);
			if (index) {
				selection.Index = index;
				selection.Tag = m_Scripts[*index].Tag;

				return selection;
			}
		}

		return selection;
	}

	OpenTypeLanguageSelection OpenTypeLayoutFont::SelectLanguage(std::size_t script_index, std::string_view requested_tag) const {
		if (script_index >= m_Scripts.size()) {
			throw std::runtime_error("OpenType language script index exceeds its list");
		}

		const auto& script = m_Scripts[script_index];
		OpenTypeLanguageSelection selection;
		for (std::size_t index = 0; index < script.Languages.size(); ++index) {
			if (script.Languages[index].Tag == requested_tag) {
				selection.Index = index;
				selection.Tag = script.Languages[index].Tag;
				selection.MatchedRequested = true;

				return selection;
			}
		}
		for (std::size_t index = 0; index < script.Languages.size(); ++index) {
			if (script.Languages[index].Tag == "dflt") {
				selection.Index = index;
				selection.Tag = script.Languages[index].Tag;

				return selection;
			}
		}

		return selection;
	}

	std::optional<std::size_t> OpenTypeLayoutFont::FindFeature(std::size_t script_index, std::string_view language_tag, std::string_view feature_tag) const {
		if (script_index >= m_Scripts.size()) {
			throw std::runtime_error("OpenType script index exceeds its list");
		}

		const auto& script = m_Scripts[script_index];
		const OpenTypeLanguageSystem* language = nullptr;
		if (language_tag.empty()) {
			language = script.DefaultLanguage ? &*script.DefaultLanguage : nullptr;
		} else {
			const auto found = std::lower_bound(script.Languages.begin(), script.Languages.end(), language_tag, [](const auto& entry, std::string_view tag) {
				return entry.Tag < tag;
			});
			if (found != script.Languages.end() && found->Tag == language_tag) {
				language = &*found;
			}
		}
		if (language == nullptr) {
			return std::nullopt;
		}
		for (const auto feature : language->Features) {
			if (m_Features[feature].Tag == feature_tag) {
				return feature;
			}
		}

		return std::nullopt;
	}

	static std::uint16_t ClassDefinitionValue(std::span<const std::uint8_t> bytes, std::uint32_t glyph_index) {
		if (bytes.empty()) {
			return 0;
		}

		if (LayoutWord(bytes, 0) == 1) {
			const auto first = LayoutWord(bytes, 2);
			const auto count = LayoutWord(bytes, 4);
			if (glyph_index < first || glyph_index - first >= count) {
				return 0;
			}

			return LayoutWord(bytes, 6 + std::size_t(glyph_index - first) * 2);
		}

		std::size_t first = 0;
		std::size_t last = LayoutWord(bytes, 2);
		while (first < last) {
			const auto middle = first + (last - first) / 2;
			const auto record = 4 + middle * 6;
			const auto begin_glyph = LayoutWord(bytes, record);
			const auto end_glyph = LayoutWord(bytes, record + 2);
			if (glyph_index < begin_glyph) {
				last = middle;
			} else if (glyph_index > end_glyph) {
				first = middle + 1;
			} else {
				return LayoutWord(bytes, record + 4);
			}
		}

		return 0;
	}

	OpenTypeGlyphClass OpenTypeLayoutFont::GlyphClass(std::uint32_t glyph_index) const {
		if (glyph_index >= m_GlyphCount) {
			throw std::runtime_error("OpenType glyph class index exceeds the font");
		}

		return static_cast<OpenTypeGlyphClass>(ClassDefinitionValue(m_GlyphClasses, glyph_index));
	}

	bool OpenTypeLayoutFont::HasGlyphClasses() const {
		return !m_GlyphClasses.empty();
	}

	std::uint16_t OpenTypeLayoutFont::MarkAttachmentClass(std::uint32_t glyph_index) const {
		if (glyph_index >= m_GlyphCount) {
			throw std::runtime_error("OpenType mark attachment index exceeds the font");
		}

		return ClassDefinitionValue(m_MarkAttachmentClasses, glyph_index);
	}

	std::uint16_t OpenTypeLayoutFont::GlyphProperties(std::uint32_t glyph_index) const {
		switch (GlyphClass(glyph_index)) {
		case OpenTypeGlyphClass::Base:
			return 2;
		case OpenTypeGlyphClass::Ligature:
			return 4;
		case OpenTypeGlyphClass::Mark:
			return static_cast<std::uint16_t>((std::uint32_t(MarkAttachmentClass(glyph_index)) << 8) | 8);
		default:
			return 0;
		}
	}

	bool OpenTypeLayoutFont::CanApplySingleSubstitution(std::size_t lookup_index) const {
		if (lookup_index >= m_Lookups.size()) {
			throw std::runtime_error("OpenType single-substitution lookup index exceeds its actual list");
		}
		const auto& lookup = m_Lookups[lookup_index];
		if (m_AdmittedMetadata || m_Kind != OpenTypeLayoutKind::Substitution || !lookup.Present || lookup.Flags != 0 || m_FeatureVariationsOffset != 0) {
			return false;
		}
		if (lookup.Type == 1) {
			return true;
		}
		if (lookup.Type != 7 || lookup.SubtableOffsets.empty()) {
			return false;
		}
		for (const auto offset : lookup.SubtableOffsets) {
			if (!ExtensionSingleSubtable(m_LayoutBytes, offset)) {
				return false;
			}
		}

		return true;
	}

	std::optional<std::uint16_t> OpenTypeLayoutFont::ApplySingleSubstitution(std::size_t lookup_index, std::uint32_t glyph_index) const {
		if (m_AdmittedMetadata) {
			throw std::runtime_error("admitted OpenType metadata does not admit lookup execution");
		}

		if (m_Kind != OpenTypeLayoutKind::Substitution) {
			throw std::runtime_error("OpenType GPOS cannot apply a substitution");
		}

		if (lookup_index >= m_Lookups.size() || glyph_index >= m_GlyphCount) {
			throw std::runtime_error("OpenType substitution index exceeds its font or lookup list");
		}
		const auto& lookup = m_Lookups[lookup_index];
		if (!CanApplySingleSubstitution(lookup_index)) {
			throw std::runtime_error("OpenType substitution requires unsupported lookup, filter, or variation behavior");
		}

		for (const auto offset : lookup.SubtableOffsets) {
			const auto target = lookup.Type == 7 ? *ExtensionSingleSubtable(m_LayoutBytes, offset) : offset;
			const auto replacement = ApplySingleSubtable(target, static_cast<std::uint16_t>(glyph_index));
			if (replacement) {
				return replacement;
			}
		}

		return std::nullopt;
	}

	static std::size_t UnitLookupInputCoverage(std::span<const std::uint8_t> bytes, std::size_t table, std::uint16_t type, bool positioning) {
		for (std::size_t work = 0; work < kMaximumLayoutRecords; ++work) {
			const auto format = LayoutWord(bytes, table);
			std::size_t field = table + 2;
			if (type == (positioning ? 9U : 7U) && format == 1) {
				type = LayoutWord(bytes, table + 2);
				table = LayoutOffset(bytes, table, LayoutDoubleWord(bytes, table + 4), 2);
				continue;
			}
			if (((positioning && (type == 1 || type == 2)) || (!positioning && type == 1)) && (format == 1 || format == 2)) {
				return LayoutOffset(bytes, table, LayoutWord(bytes, field), 4);
			}
			if (((positioning && type >= 3 && type <= 6) || (!positioning && ((type >= 2 && type <= 4) || type == 8))) && format == 1) {
				return LayoutOffset(bytes, table, LayoutWord(bytes, field), 4);
			}
			if (type == (positioning ? 7U : 5U)) {
				if (format == 3) {
					field = table + 6;
				} else if (format != 1 && format != 2) {
					break;
				}
				return LayoutOffset(bytes, table, LayoutWord(bytes, field), 4);
			}
			if (type == (positioning ? 8U : 6U)) {
				if (format == 3) {
					const auto input = table + 4 + std::size_t(LayoutWord(bytes, table + 2)) * 2;
					if (LayoutWord(bytes, input) == 0) {
						break;
					}
					field = input + 2;
				} else if (format != 1 && format != 2) {
					break;
				}
				return LayoutOffset(bytes, table, LayoutWord(bytes, field), 4);
			}
			break;
		}
		throw std::runtime_error("OpenType lookup input coverage format or work exceeds its recovered domain");
	}

	bool OpenTypeLayoutFont::LookupInputCoversGlyph(std::size_t lookup_index, std::uint32_t glyph_index) const {
		if (lookup_index >= m_Lookups.size() || glyph_index >= m_GlyphCount) {
			throw std::runtime_error("OpenType lookup coverage requires an actual lookup and glyph index");
		}
		const auto& lookup = m_Lookups[lookup_index];
		if (!lookup.Present) {
			return false;
		}
		for (const auto offset : lookup.SubtableOffsets) {
			const auto coverage = UnitLookupInputCoverage(m_LayoutBytes, offset, lookup.Type, m_Kind == OpenTypeLayoutKind::Positioning);
			static_cast<void>(ValidateCoverage(m_LayoutBytes, coverage, m_GlyphCount));
			if (CoverageIndex(m_LayoutBytes, coverage, static_cast<std::uint16_t>(glyph_index))) {
				return true;
			}
		}
		return false;
	}

	static bool CoveredLigatureMayApplyToIsolatedGlyph(std::span<const std::uint8_t> bytes, std::size_t table, std::size_t coverage_index, std::size_t& work) {
		const auto set_count = LayoutWord(bytes, table + 4);
		static_cast<void>(LayoutRange(bytes, table + 6, std::size_t(set_count) * 2));
		if (coverage_index >= set_count) {
			throw std::runtime_error("OpenType ligature coverage index exceeds its actual set array");
		}
		const auto set = LayoutOffset(bytes, table, LayoutWord(bytes, table + 6 + coverage_index * 2), 2);
		const auto count = LayoutWord(bytes, set);
		AddLayoutRecords(work, count);
		static_cast<void>(LayoutRange(bytes, set + 2, std::size_t(count) * 2));
		for (std::size_t index = 0; index < count; ++index) {
			const auto rule = LayoutOffset(bytes, set, LayoutWord(bytes, set + 2 + index * 2), 4);
			const auto components = LayoutWord(bytes, rule + 2);
			// Native3ffee0 directly substitutes count1; zero/malformed rule semantics stay gated.
			if (components <= 1) {
				return true;
			}
			AddLayoutRecords(work, components - 1);
			static_cast<void>(LayoutRange(bytes, rule + 4, std::size_t(components - 1) * 2));
		}
		return false;
	}

	static bool CoveredChainMayApplyToIsolatedGlyph(std::span<const std::uint8_t> bytes, std::size_t table, std::size_t& work) {
		const auto backtrack = LayoutWord(bytes, table + 2);
		AddLayoutRecords(work, backtrack);
		static_cast<void>(LayoutRange(bytes, table + 4, std::size_t(backtrack) * 2));
		const auto input_field = table + 4 + std::size_t(backtrack) * 2;
		const auto input = LayoutWord(bytes, input_field);
		if (input == 0) {
			throw std::runtime_error("OpenType chained context requires an actual leading input");
		}
		AddLayoutRecords(work, input);
		static_cast<void>(LayoutRange(bytes, input_field + 2, std::size_t(input) * 2));
		const auto lookahead_field = input_field + 2 + std::size_t(input) * 2;
		const auto lookahead = LayoutWord(bytes, lookahead_field);
		AddLayoutRecords(work, lookahead);
		static_cast<void>(LayoutRange(bytes, lookahead_field + 2, std::size_t(lookahead) * 2));
		const auto action_field = lookahead_field + 2 + std::size_t(lookahead) * 2;
		const auto actions = LayoutWord(bytes, action_field);
		AddLayoutRecords(work, actions);
		static_cast<void>(LayoutRange(bytes, action_field + 2, std::size_t(actions) * 4));
		// Native402450 tests input, preceding output and following input before invoking any nested action.
		return backtrack == 0 && input == 1 && lookahead == 0;
	}

	static bool CoveredClassChainMayApplyToIsolatedGlyph(std::span<const std::uint8_t> bytes, std::size_t table, std::uint32_t glyph_index, std::uint16_t glyph_count, std::size_t& work) {
		const auto set_count = LayoutWord(bytes, table + 10);
		AddLayoutRecords(work, set_count);
		static_cast<void>(LayoutRange(bytes, table + 12, std::size_t(set_count) * 2));
		std::uint16_t input_class = 0;
		const auto class_relative = LayoutWord(bytes, table + 6);
		if (class_relative != 0) {
			const auto definition = LayoutOffset(bytes, table, class_relative, 4);
			const auto classes = bytes.subspan(definition);
			AddLayoutRecords(work, LayoutWord(classes, LayoutWord(classes, 0) == 1 ? 4 : 2));
			ValidateGlyphClasses(classes, glyph_count, std::numeric_limits<std::uint16_t>::max());
			input_class = ClassDefinitionValue(classes, glyph_index);
		}
		// Native3ff480 selects the first-input class, independently of coverage order.
		if (input_class >= set_count) {
			return false;
		}
		const auto relative = LayoutWord(bytes, table + 12 + std::size_t(input_class) * 2);
		if (relative == 0) {
			return false;
		}
		const auto set = LayoutOffset(bytes, table, relative, 2);
		const auto rules = LayoutWord(bytes, set);
		AddLayoutRecords(work, rules);
		static_cast<void>(LayoutRange(bytes, set + 2, std::size_t(rules) * 2));
		for (std::size_t index = 0; index < rules; ++index) {
			const auto rule = LayoutOffset(bytes, set, LayoutWord(bytes, set + 2 + index * 2), 2);
			const auto backtrack = LayoutWord(bytes, rule);
			AddLayoutRecords(work, backtrack);
			static_cast<void>(LayoutRange(bytes, rule + 2, std::size_t(backtrack) * 2));
			const auto input_field = rule + 2 + std::size_t(backtrack) * 2;
			const auto input = LayoutWord(bytes, input_field);
			if (input == 0) {
				throw std::runtime_error("OpenType chained class context requires an actual leading input");
			}
			AddLayoutRecords(work, input);
			static_cast<void>(LayoutRange(bytes, input_field + 2, std::size_t(input - 1) * 2));
			const auto lookahead_field = input_field + std::size_t(input) * 2;
			const auto lookahead = LayoutWord(bytes, lookahead_field);
			AddLayoutRecords(work, lookahead);
			static_cast<void>(LayoutRange(bytes, lookahead_field + 2, std::size_t(lookahead) * 2));
			const auto action_field = lookahead_field + 2 + std::size_t(lookahead) * 2;
			const auto actions = LayoutWord(bytes, action_field);
			AddLayoutRecords(work, actions);
			static_cast<void>(LayoutRange(bytes, action_field + 2, std::size_t(actions) * 4));
			// Native3ff740 forwards these counts to402450 before any nested action.
			if (backtrack == 0 && input == 1 && lookahead == 0) {
				return true;
			}
		}
		return false;
	}

	bool OpenTypeLayoutFont::LookupMayApplyToIsolatedGlyph(std::size_t lookup_index, std::uint32_t glyph_index) const {
		if (m_Kind != OpenTypeLayoutKind::Substitution || lookup_index >= m_Lookups.size() || glyph_index >= m_GlyphCount) {
			throw std::runtime_error("OpenType isolated input requires an actual GSUB lookup and glyph index");
		}
		const auto& lookup = m_Lookups[lookup_index];
		if (!lookup.Present) {
			return false;
		}
		std::size_t work = 0;
		for (const auto offset : lookup.SubtableOffsets) {
			auto type = lookup.Type;
			auto table = std::size_t(offset);
			while (type == 7 && LayoutWord(m_LayoutBytes, table) == 1) {
				AddLayoutRecords(work, 1);
				type = LayoutWord(m_LayoutBytes, table + 2);
				table = LayoutOffset(m_LayoutBytes, table, LayoutDoubleWord(m_LayoutBytes, table + 4), 2);
			}
			const auto coverage = UnitLookupInputCoverage(m_LayoutBytes, table, type, false);
			AddLayoutRecords(work, ValidateCoverage(m_LayoutBytes, coverage, m_GlyphCount));
			const auto covered = CoverageIndex(m_LayoutBytes, coverage, static_cast<std::uint16_t>(glyph_index));
			if (!covered) {
				continue;
			}
			if (lookup.Flags != 0 || m_FeatureVariationsOffset != 0) {
				return true;
			}
			const auto format = LayoutWord(m_LayoutBytes, table);
			if (type == 4 && format == 1) {
				if (CoveredLigatureMayApplyToIsolatedGlyph(m_LayoutBytes, table, *covered, work)) {
					return true;
				}
			} else if (type == 6 && format == 2) {
				if (CoveredClassChainMayApplyToIsolatedGlyph(m_LayoutBytes, table, glyph_index, m_GlyphCount, work)) {
					return true;
				}
			} else if (type == 6 && format == 3) {
				if (CoveredChainMayApplyToIsolatedGlyph(m_LayoutBytes, table, work)) {
					return true;
				}
			} else {
				return true;
			}
		}
		return false;
	}

	bool OpenTypeLayoutFont::CanApplyLigatureSubstitution(std::size_t lookup_index) const {
		if (m_AdmittedMetadata || m_Kind != OpenTypeLayoutKind::Substitution || m_FeatureVariationsOffset != 0 || lookup_index >= m_Lookups.size()) {
			return false;
		}
		const auto& lookup = m_Lookups[lookup_index];
		return lookup.Present && lookup.Type == 4 && lookup.Flags == 0 && std::all_of(lookup.SubtableOffsets.begin(), lookup.SubtableOffsets.end(), [&](auto offset) {
			return LayoutWord(m_LayoutBytes, offset) == 1;
		});
	}

	std::optional<OpenTypeLigatureMatch> OpenTypeLayoutFont::MatchLigatureSubstitution(std::size_t lookup_index, std::span<const std::uint32_t> glyphs, std::size_t& work) const {
		if (!CanApplyLigatureSubstitution(lookup_index) || glyphs.empty() || glyphs.size() > 65536) {
			throw std::runtime_error("OpenType ligature requires actual consecutive base glyphs and a flags-zero format1 lookup");
		}
		AddLayoutRecords(work, glyphs.size());
		for (const auto glyph : glyphs) {
			if (glyph >= m_GlyphCount) {
				throw std::runtime_error("OpenType ligature input glyph exceeds the font");
			}
		}
		for (const auto table : m_Lookups[lookup_index].SubtableOffsets) {
			const auto coverage = LayoutOffset(m_LayoutBytes, table, LayoutWord(m_LayoutBytes, table + 2), 4);
			AddLayoutRecords(work, ValidateCoverage(m_LayoutBytes, coverage, m_GlyphCount));
			const auto covered = CoverageIndex(m_LayoutBytes, coverage, static_cast<std::uint16_t>(glyphs.front()));
			if (!covered) {
				continue;
			}
			const auto count = LayoutWord(m_LayoutBytes, table + 4);
			static_cast<void>(LayoutRange(m_LayoutBytes, table + 6, std::size_t(count) * 2));
			if (*covered >= count) {
				throw std::runtime_error("OpenType ligature coverage exceeds its actual set array");
			}
			const auto set = LayoutOffset(m_LayoutBytes, table, LayoutWord(m_LayoutBytes, table + 6 + *covered * 2), 2);
			const auto rules = LayoutWord(m_LayoutBytes, set);
			AddLayoutRecords(work, rules);
			static_cast<void>(LayoutRange(m_LayoutBytes, set + 2, std::size_t(rules) * 2));
			std::optional<OpenTypeLigatureMatch> result;
			for (std::size_t index = 0; index < rules; ++index) {
				const auto rule = LayoutOffset(m_LayoutBytes, set, LayoutWord(m_LayoutBytes, set + 2 + index * 2), 4);
				OpenTypeLigatureMatch candidate;
				candidate.GlyphIndex = LayoutWord(m_LayoutBytes, rule);
				candidate.ComponentCount = LayoutWord(m_LayoutBytes, rule + 2);
				if (candidate.GlyphIndex >= m_GlyphCount || candidate.ComponentCount == 0 || candidate.ComponentCount > 64) {
					throw std::runtime_error("OpenType ligature replacement or component count exceeds the recovered domain");
				}
				AddLayoutRecords(work, std::size_t(candidate.ComponentCount) - 1);
				static_cast<void>(LayoutRange(m_LayoutBytes, rule + 4, std::size_t(candidate.ComponentCount - 1) * 2));
				bool matches = candidate.ComponentCount <= glyphs.size();
				for (std::size_t component = 1; component < candidate.ComponentCount; ++component) {
					const auto expected = LayoutWord(m_LayoutBytes, rule + 2 + component * 2);
					if (expected >= m_GlyphCount) {
						throw std::runtime_error("OpenType ligature component glyph exceeds the font");
					}
					matches = matches && glyphs[component] == expected;
				}
				if (matches && !result) {
					result = candidate;
				}
			}
			if (result) {
				return result;
			}
		}
		return std::nullopt;
	}

	bool OpenTypeLayoutFont::LookupRequiresFollowingGlyph(std::size_t lookup_index) const {
		if (lookup_index >= m_Lookups.size()) {
			throw std::runtime_error("OpenType isolated positioning lookup index exceeds its actual list");
		}
		const auto& lookup = m_Lookups[lookup_index];
		if (m_AdmittedMetadata || m_Kind != OpenTypeLayoutKind::Positioning || m_FeatureVariationsOffset != 0 || !lookup.Present || lookup.Type != 9 || (lookup.Flags & ~0x10U) != 0 || lookup.SubtableOffsets.empty()) {
			return false;
		}
		std::size_t work = 0;
		for (const auto extension : lookup.SubtableOffsets) {
			AddLayoutRecords(work, 1);
			if (LayoutWord(m_LayoutBytes, extension) != 1 || LayoutWord(m_LayoutBytes, extension + 2) != 2) {
				return false;
			}
			const auto target = LayoutOffset(m_LayoutBytes, extension, LayoutDoubleWord(m_LayoutBytes, extension + 4), 2);
			const auto format = LayoutWord(m_LayoutBytes, target);
			if (format != 1 && format != 2) {
				return false;
			}
			static_cast<void>(LayoutRange(m_LayoutBytes, target, format == 1 ? 10 : 16));
		}
		// Native400bb0/400ce0 request one following glyph;404f80 rejects before filters/values when none exists.
		return true;
	}

	bool OpenTypeLayoutFont::CanApplyPairPositioning(std::size_t lookup_index) const {
		if (m_AdmittedMetadata || m_Kind != OpenTypeLayoutKind::Positioning || m_FeatureVariationsOffset != 0 || lookup_index >= m_Lookups.size()) {
			return false;
		}
		const auto& lookup = m_Lookups[lookup_index];
		return lookup.Present && lookup.Type == 2 && lookup.Flags == 0 && std::all_of(lookup.SubtableOffsets.begin(), lookup.SubtableOffsets.end(), [&](auto offset) {
			return (LayoutWord(m_LayoutBytes, offset) == 1 || LayoutWord(m_LayoutBytes, offset) == 2) && (LayoutWord(m_LayoutBytes, offset + 4) & 0xfff0U) == 0 && (LayoutWord(m_LayoutBytes, offset + 6) & 0xfff0U) == 0;
		});
	}

	static std::size_t PairValueWords(std::uint16_t format) {
		return std::size_t((format & 1U) != 0) + std::size_t((format & 2U) != 0) + std::size_t((format & 4U) != 0) + std::size_t((format & 8U) != 0);
	}

	static std::array<std::int16_t, 4> ReadPairValue(std::span<const std::uint8_t> bytes, std::size_t offset, std::uint16_t format) {
		std::array<std::int16_t, 4> result{};
		for (std::size_t field = 0; field < result.size(); ++field) {
			if ((format & (1U << field)) != 0) {
				const auto word = LayoutWord(bytes, offset);
				result[field] = static_cast<std::int16_t>(word < 0x8000 ? std::int32_t(word) : std::int32_t(word) - 0x10000);
				offset += 2;
			}
		}
		return result;
	}

	std::optional<OpenTypePairAdjustment> OpenTypeLayoutFont::MatchPairPositioning(std::size_t lookup_index, std::uint32_t first, std::uint32_t second, std::size_t& work) const {
		if (!CanApplyPairPositioning(lookup_index) || first >= m_GlyphCount || second >= m_GlyphCount) {
			throw std::runtime_error("OpenType pair positioning requires actual glyphs and flags-zero pair formats1/2 without device values");
		}
		for (const auto table : m_Lookups[lookup_index].SubtableOffsets) {
			const auto coverage = LayoutOffset(m_LayoutBytes, table, LayoutWord(m_LayoutBytes, table + 2), 4);
			AddLayoutRecords(work, ValidateCoverage(m_LayoutBytes, coverage, m_GlyphCount));
			const auto covered = CoverageIndex(m_LayoutBytes, coverage, static_cast<std::uint16_t>(first));
			if (!covered) {
				continue;
			}
			if (LayoutWord(m_LayoutBytes, table) == 2) {
				const auto first_count = LayoutWord(m_LayoutBytes, table + 12);
				const auto second_count = LayoutWord(m_LayoutBytes, table + 14);
				if (first_count == 0 || second_count == 0) {
					throw std::runtime_error("OpenType pair class matrix requires actual nonzero dimensions");
				}
				const auto first_class = LayoutOffset(m_LayoutBytes, table, LayoutWord(m_LayoutBytes, table + 8), 4);
				const auto second_class = LayoutOffset(m_LayoutBytes, table, LayoutWord(m_LayoutBytes, table + 10), 4);
				const auto first_classes = m_LayoutBytes.subspan(first_class);
				const auto second_classes = m_LayoutBytes.subspan(second_class);
				ValidateGlyphClasses(first_classes, m_GlyphCount, static_cast<std::uint16_t>(first_count - 1));
				ValidateGlyphClasses(second_classes, m_GlyphCount, static_cast<std::uint16_t>(second_count - 1));
				const auto entries = std::size_t(first_count) * second_count;
				AddLayoutRecords(work, entries);
				const auto first_format = LayoutWord(m_LayoutBytes, table + 4);
				const auto second_format = LayoutWord(m_LayoutBytes, table + 6);
				const auto first_words = PairValueWords(first_format);
				const auto stride = (first_words + PairValueWords(second_format)) * 2;
				static_cast<void>(LayoutRange(m_LayoutBytes, table + 16, entries * stride));
				const auto row = ClassDefinitionValue(first_classes, first);
				const auto column = ClassDefinitionValue(second_classes, second);
				const auto record = table + 16 + (std::size_t(row) * second_count + column) * stride;
				OpenTypePairAdjustment result;
				result.First = ReadPairValue(m_LayoutBytes, record, first_format);
				result.Second = ReadPairValue(m_LayoutBytes, record + first_words * 2, second_format);
				result.HasSecondValue = second_format != 0;
				return result;
			}
			const auto count = LayoutWord(m_LayoutBytes, table + 8);
			static_cast<void>(LayoutRange(m_LayoutBytes, table + 10, std::size_t(count) * 2));
			if (*covered >= count) {
				throw std::runtime_error("OpenType pair coverage exceeds its actual set array");
			}
			const auto set = LayoutOffset(m_LayoutBytes, table, LayoutWord(m_LayoutBytes, table + 10 + *covered * 2), 2);
			const auto pairs = LayoutWord(m_LayoutBytes, set);
			const auto first_format = LayoutWord(m_LayoutBytes, table + 4);
			const auto second_format = LayoutWord(m_LayoutBytes, table + 6);
			const auto first_words = PairValueWords(first_format);
			const auto stride = (1 + first_words + PairValueWords(second_format)) * 2;
			AddLayoutRecords(work, pairs);
			static_cast<void>(LayoutRange(m_LayoutBytes, set + 2, std::size_t(pairs) * stride));
			std::optional<OpenTypePairAdjustment> result;
			std::uint32_t previous = 0;
			for (std::size_t pair = 0; pair < pairs; ++pair) {
				const auto record = set + 2 + pair * stride;
				const auto glyph = LayoutWord(m_LayoutBytes, record);
				if (glyph >= m_GlyphCount || (pair != 0 && glyph <= previous)) {
					throw std::runtime_error("OpenType pair records are unordered or outside the font");
				}
				previous = glyph;
				if (glyph == second) {
					OpenTypePairAdjustment adjustment;
					adjustment.First = ReadPairValue(m_LayoutBytes, record + 2, first_format);
					adjustment.Second = ReadPairValue(m_LayoutBytes, record + 2 + first_words * 2, second_format);
					adjustment.HasSecondValue = second_format != 0;
					result = adjustment;
				}
			}
			if (result) {
				return result;
			}
		}
		return std::nullopt;
	}

} // namespace sgre::resource
