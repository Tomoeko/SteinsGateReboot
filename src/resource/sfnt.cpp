#include "sgre/resource/sfnt.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>

namespace sgre::resource {

	static constexpr std::size_t kMaximumFontBytes = 64 * 1024 * 1024;
	static constexpr std::size_t kMaximumTables = 512;
	static constexpr std::size_t kMaximumCharacterMaps = 64;
	static constexpr std::uint32_t kTrueTypeSignature = 0x00010000;
	static constexpr std::uint32_t kCffSignature = 0x4f54544f;

	static std::span<const std::uint8_t> ReadRange(std::span<const std::uint8_t> bytes, std::size_t offset, std::size_t size, std::string_view context) {
		if (offset > bytes.size() || size > bytes.size() - offset) {
			throw std::runtime_error("sfnt " + std::string(context) + " exceeds its resource range");
		}

		return bytes.subspan(offset, size);
	}

	static std::uint16_t ReadBig16(std::span<const std::uint8_t> bytes, std::size_t offset) {
		const auto field = ReadRange(bytes, offset, 2, "16-bit field");

		return static_cast<std::uint16_t>((std::uint32_t(field[0]) << 8) | field[1]);
	}

	static std::uint32_t ReadBig32(std::span<const std::uint8_t> bytes, std::size_t offset) {
		const auto field = ReadRange(bytes, offset, 4, "32-bit field");

		return (std::uint32_t(field[0]) << 24) | (std::uint32_t(field[1]) << 16) | (std::uint32_t(field[2]) << 8) | field[3];
	}

	static void ValidateMetricTable(std::span<const std::uint8_t> bytes, std::uint16_t metric_count, std::uint16_t glyph_count, std::string_view tag) {
		if (metric_count == 0 || metric_count > glyph_count) {
			throw std::runtime_error("sfnt " + std::string(tag) + " metric count is outside the glyph range");
		}

		const auto required_size = std::size_t(metric_count) * 4 + std::size_t(glyph_count - metric_count) * 2;
		static_cast<void>(ReadRange(bytes, 0, required_size, tag));
	}

	static std::uint32_t CharacterMapSize(std::span<const std::uint8_t> bytes, std::uint16_t format) {
		switch (format) {
		case 0:
		case 2:
		case 4:
		case 6:
			return ReadBig16(bytes, 2);
		case 8:
		case 10:
		case 12:
		case 13:
			return ReadBig32(bytes, 4);
		case 14:
			return ReadBig32(bytes, 2);
		default:
			throw std::runtime_error("sfnt character map has an unsupported format " + std::to_string(format));
		}
	}

	SfntFont::SfntFont(std::span<const std::uint8_t> bytes) : m_Bytes(bytes) {
		if (bytes.size() > kMaximumFontBytes) {
			throw std::runtime_error("sfnt resource exceeds the font byte limit");
		}

		ReadDirectory();
		ReadFontMetrics();
		ReadCharacterMaps();
	}

	void SfntFont::ReadDirectory() {
		static_cast<void>(ReadRange(m_Bytes, 0, 12, "table directory"));
		const auto signature = ReadBig32(m_Bytes, 0);
		if (signature != kTrueTypeSignature && signature != kCffSignature) {
			throw std::runtime_error("sfnt resource has an unsupported outline signature or font collection");
		}

		m_OutlineKind = signature == kCffSignature ? SfntOutlineKind::Cff : SfntOutlineKind::TrueType;
		const auto table_count = ReadBig16(m_Bytes, 4);
		if (table_count == 0 || table_count > kMaximumTables) {
			throw std::runtime_error("sfnt table directory count exceeds its limit");
		}

		static_cast<void>(ReadRange(m_Bytes, 12, std::size_t(table_count) * 16, "table records"));
		m_Tables.reserve(table_count);
		for (std::size_t index = 0; index < table_count; ++index) {
			const auto offset = 12 + index * 16;
			SfntTable table;
			for (std::size_t character = 0; character < 4; ++character) {
				table.Tag.push_back(static_cast<char>(m_Bytes[offset + character]));
			}

			table.Checksum = ReadBig32(m_Bytes, offset + 4);
			table.Offset = ReadBig32(m_Bytes, offset + 8);
			table.Size = ReadBig32(m_Bytes, offset + 12);
			static_cast<void>(ReadRange(m_Bytes, table.Offset, table.Size, table.Tag));
			if (std::ranges::any_of(m_Tables, [&](const SfntTable& previous) {
				    return previous.Tag == table.Tag;
			    })) {
				throw std::runtime_error("sfnt table directory repeats tag " + table.Tag);
			}

			m_Tables.push_back(table);
		}

		if (FindTable("fvar")) {
			throw std::runtime_error("sfnt variable-font metrics are not recovered");
		}

		if (m_OutlineKind == SfntOutlineKind::Cff) {
			static_cast<void>(RequiredTable("CFF "));
		} else {
			static_cast<void>(RequiredTable("glyf"));
			static_cast<void>(RequiredTable("loca"));
		}
	}

