#pragma once

#include "sgre/text/cff_blue_zones.hpp"
#include "sgre/text/cff_hint_map.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace sgre::text {

	inline constexpr std::size_t kCffStemHintLimit = 96;

	struct CffStemHint {
		std::int32_t Start16Dot16 = 0;
		std::int32_t End16Dot16 = 0;
		std::int32_t CachedLowerDevice16Dot16 = 0;
		std::int32_t CachedUpperDevice16Dot16 = 0;
		bool HasCachedDevicePositions = false;
	};

	struct CffHintMask {
		std::array<std::uint8_t, kCffStemHintLimit / 8> Bytes{};
		std::size_t HintCount = 0;
		bool Valid = false;
		bool New = true;
	};

	struct CffStemMapCoordinates {
		std::int32_t VerticalOrigin16Dot16 = 0;
		std::int32_t VerticalDarkening16Dot16 = 0;
	};

	// Horizontal stems borrow mutable glyph-owned storage; cache and initial map persist across that glyph's masks.
	// Vertical declarations contribute mask bits/count only to this recovered Y-axis map construction.
	void BuildCffGlyphHintMap(CffHintMap& map, CffHintMap& initial_map, const CffBlueZones& blue_zones, std::span<CffStemHint> horizontal_stems, std::size_t vertical_stem_count, CffHintMask& mask, const CffStemMapCoordinates& coordinates = {});

} // namespace sgre::text
