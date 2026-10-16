#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sgre::resource {

	enum class SfntOutlineKind {
		TrueType,
		Cff
	};

	struct SfntTable {
		std::string Tag;
		std::uint32_t Checksum = 0;
		std::uint32_t Offset = 0;
		std::uint32_t Size = 0;
	};

	struct SfntCharacterMap {
		std::uint16_t PlatformId = 0;
		std::uint16_t EncodingId = 0;
		std::uint16_t Format = 0;
		std::uint32_t Offset = 0;
		std::uint32_t Size = 0;
	};

	struct SfntGlyphMetrics {
		std::uint16_t Advance = 0;
		std::int16_t SideBearing = 0;
	};

	class SfntFont {
	public:
		// The immutable resource bytes must outlive this reader and every table view it returns.
		explicit SfntFont(std::span<const std::uint8_t> bytes);
		[[nodiscard]] SfntOutlineKind OutlineKind() const;
		[[nodiscard]] std::uint16_t GlyphCount() const;
		[[nodiscard]] std::uint16_t UnitsPerEm() const;
		[[nodiscard]] std::span<const SfntTable> Tables() const;
		[[nodiscard]] std::span<const SfntCharacterMap> CharacterMaps() const;
		[[nodiscard]] std::size_t SelectedCharacterMap() const;
		[[nodiscard]] std::optional<std::span<const std::uint8_t>> FindTable(std::string_view tag) const;
		[[nodiscard]] std::uint16_t MapCharacter(std::uint32_t character) const;
		// Explicit maps support formats 4 and 12. Other observed maps retain their metadata.
		[[nodiscard]] std::uint16_t MapCharacter(std::uint32_t character, std::size_t character_map) const;
		[[nodiscard]] SfntGlyphMetrics HorizontalMetrics(std::uint32_t glyph_index) const;
		// Missing vertical tables return no metrics; no synthesized vertical fallback is inferred.
		[[nodiscard]] std::optional<SfntGlyphMetrics> VerticalMetrics(std::uint32_t glyph_index) const;
		void ValidateChecksums() const;

	private:
		void ReadDirectory();
		void ReadFontMetrics();
		void ReadCharacterMaps();
		void ValidateSegmentMap(std::span<const std::uint8_t> bytes) const;
		void ValidateCoverageMap(std::span<const std::uint8_t> bytes) const;
		[[nodiscard]] std::uint16_t MapSegmentCharacter(std::span<const std::uint8_t> bytes, std::uint16_t character) const;
		[[nodiscard]] std::uint16_t MapCoverageCharacter(std::span<const std::uint8_t> bytes, std::uint32_t character) const;
		[[nodiscard]] std::span<const std::uint8_t> RequiredTable(std::string_view tag) const;
		[[nodiscard]] SfntGlyphMetrics ReadGlyphMetrics(std::span<const std::uint8_t> bytes, std::uint16_t metric_count, std::uint32_t glyph_index) const;

		std::span<const std::uint8_t> m_Bytes;
		SfntOutlineKind m_OutlineKind = SfntOutlineKind::TrueType;
		std::vector<SfntTable> m_Tables;
		std::vector<SfntCharacterMap> m_CharacterMaps;
		std::size_t m_SelectedCharacterMap = 0;
		std::uint16_t m_GlyphCount = 0;
		std::uint16_t m_UnitsPerEm = 0;
		std::uint16_t m_HorizontalMetricCount = 0;
		std::uint16_t m_VerticalMetricCount = 0;
	};

} // namespace sgre::resource
