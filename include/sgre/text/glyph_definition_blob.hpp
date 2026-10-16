#pragma once

#include "sgre/resource/sfnt.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace sgre::text {

	enum class GlyphDefinitionBlobStatus : std::uint8_t {
		Empty,
		Accepted,
		Repaired,
		Rejected
	};

	// The original immutable font bytes outlive this owner. Only repair duplicates table bytes.
	class GlyphDefinitionBlob {
	public:
		explicit GlyphDefinitionBlob(const resource::SfntFont& font);
		explicit GlyphDefinitionBlob(std::span<const std::uint8_t> bytes, bool initially_immutable = false);
		GlyphDefinitionBlob(const GlyphDefinitionBlob&) = delete;
		GlyphDefinitionBlob& operator=(const GlyphDefinitionBlob&) = delete;
		GlyphDefinitionBlob(GlyphDefinitionBlob&&) = delete;
		GlyphDefinitionBlob& operator=(GlyphDefinitionBlob&&) = delete;
		[[nodiscard]] GlyphDefinitionBlobStatus Status() const;
		[[nodiscard]] std::span<const std::uint8_t> Bytes() const;
		[[nodiscard]] std::uint32_t Length() const;
		[[nodiscard]] bool HasGlyphClasses() const;
		[[nodiscard]] std::uint16_t GlyphClass(std::uint32_t glyph_index) const;
		[[nodiscard]] std::uint16_t GlyphProperties(std::uint32_t glyph_index) const;
		// Original exception policy uses sanitized GSUB/GPOS blob lengths; the retained GDEF blob is not erased.
		void ApplyLayoutSuppression(std::uint32_t substitution_length, std::uint32_t positioning_length);
		[[nodiscard]] bool IsSuppressed() const;

	private:
		std::span<const std::uint8_t> m_Bytes;
		std::vector<std::uint8_t> m_RepairedBytes;
		GlyphDefinitionBlobStatus m_Status = GlyphDefinitionBlobStatus::Empty;
		bool m_Suppressed = false;
	};

} // namespace sgre::text
