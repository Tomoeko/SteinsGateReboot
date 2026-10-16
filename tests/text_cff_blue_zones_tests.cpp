#include "sgre/text/cff_blue_zones.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace sgre::text::test {

	static void Require(bool condition, std::string_view message) {
		if (!condition) {
			throw std::runtime_error(std::string(message));
		}
	}

	template <typename Operation>
	static void RequireError(Operation operation, std::string_view message) {
		try {
			operation();
		} catch (const std::runtime_error&) {
			return;
		}

		throw std::runtime_error(std::string(message));
	}

	static void CheckFamilyZonesAndSuppression() {
		const std::array<std::int32_t, 4> blue{-10, 0, 700, 710};
		const std::array<std::int32_t, 2> other{-250, -240};
		const std::array<std::int32_t, 4> family{-20, 1, 701, 711};
		const std::array<std::int32_t, 2> family_other{-251, -241};
		CffBlueZoneInput input;
		input.BlueValues = blue;
		input.OtherBlues = other;
		input.FamilyBlues = family;
		input.FamilyOtherBlues = family_other;
		input.VerticalScale16Dot16 = 1573;
		input.VerticalDarkening16Dot16 = 65536;
		const auto result = BuildCffBlueZones(input);
		Require(!result.EmBox && result.ZoneCount == 3 && result.BlueScale16Dot16 == 2597 && result.BlueShift16Dot16 == 458752 && result.BlueFuzz16Dot16 == 65536, "ordinary private blue arrays preserve normalized scale and thresholds");
		Require(result.SuppressOvershoots && result.OvershootBoost16Dot16 == 15505, "small device scale produces the measured overshoot boost");
		Require(result.Zones[0].IsBottom && result.Zones[0].CharacterBottom16Dot16 == -655360 && result.Zones[0].CharacterTop16Dot16 == 0 && result.Zones[0].CharacterFlatEdge16Dot16 == 65536 && result.Zones[0].DeviceFlatEdge16Dot16 == 0, "bottom family substitution moves only the flat edge");
		Require(!result.Zones[1].IsBottom && result.Zones[1].CharacterBottom16Dot16 == 702 * 65536 && result.Zones[1].CharacterTop16Dot16 == 712 * 65536 && result.Zones[1].CharacterFlatEdge16Dot16 == 703 * 65536 && result.Zones[1].DeviceFlatEdge16Dot16 == 1114112, "top zones and their family candidates include twice the vertical darkening");
		Require(result.Zones[2].IsBottom && result.Zones[2].CharacterFlatEdge16Dot16 == -241 * 65536 && result.Zones[2].DeviceFlatEdge16Dot16 == -393216, "OtherBlues use bottom-zone family matching and signed pixel rounding");
		input.StemDarkeningEnabled = true;
		const auto darkened = BuildCffBlueZones(input);
		Require(darkened.SuppressOvershoots && darkened.OvershootBoost16Dot16 == 0, "stem darkening disables boost while preserving suppression state");
	}

	static void CheckIdeographicEmBox() {
		CffBlueZoneInput input;
		input.LanguageGroup = CffBlueLanguageGroup::Ideographic;
		input.VerticalScale16Dot16 = 1573;
		const auto empty = BuildCffBlueZones(input);
		Require(empty.EmBox && empty.ZoneCount == 0 && !empty.SuppressOvershoots && empty.OvershootBoost16Dot16 == 0, "an ideographic font without blue values uses synthetic em-box edges");
		Require(empty.EmBox->Lower.Flags == (CffHintEdgeFlags::LowerGhost | CffHintEdgeFlags::Locked | CffHintEdgeFlags::Synthetic) && empty.EmBox->Upper.Flags == (CffHintEdgeFlags::UpperGhost | CffHintEdgeFlags::Locked | CffHintEdgeFlags::Synthetic), "em-box edge flags retain synthetic locked ghost kinds");
		Require(empty.EmBox->Lower.CharacterPosition16Dot16 == -7864321 && empty.EmBox->Lower.DevicePosition16Dot16 == -229376 && empty.EmBox->Upper.CharacterPosition16Dot16 == 57671681 && empty.EmBox->Upper.DevicePosition16Dot16 == 1409024, "em-box boundaries retain the native one-unit epsilon and half-pixel margins");
		const std::array<std::int32_t, 4> distant{-250, -250, 1100, 1100};
		input.BlueValues = distant;
		const auto wide = BuildCffBlueZones(input);
		Require(wide.EmBox && wide.EmBox->Lower.DevicePosition16Dot16 == empty.EmBox->Lower.DevicePosition16Dot16, "the original wide four-entry ideographic profile selects the same em box");
		const std::array<std::int32_t, 4> boundary{-120, -120, 1100, 1100};
		input.BlueValues = boundary;
		Require(!BuildCffBlueZones(input).EmBox, "em-box replacement excludes equality at its character boundary");
	}

	static void CheckNearestAndScaleLimits() {
		const std::array<std::int32_t, 2> blue{-10, 0};
		const std::array<std::int32_t, 4> tied{-10, -1, 0, 1};
		const std::array<std::int32_t, 2> family{-5, 1};
		CffBlueZoneInput input;
		input.BlueValues = blue;
		input.FamilyOtherBlues = tied;
		input.FamilyBlues = family;
		input.VerticalScale16Dot16 = 32768;
		const auto tie = BuildCffBlueZones(input);
		Require(tie.Zones[0].CharacterFlatEdge16Dot16 == -65536, "equally near family candidates retain the first substitution");
		const std::array<std::int32_t, 2> exact_pixel{-3, -2};
		input.FamilyOtherBlues = exact_pixel;
		input.FamilyBlues = {};
		Require(BuildCffBlueZones(input).Zones[0].CharacterFlatEdge16Dot16 == 0, "a family candidate exactly one device pixel away is excluded");
		const std::array<std::int32_t, 2> tall{0, 200};
		input.BlueValues = tall;
		input.FamilyOtherBlues = {};
		input.VerticalScale16Dot16 = 1573;
		const auto reduced = BuildCffBlueZones(input);
		Require(reduced.BlueScale16Dot16 == 328 && !reduced.SuppressOvershoots && reduced.OvershootBoost16Dot16 == 0, "maximum valid zone height limits BlueScale before suppression");
		const std::array<std::int32_t, 4> reversed{10, 0, 700, 710};
		input.BlueValues = reversed;
		const auto skipped = BuildCffBlueZones(input);
		Require(skipped.ZoneCount == 1 && !skipped.Zones[0].IsBottom, "a reversed first zone is skipped without changing the later zone kind");
		input.BlueValues = {};
		input.VerticalScale16Dot16 = 3;
		input.BlueScale16Dot16 = 65536;
		Require(BuildCffBlueZones(input).OvershootBoost16Dot16 == 32767, "overshoot boost is capped below a half pixel");
	}

	static void CheckInvalidParameters() {
		CffBlueZoneInput input;
		const std::array<std::int32_t, 1> odd{0};
		input.BlueValues = odd;
		RequireError([&] {
			(void)BuildCffBlueZones(input);
		},
		    "unpaired blue values must fail");
		const std::array<std::int32_t, 16> excessive{};
		input.BlueValues = excessive;
		RequireError([&] {
			(void)BuildCffBlueZones(input);
		},
		    "excessive blue values must fail");
		const std::array<std::int32_t, 2> overflowing{0, std::numeric_limits<std::int32_t>::max()};
		input.BlueValues = overflowing;
		RequireError([&] {
			(void)BuildCffBlueZones(input);
		},
		    "font-unit conversion overflow must fail");
		input.BlueValues = {};
		input.VerticalScale16Dot16 = 0;
		RequireError([&] {
			(void)BuildCffBlueZones(input);
		},
		    "zero vertical scale must fail");
		input.VerticalScale16Dot16 = 1;
		RequireError([&] {
			(void)BuildCffBlueZones(input);
		},
		    "an unrepresentable inverse character scale must fail");
		input.VerticalScale16Dot16 = 1573;
		input.LanguageGroup = static_cast<CffBlueLanguageGroup>(2);
		RequireError([&] {
			(void)BuildCffBlueZones(input);
		},
		    "unsupported language group must fail");
		input.LanguageGroup = CffBlueLanguageGroup::Default;
		input.BlueFuzzFontUnits = -1;
		RequireError([&] {
			(void)BuildCffBlueZones(input);
		},
		    "negative blue fuzz must fail");
	}

} // namespace sgre::text::test

int main() {
	try {
		sgre::text::test::CheckFamilyZonesAndSuppression();
		sgre::text::test::CheckIdeographicEmBox();
		sgre::text::test::CheckNearestAndScaleLimits();
		sgre::text::test::CheckInvalidParameters();
		std::cout << "CFF blue-zone tests passed\n";
		return 0;
	} catch (const std::exception& exception) {
		std::cerr << exception.what() << '\n';
		return 1;
	}
}
