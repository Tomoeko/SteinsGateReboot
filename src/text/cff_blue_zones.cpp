#include "sgre/text/cff_blue_zones.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace sgre::text {

	static constexpr std::int32_t kFixedUnit = 65536;
	static constexpr std::int32_t kFixedHalf = 32768;
	static constexpr std::int32_t kMaximumOvershootBoost = 32767;
	static constexpr std::int32_t kOvershootBoostFactor = 0x999a;

	static std::int32_t CheckedBlueCoordinate(std::int64_t value) {
		if (value < std::numeric_limits<std::int32_t>::min() || value > std::numeric_limits<std::int32_t>::max()) {
			throw std::runtime_error("CFF blue-zone arithmetic exceeds signed 16.16 storage");
		}

		return static_cast<std::int32_t>(value);
	}

	static std::int32_t FontUnitsToFixed(std::int32_t value) {
		return CheckedBlueCoordinate(std::int64_t(value) * kFixedUnit);
	}

	static std::int32_t MultiplyBlueFixed(std::int32_t value, std::int32_t scale) {
		const auto product = std::int64_t(value) * scale;
		const auto magnitude = product < 0 ? -product : product;
		const auto rounded = (magnitude + kFixedHalf) / kFixedUnit;

		return CheckedBlueCoordinate(product < 0 ? -rounded : rounded);
	}

	static std::int32_t DivideBluePositive(std::int64_t numerator, std::int32_t denominator) {
		if (numerator < 0 || denominator <= 0) {
			throw std::runtime_error("CFF blue-zone division requires a nonnegative numerator and positive denominator");
		}

		return CheckedBlueCoordinate((numerator + denominator / 2) / denominator);
	}

	static std::int32_t RoundBluePixel(std::int32_t value) {
		const auto shifted = CheckedBlueCoordinate(std::int64_t(value) + kFixedHalf);
		auto whole_pixels = shifted / kFixedUnit;
		if (shifted < 0 && shifted % kFixedUnit != 0) {
			--whole_pixels;
		}

		return CheckedBlueCoordinate(std::int64_t(whole_pixels) * kFixedUnit);
	}

	static void ValidateBlueArray(std::span<const std::int32_t> values, std::size_t maximum_count) {
		if (values.size() > maximum_count || values.size() % 2 != 0) {
			throw std::runtime_error("CFF blue-zone private array has an excessive or unpaired coordinate count");
		}

		for (const auto value : values) {
			(void)FontUnitsToFixed(value);
		}
	}

	static void ValidateBlueInput(const CffBlueZoneInput& input) {
		ValidateBlueArray(input.BlueValues, 14);
		ValidateBlueArray(input.OtherBlues, 10);
		ValidateBlueArray(input.FamilyBlues, 14);
		ValidateBlueArray(input.FamilyOtherBlues, 10);
		if (input.BlueScale16Dot16 <= 0 || input.VerticalScale16Dot16 <= 0 || input.VerticalDarkening16Dot16 < 0 || input.BlueShiftFontUnits < 0 || input.BlueFuzzFontUnits < 0) {
			throw std::runtime_error("CFF blue-zone scales, darkening and thresholds exceed supported positive ranges");
		}
		if (input.LanguageGroup != CffBlueLanguageGroup::Default && input.LanguageGroup != CffBlueLanguageGroup::Ideographic) {
			throw std::runtime_error("CFF blue-zone language group is unsupported");
		}
	}

	static bool UsesEmBox(const CffBlueZoneInput& input) {
		if (input.LanguageGroup != CffBlueLanguageGroup::Ideographic) {
			return false;
		}
		if (input.BlueValues.empty()) {
			return true;
		}

		return input.BlueValues.size() == 4 && input.BlueValues[0] < -120 && input.BlueValues[1] < -120 && input.BlueValues[2] > 880 && input.BlueValues[3] > 880;
	}

	static CffBlueEmBox PrepareEmBox(const CffBlueZoneInput& input) {
		CffBlueEmBox box;
		box.Lower.Flags = CffHintEdgeFlags::LowerGhost | CffHintEdgeFlags::Locked | CffHintEdgeFlags::Synthetic;
		box.Upper.Flags = CffHintEdgeFlags::UpperGhost | CffHintEdgeFlags::Locked | CffHintEdgeFlags::Synthetic;
		box.Lower.CharacterPosition16Dot16 = -120 * kFixedUnit - 1;
		box.Upper.CharacterPosition16Dot16 = CheckedBlueCoordinate(std::int64_t(880) * kFixedUnit + 1 + std::int64_t(input.VerticalDarkening16Dot16) * 2);
		box.Lower.DevicePosition16Dot16 = CheckedBlueCoordinate(std::int64_t(RoundBluePixel(MultiplyBlueFixed(box.Lower.CharacterPosition16Dot16, input.VerticalScale16Dot16))) - kFixedHalf);
		box.Upper.DevicePosition16Dot16 = CheckedBlueCoordinate(std::int64_t(RoundBluePixel(MultiplyBlueFixed(box.Upper.CharacterPosition16Dot16, input.VerticalScale16Dot16))) + kFixedHalf);
		box.Lower.SegmentScale16Dot16 = input.VerticalScale16Dot16;
		box.Upper.SegmentScale16Dot16 = input.VerticalScale16Dot16;

		return box;
	}

	static std::int32_t AppendBlueZones(CffBlueZones& result, const CffBlueZoneInput& input) {
		std::int32_t maximum_height = 0;
		for (std::size_t index = 0; index < input.BlueValues.size(); index += 2) {
			auto bottom = FontUnitsToFixed(input.BlueValues[index]);
			auto top = FontUnitsToFixed(input.BlueValues[index + 1]);
			const auto height = CheckedBlueCoordinate(std::int64_t(top) - bottom);
			if (height < 0) {
				continue;
			}

			maximum_height = std::max(maximum_height, height);
			const bool is_bottom = index == 0;
			if (!is_bottom) {
				bottom = CheckedBlueCoordinate(std::int64_t(bottom) + std::int64_t(input.VerticalDarkening16Dot16) * 2);
				top = CheckedBlueCoordinate(std::int64_t(top) + std::int64_t(input.VerticalDarkening16Dot16) * 2);
			}

			auto& zone = result.Zones[result.ZoneCount++];
			zone.CharacterBottom16Dot16 = bottom;
			zone.CharacterTop16Dot16 = top;
			zone.CharacterFlatEdge16Dot16 = is_bottom ? top : bottom;
			zone.IsBottom = is_bottom;
		}
		for (std::size_t index = 0; index < input.OtherBlues.size(); index += 2) {
			const auto bottom = FontUnitsToFixed(input.OtherBlues[index]);
			const auto top = FontUnitsToFixed(input.OtherBlues[index + 1]);
			const auto height = CheckedBlueCoordinate(std::int64_t(top) - bottom);
			if (height < 0) {
				continue;
			}

			maximum_height = std::max(maximum_height, height);
			auto& zone = result.Zones[result.ZoneCount++];
			zone.CharacterBottom16Dot16 = bottom;
			zone.CharacterTop16Dot16 = top;
			zone.CharacterFlatEdge16Dot16 = top;
			zone.IsBottom = true;
		}

		return maximum_height;
	}

	static bool ConsiderFamilyFlatEdge(CffBlueZone& zone, std::int32_t original_flat_edge, std::int32_t candidate, std::int32_t one_pixel_character_extent, std::int64_t& closest_distance) {
		const auto distance = std::int64_t(original_flat_edge) - candidate;
		const auto magnitude = distance < 0 ? -distance : distance;
		if (magnitude < closest_distance && magnitude < one_pixel_character_extent) {
			zone.CharacterFlatEdge16Dot16 = candidate;
			closest_distance = magnitude;
			return magnitude == 0;
		}

		return false;
	}

	static void SubstituteFamilyFlatEdges(CffBlueZones& result, const CffBlueZoneInput& input, std::int32_t one_pixel_character_extent) {
		for (std::size_t zone_index = 0; zone_index < result.ZoneCount; ++zone_index) {
			auto& zone = result.Zones[zone_index];
			const auto original_flat = zone.CharacterFlatEdge16Dot16;
			std::int64_t closest = std::numeric_limits<std::int32_t>::max();
			if (zone.IsBottom) {
				for (std::size_t index = 1; index < input.FamilyOtherBlues.size(); index += 2) {
					if (ConsiderFamilyFlatEdge(zone, original_flat, FontUnitsToFixed(input.FamilyOtherBlues[index]), one_pixel_character_extent, closest)) {
						break;
					}
				}
				if (input.FamilyBlues.size() >= 2) {
					(void)ConsiderFamilyFlatEdge(zone, original_flat, FontUnitsToFixed(input.FamilyBlues[1]), one_pixel_character_extent, closest);
				}
			} else {
				for (std::size_t index = 2; index < input.FamilyBlues.size(); index += 2) {
					const auto candidate = CheckedBlueCoordinate(std::int64_t(FontUnitsToFixed(input.FamilyBlues[index])) + std::int64_t(input.VerticalDarkening16Dot16) * 2);
					if (ConsiderFamilyFlatEdge(zone, original_flat, candidate, one_pixel_character_extent, closest)) {
						break;
					}
				}
			}
		}
	}

	static void MapBlueFlatEdges(CffBlueZones& result) {
		for (std::size_t index = 0; index < result.ZoneCount; ++index) {
			auto& zone = result.Zones[index];
			const auto mapped = MultiplyBlueFixed(zone.CharacterFlatEdge16Dot16, result.VerticalScale16Dot16);
			const auto boost = zone.IsBottom ? -result.OvershootBoost16Dot16 : result.OvershootBoost16Dot16;
			zone.DeviceFlatEdge16Dot16 = RoundBluePixel(CheckedBlueCoordinate(std::int64_t(mapped) + boost));
		}
	}

	CffBlueZones BuildCffBlueZones(const CffBlueZoneInput& input) {
		ValidateBlueInput(input);
		CffBlueZones result;
		result.VerticalScale16Dot16 = input.VerticalScale16Dot16;
		result.BlueScale16Dot16 = input.BlueScale16Dot16;
		result.BlueShift16Dot16 = FontUnitsToFixed(input.BlueShiftFontUnits);
		result.BlueFuzz16Dot16 = FontUnitsToFixed(input.BlueFuzzFontUnits);
		if (UsesEmBox(input)) {
			result.EmBox = PrepareEmBox(input);
			return result;
		}

		const auto maximum_height = AppendBlueZones(result, input);
		const auto one_pixel_character_extent = DivideBluePositive(std::int64_t(kFixedUnit) * kFixedUnit, input.VerticalScale16Dot16);
		SubstituteFamilyFlatEdges(result, input, one_pixel_character_extent);
		if (maximum_height > 0) {
			result.BlueScale16Dot16 = std::min(result.BlueScale16Dot16, DivideBluePositive(std::int64_t(kFixedUnit) * kFixedUnit, maximum_height));
		}
		if (result.VerticalScale16Dot16 < result.BlueScale16Dot16) {
			result.SuppressOvershoots = true;
			const auto reduction = DivideBluePositive(std::int64_t(kOvershootBoostFactor) * result.VerticalScale16Dot16, result.BlueScale16Dot16);
			result.OvershootBoost16Dot16 = std::min(kMaximumOvershootBoost, kOvershootBoostFactor - reduction);
		}
		if (input.StemDarkeningEnabled) {
			result.OvershootBoost16Dot16 = 0;
		}
		MapBlueFlatEdges(result);

		return result;
	}

} // namespace sgre::text