	std::optional<std::span<const std::uint8_t>> SfntFont::FindTable(std::string_view tag) const {
		const auto found = std::ranges::find_if(m_Tables, [&](const SfntTable& table) {
			return table.Tag == tag;
		});
		if (found == m_Tables.end()) {
			return std::nullopt;
		}

		return ReadRange(m_Bytes, found->Offset, found->Size, tag);
	}

	std::span<const std::uint8_t> SfntFont::RequiredTable(std::string_view tag) const {
		const auto table = FindTable(tag);
		if (!table) {
			throw std::runtime_error("sfnt resource is missing required table " + std::string(tag));
		}

		return *table;
	}

	void SfntFont::ReadFontMetrics() {
		const auto head = RequiredTable("head");
		static_cast<void>(ReadRange(head, 0, 54, "head"));
		if (ReadBig32(head, 0) != 0x00010000 || ReadBig32(head, 12) != 0x5f0f3cf5) {
			throw std::runtime_error("sfnt head version or magic is invalid");
		}

		m_UnitsPerEm = ReadBig16(head, 18);
		if (m_UnitsPerEm < 16 || m_UnitsPerEm > 16384) {
			throw std::runtime_error("sfnt head units per em are outside the format range");
		}

		const auto maximum_profile = RequiredTable("maxp");
		const auto profile_version = ReadBig32(maximum_profile, 0);
		if ((m_OutlineKind == SfntOutlineKind::TrueType && profile_version != 0x00010000) || (m_OutlineKind == SfntOutlineKind::Cff && profile_version != 0x00005000)) {
			throw std::runtime_error("sfnt maxp version does not match its outline format");
		}

		static_cast<void>(ReadRange(maximum_profile, 0, m_OutlineKind == SfntOutlineKind::TrueType ? 32 : 6, "maxp"));
		m_GlyphCount = ReadBig16(maximum_profile, 4);
		if (m_GlyphCount == 0) {
			throw std::runtime_error("sfnt maxp glyph count is zero");
		}

		const auto horizontal_header = RequiredTable("hhea");
		static_cast<void>(ReadRange(horizontal_header, 0, 36, "hhea"));
		if (ReadBig32(horizontal_header, 0) != 0x00010000 || ReadBig16(horizontal_header, 32) != 0) {
			throw std::runtime_error("sfnt hhea version or metric format is unsupported");
		}

		m_HorizontalMetricCount = ReadBig16(horizontal_header, 34);
		ValidateMetricTable(RequiredTable("hmtx"), m_HorizontalMetricCount, m_GlyphCount, "hmtx");
		const auto vertical_header = FindTable("vhea");
		const auto vertical_metrics = FindTable("vmtx");
		if (vertical_header.has_value() != vertical_metrics.has_value()) {
			throw std::runtime_error("sfnt vertical header and metrics are not paired");
		}

		if (vertical_header) {
			static_cast<void>(ReadRange(*vertical_header, 0, 36, "vhea"));
			const auto version = ReadBig32(*vertical_header, 0);
			if ((version != 0x00010000 && version != 0x00011000) || ReadBig16(*vertical_header, 32) != 0) {
				throw std::runtime_error("sfnt vhea version or metric format is unsupported");
			}

			m_VerticalMetricCount = ReadBig16(*vertical_header, 34);
			ValidateMetricTable(*vertical_metrics, m_VerticalMetricCount, m_GlyphCount, "vmtx");
		}
	}

