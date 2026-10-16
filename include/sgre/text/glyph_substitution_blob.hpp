#pragma once

#include "sgre/resource/sfnt.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace sgre::text {

	enum class GlyphSubstitutionBlobStatus : std::uint8_t {
		Empty,
		Accepted,
		Repaired,
		Rejected
	};

	// Clean table bytes borrow the immutable font owner. Offset repair owns its duplicate.
	class GlyphSubstitutionBlob {
	public:
		explicit GlyphSubstitutionBlob(const resource::SfntFont& font);
		explicit GlyphSubstitutionBlob(std::span<const std::uint8_t> bytes, bool initially_immutable = false);
		GlyphSubstitutionBlob(const GlyphSubstitutionBlob&) = delete;
		GlyphSubstitutionBlob& operator=(const GlyphSubstitutionBlob&) = delete;
		GlyphSubstitutionBlob(GlyphSubstitutionBlob&&) = delete;
		GlyphSubstitutionBlob& operator=(GlyphSubstitutionBlob&&) = delete;
		[[nodiscard]] GlyphSubstitutionBlobStatus Status() const;
		[[nodiscard]] std::span<const std::uint8_t> Bytes() const;
		[[nodiscard]] std::uint32_t Length() const;

	private:
		std::span<const std::uint8_t> m_Bytes;
		std::vector<std::uint8_t> m_RepairedBytes;
		GlyphSubstitutionBlobStatus m_Status = GlyphSubstitutionBlobStatus::Empty;
	};

} // namespace sgre::text
