#include "sgre/text/cff_stem_map.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace sgre::text {

	static constexpr std::int32_t kFixedUnit = 65536;
	static constexpr std::int32_t kFixedHalf = 32768;

	struct CffStemEdges {
		CffHintEdge Lower;
		CffHintEdge Upper;
	};

	static bool HasStemFlag(CffHintEdgeFlags flags, CffHintEdgeFlags flag) {
		return (static_cast<std::uint8_t>(flags) & static_cast<std::uint8_t>(flag)) != 0;
	}

	static std::int32_t CheckedStemCoordinate(std::int64_t value) {
		if (value < std::numeric_limits<std::int32_t>::min() || value > std::numeric_limits<std::int32_t>::max()) {
			throw std::runtime_error("CFF stem-map arithmetic exceeds signed 16.16 storage");
		}

		return static_cast<std::int32_t>(value);
	}

	static std::int32_t MultiplyStemFixed(std::int32_t value, std::int32_t scale) {
		const auto product = std::int64_t(value) * scale;
		const auto magnitude = product < 0 ? -product : product;
		const auto rounded = (magnitude + kFixedHalf) / kFixedUnit;

		return CheckedStemCoordinate(product < 0 ? -rounded : rounded);
	}

	static std::int32_t RoundStemPixel(std::int32_t value) {
		const auto shifted = CheckedStemCoordinate(std::int64_t(value) + kFixedHalf);
		auto pixels = shifted / kFixedUnit;
		if (shifted < 0 && shifted % kFixedUnit != 0) {
			--pixels;
		}

		return CheckedStemCoordinate(std::int64_t(pixels) * kFixedUnit);
	}

	static bool MaskSelects(const CffHintMask& mask, std::size_t index) {
		return (mask.Bytes[index / 8] & (0x80U >> (index % 8))) != 0;
	}

	static void ClearMaskSelection(CffHintMask& mask, std::size_t index) {
		mask.Bytes[index / 8] &= static_cast<std::uint8_t>(~(0x80U >> (index % 8)));
	}

	static void InitializeAllHintBits(CffHintMask& mask, std::size_t count) {
		mask.HintCount = count;
		mask.Valid = true;
		mask.New = true;
		const auto byte_count = (count + 7) / 8;
		for (std::size_t index = 0; index < byte_count; ++index) {
			mask.Bytes[index] = 255;
		}
		if (count != 0) {
			mask.Bytes[byte_count - 1] &= static_cast<std::uint8_t>(255U << ((8 - count % 8) % 8));
		}
	}

	static CffHintEdge PrepareStemEdge(const CffStemHint& stem, std::size_t index, CffHintEdgeFlags kind, std::int32_t character_position, std::int32_t scale, std::int32_t vertical_origin, std::int32_t darkening) {
		CffHintEdge edge;
		edge.Flags = kind;
		edge.StemIndex = index;
		edge.SegmentScale16Dot16 = scale;
		const bool is_upper = HasStemFlag(kind, CffHintEdgeFlags::UpperGhost) || HasStemFlag(kind, CffHintEdgeFlags::UpperStem);
		edge.CharacterPosition16Dot16 = CheckedStemCoordinate(std::int64_t(character_position) + vertical_origin + (is_upper ? std::int64_t(darkening) * 2 : 0));
		if (kind != CffHintEdgeFlags::Inactive && stem.HasCachedDevicePositions) {
			edge.Flags = edge.Flags | CffHintEdgeFlags::Locked;
			edge.DevicePosition16Dot16 = is_upper ? stem.CachedUpperDevice16Dot16 : stem.CachedLowerDevice16Dot16;
		} else {
			edge.DevicePosition16Dot16 = MultiplyStemFixed(edge.CharacterPosition16Dot16, scale);
		}

		return edge;
	}

	static CffStemEdges PrepareStemEdges(const CffStemHint& stem, std::size_t index, std::int32_t scale, std::int32_t vertical_origin, std::int32_t darkening) {
		const auto width = CheckedStemCoordinate(std::int64_t(stem.End16Dot16) - stem.Start16Dot16);
		CffHintEdgeFlags lower_kind = CffHintEdgeFlags::LowerStem;
		CffHintEdgeFlags upper_kind = CffHintEdgeFlags::UpperStem;
		auto lower_position = std::min(stem.Start16Dot16, stem.End16Dot16);
		auto upper_position = std::max(stem.Start16Dot16, stem.End16Dot16);
		if (width == -21 * kFixedUnit) {
			lower_kind = CffHintEdgeFlags::LowerGhost;
			upper_kind = CffHintEdgeFlags::Inactive;
			lower_position = stem.End16Dot16;
			upper_position = 0;
		} else if (width == -20 * kFixedUnit) {
			lower_kind = CffHintEdgeFlags::Inactive;
			upper_kind = CffHintEdgeFlags::UpperGhost;
			lower_position = 0;
			upper_position = stem.Start16Dot16;
		}

		CffStemEdges edges;
		edges.Lower = PrepareStemEdge(stem, index, lower_kind, lower_position, scale, vertical_origin, darkening);
		edges.Upper = PrepareStemEdge(stem, index, upper_kind, upper_position, scale, vertical_origin, darkening);

		return edges;
	}

	static bool LockStemToBlueZone(CffStemEdges& edges, const CffBlueZones& blue_zones) {
		for (std::size_t index = 0; index < blue_zones.ZoneCount; ++index) {
			const auto& zone = blue_zones.Zones[index];
			auto& edge = zone.IsBottom ? edges.Lower : edges.Upper;
			const bool correct_kind = zone.IsBottom ? HasStemFlag(edge.Flags, CffHintEdgeFlags::LowerGhost) || HasStemFlag(edge.Flags, CffHintEdgeFlags::LowerStem) : HasStemFlag(edge.Flags, CffHintEdgeFlags::UpperGhost) || HasStemFlag(edge.Flags, CffHintEdgeFlags::UpperStem);
			const auto minimum = CheckedStemCoordinate(std::int64_t(zone.CharacterBottom16Dot16) - blue_zones.BlueFuzz16Dot16);
			const auto maximum = CheckedStemCoordinate(std::int64_t(zone.CharacterTop16Dot16) + blue_zones.BlueFuzz16Dot16);
			if (!correct_kind || edge.CharacterPosition16Dot16 < minimum || edge.CharacterPosition16Dot16 > maximum) {
				continue;
			}

			std::int32_t target = zone.DeviceFlatEdge16Dot16;
			if (!blue_zones.SuppressOvershoots) {
				const auto distance = zone.IsBottom ? std::int64_t(zone.CharacterTop16Dot16) - edge.CharacterPosition16Dot16 : std::int64_t(edge.CharacterPosition16Dot16) - zone.CharacterBottom16Dot16;
				target = RoundStemPixel(edge.DevicePosition16Dot16);
				if (distance >= blue_zones.BlueShift16Dot16) {
					const auto outside = CheckedStemCoordinate(std::int64_t(zone.DeviceFlatEdge16Dot16) + (zone.IsBottom ? -kFixedUnit : kFixedUnit));
					target = zone.IsBottom ? std::min(target, outside) : std::max(target, outside);
				}
			}

			const auto delta = CheckedStemCoordinate(std::int64_t(target) - edge.DevicePosition16Dot16);
			if (edges.Lower.Flags != CffHintEdgeFlags::Inactive) {
				edges.Lower.DevicePosition16Dot16 = CheckedStemCoordinate(std::int64_t(edges.Lower.DevicePosition16Dot16) + delta);
				edges.Lower.Flags = edges.Lower.Flags | CffHintEdgeFlags::Locked;
			}
			if (edges.Upper.Flags != CffHintEdgeFlags::Inactive) {
				edges.Upper.DevicePosition16Dot16 = CheckedStemCoordinate(std::int64_t(edges.Upper.DevicePosition16Dot16) + delta);
				edges.Upper.Flags = edges.Upper.Flags | CffHintEdgeFlags::Locked;
			}

			return true;
		}

		return false;
	}

	static void InsertStemPair(CffHintMap& map, CffHintMap& initial_map, const CffStemEdges& edges) {
		(void)InsertCffHintEdges(map, initial_map, edges.Lower, edges.Upper);
	}

	static void CacheMappedStemEdges(const CffHintMap& map, std::span<CffStemHint> stems) {
		for (std::size_t index = 0; index < map.EdgeCount; ++index) {
			const auto& edge = map.Edges[index];
			if (HasStemFlag(edge.Flags, CffHintEdgeFlags::Synthetic)) {
				continue;
			}
			if (edge.StemIndex >= stems.size()) {
				throw std::runtime_error("CFF stem-map edge has an out-of-range glyph stem identity");
			}

			auto& stem = stems[edge.StemIndex];
			if (HasStemFlag(edge.Flags, CffHintEdgeFlags::UpperGhost) || HasStemFlag(edge.Flags, CffHintEdgeFlags::UpperStem)) {
				stem.CachedUpperDevice16Dot16 = edge.DevicePosition16Dot16;
			} else {
				stem.CachedLowerDevice16Dot16 = edge.DevicePosition16Dot16;
			}
			stem.HasCachedDevicePositions = true;
		}
	}

	static void BuildPreparedStemMap(CffHintMap& map, CffHintMap& initial_map, const CffBlueZones& blue_zones, std::span<CffStemHint> stems, CffHintMask& mask, std::int32_t vertical_origin, std::int32_t darkening, bool initial) {
		map.EdgeCount = 0;
		map.Cursor = 0;
		if (blue_zones.EmBox) {
			(void)InsertCffHintEdges(map, initial_map, blue_zones.EmBox->Lower, {});
			(void)InsertCffHintEdges(map, initial_map, {}, blue_zones.EmBox->Upper);
		}

		auto unlocked_mask = mask;
		for (std::size_t index = 0; index < stems.size(); ++index) {
			if (!MaskSelects(unlocked_mask, index)) {
				continue;
			}

			const auto edges = PrepareStemEdges(stems[index], index, map.BaseScale16Dot16, vertical_origin, darkening);
			const bool cached = HasStemFlag(edges.Lower.Flags, CffHintEdgeFlags::Locked) || HasStemFlag(edges.Upper.Flags, CffHintEdgeFlags::Locked);
			auto locked_edges = edges;
			if (cached || LockStemToBlueZone(locked_edges, blue_zones)) {
				InsertStemPair(map, initial_map, locked_edges);
				// The native builder clears a selected locked bit even if geometric insertion rejects it.
				ClearMaskSelection(unlocked_mask, index);
			}
		}
		if (initial) {
			if (map.EdgeCount == 0 || map.Edges[0].CharacterPosition16Dot16 > 0 || map.Edges[map.EdgeCount - 1].CharacterPosition16Dot16 < 0) {
				CffHintEdge zero;
				zero.Flags = CffHintEdgeFlags::LowerGhost | CffHintEdgeFlags::Locked | CffHintEdgeFlags::Synthetic;
				zero.SegmentScale16Dot16 = map.BaseScale16Dot16;
				(void)InsertCffHintEdges(map, initial_map, zero, {});
			}
		} else {
			for (std::size_t index = 0; index < stems.size(); ++index) {
				if (MaskSelects(unlocked_mask, index)) {
					InsertStemPair(map, initial_map, PrepareStemEdges(stems[index], index, map.BaseScale16Dot16, vertical_origin, darkening));
				}
			}
		}

		SnapCffHintMap(map);
		if (!initial) {
			CacheMappedStemEdges(map, stems);
		}
		map.Initialized = true;
		mask.New = false;
	}

	void BuildCffGlyphHintMap(CffHintMap& map, CffHintMap& initial_map, const CffBlueZones& blue_zones, std::span<CffStemHint> horizontal_stems, std::size_t vertical_stem_count, CffHintMask& mask, const CffStemMapCoordinates& coordinates) {
		if (&map == &initial_map) {
			throw std::runtime_error("CFF glyph current and initial hint maps must be distinct owners");
		}
		if (horizontal_stems.size() > kCffStemHintLimit || vertical_stem_count > kCffStemHintLimit - horizontal_stems.size()) {
			throw std::runtime_error("CFF glyph stem declarations exceed the 96-hint mask bound");
		}
		if (blue_zones.ZoneCount > blue_zones.Zones.size() || blue_zones.VerticalScale16Dot16 <= 0 || blue_zones.BlueFuzz16Dot16 < 0 || blue_zones.BlueShift16Dot16 < 0 || map.BaseScale16Dot16 <= 0 || initial_map.BaseScale16Dot16 <= 0 || coordinates.VerticalDarkening16Dot16 < 0) {
			throw std::runtime_error("CFF glyph prepared blue zones or map scales are invalid");
		}

		const auto count = horizontal_stems.size() + vertical_stem_count;
		if (mask.Valid && mask.HintCount != count) {
			throw std::runtime_error("CFF glyph active mask count differs from its declared stems");
		}
		if (!initial_map.Initialized) {
			CffHintMask all_hints;
			InitializeAllHintBits(all_hints, count);
			BuildPreparedStemMap(initial_map, initial_map, blue_zones, horizontal_stems, all_hints, coordinates.VerticalOrigin16Dot16, coordinates.VerticalDarkening16Dot16, true);
		}
		if (!mask.Valid) {
			InitializeAllHintBits(mask, count);
		}
		BuildPreparedStemMap(map, initial_map, blue_zones, horizontal_stems, mask, coordinates.VerticalOrigin16Dot16, coordinates.VerticalDarkening16Dot16, false);
	}

} // namespace sgre::text