	void SfntFont::ReadCharacterMaps() {
		const auto table = RequiredTable("cmap");
		if (ReadBig16(table, 0) != 0) {
			throw std::runtime_error("sfnt cmap version is unsupported");
		}

		const auto map_count = ReadBig16(table, 2);
		if (map_count == 0 || map_count > kMaximumCharacterMaps) {
			throw std::runtime_error("sfnt character-map count exceeds its limit");
		}

		static_cast<void>(ReadRange(table, 4, std::size_t(map_count) * 8, "cmap records"));
		std::optional<std::size_t> selected;
		m_CharacterMaps.reserve(map_count);
		for (std::size_t index = 0; index < map_count; ++index) {
			const auto offset = 4 + index * 8;
			SfntCharacterMap map;
			map.PlatformId = ReadBig16(table, offset);
			map.EncodingId = ReadBig16(table, offset + 2);
			map.Offset = ReadBig32(table, offset + 4);
			static_cast<void>(ReadRange(table, map.Offset, 0, "cmap subtable"));
			const auto available = table.subspan(map.Offset);
			map.Format = ReadBig16(available, 0);
			map.Size = CharacterMapSize(available, map.Format);
			if (map.Size < (map.Format == 14 ? 10u : map.Format >= 8 ? 16u
			                                                         : 6u)) {
				throw std::runtime_error("sfnt character-map header length is invalid");
			}

			const auto subtable = ReadRange(table, map.Offset, map.Size, "cmap subtable");
			if (map.Format == 4) {
				ValidateSegmentMap(subtable);
			} else if (map.Format == 12) {
				ValidateCoverageMap(subtable);
			}

			if (!selected && ((map.PlatformId == 0 && map.EncodingId == 3) || (map.PlatformId == 3 && map.EncodingId == 1))) {
				selected = index;
			}

			m_CharacterMaps.push_back(map);
		}

		if (!selected) {
			throw std::runtime_error("sfnt font lacks a recovered native-preferred character map");
		}

		m_SelectedCharacterMap = *selected;
		if (m_CharacterMaps[*selected].Format != 4 && m_CharacterMaps[*selected].Format != 12) {
			throw std::runtime_error("sfnt native-preferred character-map format is not implemented");
		}
	}

	void SfntFont::ValidateSegmentMap(std::span<const std::uint8_t> bytes) const {
		const auto doubled_count = ReadBig16(bytes, 6);
		if (doubled_count == 0 || doubled_count % 2 != 0) {
			throw std::runtime_error("sfnt format 4 segment count is invalid");
		}

		const auto count = std::size_t(doubled_count / 2);
		const auto glyph_array = 16 + count * 8;
		static_cast<void>(ReadRange(bytes, 0, glyph_array, "format 4 segment arrays"));
		if (ReadBig16(bytes, 14 + count * 2) != 0) {
			throw std::runtime_error("sfnt format 4 reserved pad is nonzero");
		}

		std::uint32_t previous_end = 0;
		for (std::size_t index = 0; index < count; ++index) {
			const auto end = ReadBig16(bytes, 14 + index * 2);
			const auto start = ReadBig16(bytes, 16 + count * 2 + index * 2);
			if (start > end || (index != 0 && start <= previous_end)) {
				throw std::runtime_error("sfnt format 4 segments are unordered or overlap");
			}

			const auto range_word = 16 + count * 6 + index * 2;
			const auto range_offset = ReadBig16(bytes, range_word);
			if (range_offset != 0) {
				const auto first_glyph = range_word + range_offset;
				if (range_offset % 2 != 0 || first_glyph < glyph_array) {
					throw std::runtime_error("sfnt format 4 glyph array offset is invalid");
				}

				static_cast<void>(ReadRange(bytes, first_glyph, (std::size_t(end) - start + 1) * 2, "format 4 glyph array"));
			}

			previous_end = end;
		}

		if (ReadBig16(bytes, 14 + (count - 1) * 2) != 0xffff || ReadBig16(bytes, 16 + count * 2 + (count - 1) * 2) != 0xffff) {
			throw std::runtime_error("sfnt format 4 terminal segment is missing");
		}

		for (std::uint32_t character = 0; character <= 0xffff; ++character) {
			if (MapSegmentCharacter(bytes, static_cast<std::uint16_t>(character)) >= m_GlyphCount) {
				throw std::runtime_error("sfnt format 4 mapped glyph exceeds the maxp glyph count");
			}
		}
	}

