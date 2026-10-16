#pragma once

#include "sgre/resource/sfnt.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace sgre::text {

	enum class GlyphPositioningBlobStatus : std::uint8_t {
		Empty,
		Accepted,
		Repaired,
		Rejected
	};

	// Clean table bytes borrow the immutable font owner. Offset repair owns its duplicate.
	class GlyphPositioningBlob {
	public:
		explicit GlyphPositioningBlob(const resource::SfntFont& font);
		explicit GlyphPositioningBlob(std::span<const std::uint8_t> bytes, bool initially_immutable = false);
		GlyphPositioningBlob(const GlyphPositioningBlob&) = delete;
		GlyphPositioningBlob& operator=(const GlyphPositioningBlob&) = delete;
		GlyphPositioningBlob(GlyphPositioningBlob&&) = delete;
		GlyphPositioningBlob& operator=(GlyphPositioningBlob&&) = delete;
		[[nodiscard]] GlyphPositioningBlobStatus Status() const;
		[[nodiscard]] std::span<const std::uint8_t> Bytes() const;
		[[nodiscard]] std::uint32_t Length() const;

	private:
		std::span<const std::uint8_t> m_Bytes;
		std::vector<std::uint8_t> m_RepairedBytes;
		GlyphPositioningBlobStatus m_Status = GlyphPositioningBlobStatus::Empty;
	};

} // namespace sgre::text
