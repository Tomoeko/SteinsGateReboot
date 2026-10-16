#pragma once

#include "sgre/text/cff_stem_map.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace sgre::text {

	struct CffCounterMaskConfiguration {
		std::int32_t BaseScale16Dot16 = 65536;
		std::int32_t VerticalDarkening16Dot16 = 0;
		bool HintingEnabled = true;
	};

	struct CffCounterMaskPreparation {
		CffHintMap Map;
		CffHintMask Mask;
		std::size_t ConsumedByteCount = 0;
	};

	// Copies the exact primary-mask byte count, retaining unused bytes in the glyph-owned mask.
	// For a counter mask, call with a fresh temporary mask; its resulting map is a separate phase.
	[[nodiscard]] std::size_t UpdateCffCharstringHintMask(CffHintMask& mask, std::span<const std::uint8_t> program_bytes, std::size_t horizontal_stem_count, std::size_t vertical_stem_count);

	// The temporary counter map may initialize the glyph's parent and update declared-stem caches.
	// It neither replaces a primary mask nor a pending contour's current/snapshot maps.
	[[nodiscard]] CffCounterMaskPreparation PrepareCffCounterHintMap(CffHintMap& initial_map, const CffBlueZones& blue_zones, std::span<CffStemHint> horizontal_stems, std::size_t vertical_stem_count, std::span<const std::uint8_t> program_bytes, const CffCounterMaskConfiguration& configuration = {});

} // namespace sgre::text