	void SfntFont::ValidateCoverageMap(std::span<const std::uint8_t> bytes) const {
		if (ReadBig16(bytes, 2) != 0) {
			throw std::runtime_error("sfnt format 12 reserved field is nonzero");
		}

		const auto count = ReadBig32(bytes, 12);
		if (count > 0x110000) {
			throw std::runtime_error("sfnt format 12 group count exceeds the Unicode range");
		}

		static_cast<void>(ReadRange(bytes, 16, std::size_t(count) * 12, "format 12 groups"));
		std::uint32_t previous_end = 0;
		for (std::size_t index = 0; index < count; ++index) {
			const auto offset = 16 + index * 12;
			const auto start = ReadBig32(bytes, offset);
			const auto end = ReadBig32(bytes, offset + 4);
			const auto first_glyph = ReadBig32(bytes, offset + 8);
			if (start > end || end > 0x10ffff || (index != 0 && start <= previous_end)) {
				throw std::runtime_error("sfnt format 12 groups are unordered or outside Unicode");
			}

			if (std::uint64_t(first_glyph) + end - start >= m_GlyphCount) {
				throw std::runtime_error("sfnt format 12 mapped glyph exceeds the maxp glyph count");
			}

			previous_end = end;
		}
	}

	std::uint16_t SfntFont::MapSegmentCharacter(std::span<const std::uint8_t> bytes, std::uint16_t character) const {
		const auto count = std::size_t(ReadBig16(bytes, 6) / 2);
		std::size_t first = 0;
		std::size_t last = count;
		while (first < last) {
			const auto middle = first + (last - first) / 2;
			if (ReadBig16(bytes, 14 + middle * 2) < character) {
				first = middle + 1;
			} else {
				last = middle;
			}
		}

		if (first == count || ReadBig16(bytes, 16 + count * 2 + first * 2) > character) {
			return 0;
		}

		const auto delta = ReadBig16(bytes, 16 + count * 4 + first * 2);
		const auto range_word = 16 + count * 6 + first * 2;
		const auto range_offset = ReadBig16(bytes, range_word);
		if (range_offset == 0) {
			return static_cast<std::uint16_t>(std::uint32_t(character) + delta);
		}

		const auto start = ReadBig16(bytes, 16 + count * 2 + first * 2);
		const auto glyph = ReadBig16(bytes, range_word + range_offset + std::size_t(character - start) * 2);

		return glyph == 0 ? 0 : static_cast<std::uint16_t>(std::uint32_t(glyph) + delta);
	}

	std::uint16_t SfntFont::MapCoverageCharacter(std::span<const std::uint8_t> bytes, std::uint32_t character) const {
		std::size_t first = 0;
		std::size_t last = ReadBig32(bytes, 12);
		while (first < last) {
			const auto middle = first + (last - first) / 2;
			if (ReadBig32(bytes, 16 + middle * 12 + 4) < character) {
				first = middle + 1;
			} else {
				last = middle;
			}
		}

		if (first == ReadBig32(bytes, 12)) {
			return 0;
		}

		const auto start = ReadBig32(bytes, 16 + first * 12);
		if (start > character) {
			return 0;
		}

		return static_cast<std::uint16_t>(ReadBig32(bytes, 16 + first * 12 + 8) + character - start);
	}

	std::uint16_t SfntFont::MapCharacter(std::uint32_t character) const {
		return MapCharacter(character, m_SelectedCharacterMap);
	}

