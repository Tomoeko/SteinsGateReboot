#include "sgre/text/cff_stem_map.hpp"

#include <array>
#include <cstdint>
#include <iostream>
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

	static CffStemHint Stem(std::int32_t start, std::int32_t end) {
		CffStemHint stem;
		stem.Start16Dot16 = start;
		stem.End16Dot16 = end;

		return stem;
	}

	static CffBlueZones EmptyBlueZones(std::int32_t scale) {
		CffBlueZoneInput input;
		input.VerticalScale16Dot16 = scale;

		return BuildCffBlueZones(input);
	}

	static void CheckMasksAndGlyphCache() {
		const auto blues = EmptyBlueZones(65536);
		CffHintMap map;
		CffHintMap initial;
		std::array<CffStemHint, 1> stems{Stem(0, 65536)};
		CffHintMask mask;
		BuildCffGlyphHintMap(map, initial, blues, stems, 2, mask);
		Require(mask.Valid && !mask.New && mask.HintCount == 3 && mask.Bytes[0] == 0xe0, "a missing mask enables declared horizontal and vertical bits in MSB-first order");
		Require(initial.Initialized && initial.EdgeCount == 1 && initial.Edges[0].CharacterPosition16Dot16 == 0 && initial.Edges[0].Flags == (CffHintEdgeFlags::LowerGhost | CffHintEdgeFlags::Locked | CffHintEdgeFlags::Synthetic), "an empty initial map receives the native synthetic zero anchor");
		Require(map.Initialized && map.EdgeCount == 2 && map.Edges[0].Flags == CffHintEdgeFlags::LowerStem && map.Edges[1].Flags == CffHintEdgeFlags::UpperStem && stems[0].HasCachedDevicePositions && stems[0].CachedLowerDevice16Dot16 == 0 && stems[0].CachedUpperDevice16Dot16 == 65536, "the first active stem pair snaps then caches its mapped positions");
		mask.Bytes[0] = 0x40;
		mask.New = true;
		BuildCffGlyphHintMap(map, initial, blues, stems, 2, mask);
		Require(map.EdgeCount == 0 && stems[0].HasCachedDevicePositions && initial.EdgeCount == 1 && !mask.New, "a vertical-only mask does not select a horizontal stem and retains the glyph cache");
		mask.Bytes[0] = 0x80;
		BuildCffGlyphHintMap(map, initial, blues, stems, 2, mask);
		Require(map.EdgeCount == 2 && map.Edges[0].Flags == (CffHintEdgeFlags::LowerStem | CffHintEdgeFlags::Locked) && map.Edges[1].Flags == (CffHintEdgeFlags::UpperStem | CffHintEdgeFlags::Locked), "cached positions become locked edges on a later mask build");
	}

	static void CheckGhostsAndNegativeWidths() {
		const auto blues = EmptyBlueZones(65536);
		CffHintMap map;
		CffHintMap initial;
		std::array<CffStemHint, 2> stems{Stem(21 * 65536, 0), Stem(10 * 65536, -10 * 65536)};
		stems[0].CachedUpperDevice16Dot16 = 123;
		stems[1].CachedLowerDevice16Dot16 = 456;
		CffHintMask mask;
		CffStemMapCoordinates coordinates;
		coordinates.VerticalDarkening16Dot16 = 65536;
		BuildCffGlyphHintMap(map, initial, blues, stems, 0, mask, coordinates);
		Require(map.EdgeCount == 2 && map.Edges[0].Flags == CffHintEdgeFlags::LowerGhost && map.Edges[1].Flags == CffHintEdgeFlags::UpperGhost, "negative widths minus21/minus20 declare one lower/upper ghost edge");
		Require(map.Edges[0].CharacterPosition16Dot16 == 0 && map.Edges[1].CharacterPosition16Dot16 == 12 * 65536 && stems[0].CachedLowerDevice16Dot16 == 0 && stems[1].CachedUpperDevice16Dot16 == 12 * 65536, "upper ghosts include twice darkening while lower ghosts use the declared end");
		Require(stems[0].CachedUpperDevice16Dot16 == 123 && stems[1].CachedLowerDevice16Dot16 == 456, "ghost cache updates preserve the inactive opposite side");

		CffHintMap reversed_map;
		CffHintMap reversed_initial;
		std::array<CffStemHint, 1> reversed{Stem(10 * 65536, 7 * 65536)};
		CffHintMask reversed_mask;
		BuildCffGlyphHintMap(reversed_map, reversed_initial, blues, reversed, 0, reversed_mask);
		Require(reversed_map.EdgeCount == 2 && reversed_map.Edges[0].CharacterPosition16Dot16 == 7 * 65536 && reversed_map.Edges[1].CharacterPosition16Dot16 == 10 * 65536, "an ordinary negative width reorders geometric stem edges");
	}

	static void CheckBlueLocksAndRejectedStems() {
		const std::array<std::int32_t, 2> values{-10, 0};
		CffBlueZoneInput input;
		input.BlueValues = values;
		input.VerticalScale16Dot16 = 3000;
		const auto blues = BuildCffBlueZones(input);
		CffHintMap map;
		CffHintMap initial;
		map.BaseScale16Dot16 = initial.BaseScale16Dot16 = 3000;
		std::array<CffStemHint, 1> stems{Stem(-7 * 65536, 3 * 65536)};
		CffHintMask mask;
		BuildCffGlyphHintMap(map, initial, blues, stems, 0, mask);
		Require(!blues.SuppressOvershoots && map.EdgeCount == 2 && map.Edges[0].Flags == (CffHintEdgeFlags::LowerStem | CffHintEdgeFlags::Locked) && map.Edges[0].DevicePosition16Dot16 == -65536, "an unsuppressed bottom overshoot at BlueShift retains at least one device pixel");
		Require(initial.EdgeCount == 2 && initial.Edges[0].CharacterPosition16Dot16 < 0 && initial.Edges[1].CharacterPosition16Dot16 > 0, "a blue-locked initial interval containing zero needs no synthetic anchor");

		CffHintMap duplicate_map;
		CffHintMap duplicate_initial;
		std::array<CffStemHint, 2> duplicates{Stem(0, 65536), Stem(0, 65536)};
		CffHintMask duplicate_mask;
		BuildCffGlyphHintMap(duplicate_map, duplicate_initial, EmptyBlueZones(65536), duplicates, 0, duplicate_mask);
		Require(duplicate_map.EdgeCount == 2 && duplicates[0].HasCachedDevicePositions && !duplicates[1].HasCachedDevicePositions, "geometrically rejected duplicate stems do not receive invented cache entries");
	}

	static void CheckInvalidState() {
		const auto blues = EmptyBlueZones(65536);
		CffHintMap map;
		CffHintMap initial;
		std::array<CffStemHint, 1> stems{Stem(0, 65536)};
		CffHintMask mask;
		mask.Valid = true;
		mask.HintCount = 0;
		RequireError([&] {
			BuildCffGlyphHintMap(map, initial, blues, stems, 0, mask);
		},
		    "active-mask count mismatch must fail");
		mask.Valid = false;
		RequireError([&] {
			BuildCffGlyphHintMap(map, initial, blues, stems, 96, mask);
		},
		    "excessive combined stem count must fail");
		RequireError([&] {
			BuildCffGlyphHintMap(map, map, blues, stems, 0, mask);
		},
		    "current and initial map ownership must be distinct");
		CffStemMapCoordinates coordinates;
		coordinates.VerticalDarkening16Dot16 = -1;
		RequireError([&] {
			BuildCffGlyphHintMap(map, initial, blues, stems, 0, mask, coordinates);
		},
		    "negative darkening must fail");
	}

} // namespace sgre::text::test

int main() {
	try {
		sgre::text::test::CheckMasksAndGlyphCache();
		sgre::text::test::CheckGhostsAndNegativeWidths();
		sgre::text::test::CheckBlueLocksAndRejectedStems();
		sgre::text::test::CheckInvalidState();
		std::cout << "CFF stem-map tests passed\n";
		return 0;
	} catch (const std::exception& exception) {
		std::cerr << exception.what() << '\n';
		return 1;
	}
}
