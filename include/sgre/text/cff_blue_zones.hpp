#pragma once

#include "sgre/text/cff_hint_map.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace sgre::text {

	inline constexpr std::size_t kCffBlueZoneLimit = 12;

	enum class CffBlueLanguageGroup : std::uint8_t {
		Default = 0,
		Ideographic = 1
	};

	struct CffBlueZoneInput {
		// Absolute private-dictionary coordinates in integer font units; paired bottom/top entries.
		std::span<const std::int32_t> BlueValues;
		std::span<const std::int32_t> OtherBlues;
		std::span<const std::int32_t> FamilyBlues;
		std::span<const std::int32_t> FamilyOtherBlues;
		std::int32_t BlueScale16Dot16 = 2597;
		std::int32_t BlueShiftFontUnits = 7;
		std::int32_t BlueFuzzFontUnits = 1;
		std::int32_t VerticalScale16Dot16 = 65536;
		std::int32_t VerticalDarkening16Dot16 = 0;
		CffBlueLanguageGroup LanguageGroup = CffBlueLanguageGroup::Default;
		bool StemDarkeningEnabled = false;
	};

	struct CffBlueZone {
		std::int32_t CharacterBottom16Dot16 = 0;
		std::int32_t CharacterTop16Dot16 = 0;
		std::int32_t CharacterFlatEdge16Dot16 = 0;
		std::int32_t DeviceFlatEdge16Dot16 = 0;
		bool IsBottom = false;
	};

	struct CffBlueEmBox {
		CffHintEdge Lower;
		CffHintEdge Upper;
	};

	struct CffBlueZones {
		std::array<CffBlueZone, kCffBlueZoneLimit> Zones{};
		std::size_t ZoneCount = 0;
		std::optional<CffBlueEmBox> EmBox;
		std::int32_t VerticalScale16Dot16 = 0;
		std::int32_t BlueScale16Dot16 = 0;
		std::int32_t BlueShift16Dot16 = 0;
		std::int32_t BlueFuzz16Dot16 = 0;
		std::int32_t OvershootBoost16Dot16 = 0;
		bool SuppressOvershoots = false;
	};

	// Reproduces native CF2 normalization from already decoded/quantized private-dictionary values.
	// The returned zones own their values; active-mask selection and stem locking remain separate.
	[[nodiscard]] CffBlueZones BuildCffBlueZones(const CffBlueZoneInput& input);

} // namespace sgre::text