	std::uint16_t SfntFont::MapCharacter(std::uint32_t character, std::size_t character_map) const {
		if (character_map >= m_CharacterMaps.size()) {
			throw std::runtime_error("sfnt character-map index is outside its font owner");
		}

		const auto& map = m_CharacterMaps[character_map];
		const auto bytes = ReadRange(RequiredTable("cmap"), map.Offset, map.Size, "selected cmap");
		if (map.Format == 4) {
			return character <= 0xffff ? MapSegmentCharacter(bytes, static_cast<std::uint16_t>(character)) : 0;
		}

		if (map.Format == 12) {
			return character <= 0x10ffff ? MapCoverageCharacter(bytes, character) : 0;
		}

		throw std::runtime_error("sfnt character-map lookup format is not implemented: " + std::to_string(map.Format));
	}

	SfntGlyphMetrics SfntFont::ReadGlyphMetrics(std::span<const std::uint8_t> bytes, std::uint16_t metric_count, std::uint32_t glyph_index) const {
		if (glyph_index >= m_GlyphCount) {
			throw std::runtime_error("sfnt metric glyph index is outside its font owner");
		}

		const auto advance_index = std::min(glyph_index, std::uint32_t(metric_count - 1));
		const auto bearing_offset = glyph_index < metric_count ? std::size_t(glyph_index) * 4 + 2 : std::size_t(metric_count) * 4 + std::size_t(glyph_index - metric_count) * 2;
		SfntGlyphMetrics metrics;
		metrics.Advance = ReadBig16(bytes, std::size_t(advance_index) * 4);
		metrics.SideBearing = std::bit_cast<std::int16_t>(ReadBig16(bytes, bearing_offset));

		return metrics;
	}

	SfntGlyphMetrics SfntFont::HorizontalMetrics(std::uint32_t glyph_index) const {
		return ReadGlyphMetrics(RequiredTable("hmtx"), m_HorizontalMetricCount, glyph_index);
	}

	std::optional<SfntGlyphMetrics> SfntFont::VerticalMetrics(std::uint32_t glyph_index) const {
		if (glyph_index >= m_GlyphCount) {
			throw std::runtime_error("sfnt vertical metric glyph index is outside its font owner");
		}

		if (m_VerticalMetricCount == 0) {
			return std::nullopt;
		}

		return ReadGlyphMetrics(RequiredTable("vmtx"), m_VerticalMetricCount, glyph_index);
	}

	static std::uint32_t ComputeChecksum(std::span<const std::uint8_t> bytes, bool clear_adjustment) {
		std::uint32_t sum = 0;
		for (std::size_t offset = 0; offset < bytes.size(); offset += 4) {
			std::uint32_t word = 0;
			for (std::size_t byte = 0; byte < 4; ++byte) {
				word <<= 8;
				if (offset + byte < bytes.size() && (!clear_adjustment || offset + byte < 8 || offset + byte >= 12)) {
					word |= bytes[offset + byte];
				}
			}

			sum += word;
		}

		return sum;
	}

	void SfntFont::ValidateChecksums() const {
		for (const auto& table : m_Tables) {
			const auto bytes = ReadRange(m_Bytes, table.Offset, table.Size, table.Tag);
			if (ComputeChecksum(bytes, table.Tag == "head") != table.Checksum) {
				throw std::runtime_error("sfnt table checksum differs for " + table.Tag);
			}
		}

		if (ComputeChecksum(m_Bytes, false) != 0xb1b0afba) {
			throw std::runtime_error("sfnt whole-font checksum adjustment is invalid");
		}
	}

	SfntOutlineKind SfntFont::OutlineKind() const {
		return m_OutlineKind;
	}

	std::uint16_t SfntFont::GlyphCount() const {
		return m_GlyphCount;
	}

	std::uint16_t SfntFont::UnitsPerEm() const {
		return m_UnitsPerEm;
	}

	std::span<const SfntTable> SfntFont::Tables() const {
		return m_Tables;
	}

	std::span<const SfntCharacterMap> SfntFont::CharacterMaps() const {
		return m_CharacterMaps;
	}

	std::size_t SfntFont::SelectedCharacterMap() const {
		return m_SelectedCharacterMap;
	}

} // namespace sgre::resource
